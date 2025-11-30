#include "pipeline_ops.h"
#include "parallel_detection.h"
#include "thread_safe_cout.h"
#include "debug_path.h"
#include "mesh_preprocessor.h"
#include <CGAL/bounding_box.h>
#include <chrono>
#include <iostream>
#include <unordered_set>

namespace MeshRepair {

namespace {

    void detect_holes_async(PipelineContext& ctx, BoundedQueue<HoleInfo>& hole_queue, std::atomic<bool>& detection_done,
                            std::atomic<size_t>& holes_detected)
    {
        Mesh& mesh = *ctx.mesh;

        std::vector<halfedge_descriptor> border_halfedges;
        auto& pool = ctx.thread_mgr->detection_pool;
        if (pool.threadCount() > 1) {
            border_halfedges = find_border_halfedges_parallel(mesh, pool, ctx.options.verbose);
        } else {
            for (auto h : mesh.halfedges()) {
                if (mesh.is_border(h)) {
                    border_halfedges.push_back(h);
                }
            }

            if (ctx.options.verbose) {
                std::cerr << "  [Detection] Found " << border_halfedges.size() << " border halfedges\n";
            }
        }

        std::unordered_set<halfedge_descriptor> processed;

        for (auto h : border_halfedges) {
            if (processed.find(h) != processed.end()) {
                continue;
            }

            HoleInfo hole = analyze_hole(mesh, h);

            auto h_current = h;
            do {
                processed.insert(h_current);
                h_current = mesh.next(h_current);
            } while (h_current != h);

            hole_queue.push(hole);
            holes_detected.fetch_add(1);

            if (ctx.options.verbose) {
                std::cerr << "  [Pipeline] Hole detected (" << hole.boundary_size << " vertices), queued for filling\n";
            }
        }

        detection_done.store(true);

        if (ctx.options.verbose) {
            std::cerr << "  [Pipeline] Detection complete: " << holes_detected.load() << " hole(s) found\n";
        }
    }

    void fill_holes_async(PipelineContext& ctx, BoundedQueue<HoleInfo>& hole_queue, std::atomic<bool>& detection_done,
                          std::vector<HoleStatistics>& results, std::mutex& results_mutex)
    {
        (void)detection_done;

        static std::mutex mesh_modification_mutex;

        while (true) {
            HoleInfo hole;
            if (!hole_queue.pop(hole)) {
                break;
            }

            HoleStatistics stats;

            try {
                std::unique_lock<std::mutex> lock(mesh_modification_mutex);

                FillingOptions non_verbose_opts = ctx.options;
                non_verbose_opts.verbose        = false;

                HoleFillerCtx fill_ctx;
                fill_ctx.mesh    = ctx.mesh;
                fill_ctx.options = non_verbose_opts;

                stats = fill_hole_ctx(&fill_ctx, hole);

                lock.unlock();
            } catch (const std::exception& e) {
                thread_safe_log_err("  [ERROR] Exception during fill_hole: ");
                thread_safe_log_err(e.what());
                thread_safe_log_err("\n");
                stats.filled_successfully = false;
                stats.error_message       = std::string("Exception: ") + e.what();
            } catch (...) {
                thread_safe_log_err("  [ERROR] Unknown exception during fill_hole\n");
                stats.filled_successfully = false;
                stats.error_message       = "Unknown exception during hole filling";
            }

            {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(stats);

                if (ctx.options.verbose) {
                    if (stats.filled_successfully) {
                        std::string msg = "  [Pipeline] Hole filled: " + std::to_string(stats.num_faces_added)
                                          + " faces, " + std::to_string(stats.num_vertices_added) + " vertices added"
                                          + (stats.fairing_succeeded ? "" : " [FAIRING FAILED]") + "\n";
                        thread_safe_log(msg);
                    } else {
                        if (!stats.error_message.empty()) {
                            std::string msg = "  [Pipeline] Hole filling FAILED: " + stats.error_message + "\n";
                            thread_safe_log(msg);
                        } else {
                            thread_safe_log("  [Pipeline] Hole filling FAILED\n");
                        }
                    }
                }
            }
        }
    }

}  // namespace

MeshStatistics
pipeline_process_pipeline(PipelineContext* ctx, bool verbose)
{
    MeshStatistics stats;
    if (!ctx || !ctx->mesh || !ctx->thread_mgr) {
        return stats;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    Mesh& mesh          = *ctx->mesh;
    ThreadManager& mgr  = *ctx->thread_mgr;
    FillingOptions opts = ctx->options;

    stats.original_vertices = mesh.number_of_vertices();
    stats.original_faces    = mesh.number_of_faces();

    thread_manager_enter_pipeline(mgr);

    size_t queue_memory = mgr.config.queue_size * sizeof(HoleInfo) * 100;
    BoundedQueue<HoleInfo> hole_queue(queue_memory);

    std::atomic<bool> detection_done { false };
    std::atomic<size_t> holes_detected { 0 };
    std::vector<HoleStatistics> results;
    std::mutex results_mutex;

    if (verbose) {
        std::cerr << "[Pipeline] Starting detection and filling in parallel\n";
        std::cerr << "[Pipeline] Detection: " << mgr.config.detection_threads << " thread(s)\n";
        std::cerr << "[Pipeline] Filling: " << mgr.config.filling_threads << " thread(s)\n";
        std::cerr << "[Pipeline] Queue size: " << mgr.config.queue_size << " holes\n\n";
    }

    auto& detection_pool = mgr.detection_pool;

    if (verbose) {
        std::cerr << "[Pipeline] Enqueueing detection task...\n";
    }

    std::atomic<bool> detection_finished { false };
    detection_pool.enqueue([&ctx, &hole_queue, &detection_done, &holes_detected, verbose, &detection_finished]() {
        if (verbose) {
            thread_safe_log("[Pipeline] Detection thread started\n");
        }
        detect_holes_async(*ctx, hole_queue, detection_done, holes_detected);
        detection_finished.store(true, std::memory_order_release);
        if (verbose) {
            thread_safe_log("[Pipeline] Detection thread finished\n");
        }
    });

    std::atomic<size_t> filling_active { 0 };
    std::vector<std::thread> filling_threads;

    for (size_t i = 0; i < mgr.config.filling_threads; ++i) {
        filling_threads.emplace_back(
            [ctx, &hole_queue, &detection_done, &results, &results_mutex, i, verbose, &filling_active]() {
                filling_active.fetch_add(1, std::memory_order_relaxed);
                if (verbose) {
                    thread_safe_log("[Pipeline] Filling thread " + std::to_string(i) + " started\n");
                }
                fill_holes_async(*ctx, hole_queue, detection_done, results, results_mutex);
                if (verbose) {
                    thread_safe_log("[Pipeline] Filling thread " + std::to_string(i) + " finished\n");
                }
                filling_active.fetch_sub(1, std::memory_order_relaxed);
            });
    }

    while (!detection_finished.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    hole_queue.finish();

    for (auto& t : filling_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    stats.num_holes_detected = holes_detected.load();
    stats.total_time_ms      = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    {
        std::lock_guard<std::mutex> lock(results_mutex);
        stats.hole_details = std::move(results);

        for (const auto& h : stats.hole_details) {
            if (h.filled_successfully) {
                stats.num_holes_filled++;
            } else {
                stats.num_holes_failed++;
            }
        }
    }

    stats.final_vertices = mesh.number_of_vertices();
    stats.final_faces    = mesh.number_of_faces();

    return stats;
}

MeshStatistics
pipeline_process_batch(PipelineContext* ctx, bool verbose)
{
    MeshStatistics stats;
    if (!ctx || !ctx->mesh || !ctx->thread_mgr) {
        return stats;
    }

    Mesh& mesh         = *ctx->mesh;
    ThreadManager& mgr = *ctx->thread_mgr;

    if (verbose) {
        std::cerr << "[Batch] Detecting all holes first...\n";
    }

    HoleDetectorCtx detect_ctx { ctx->mesh, verbose };
    std::vector<HoleInfo> holes;
    detect_all_holes_ctx(detect_ctx, holes);

    if (holes.empty()) {
        stats.original_vertices = mesh.number_of_vertices();
        stats.original_faces    = mesh.number_of_faces();
        stats.final_vertices    = stats.original_vertices;
        stats.final_faces       = stats.original_faces;
        return stats;
    }

    if (verbose) {
        std::cerr << "[Batch] Filling " << holes.size() << " hole(s)...\n";
    }

    thread_manager_enter_filling(mgr);

    HoleFillerCtx fill_ctx;
    fill_ctx.mesh    = &mesh;
    fill_ctx.options = ctx->options;
    return fill_all_holes_ctx(&fill_ctx, holes);
}

MeshStatistics
parallel_fill_partitioned(ParallelPipelineCtx* ctx, bool verbose, bool debug)
{
    MeshStatistics stats;
    if (!ctx || !ctx->mesh || !ctx->thread_mgr) {
        return stats;
    }

    Mesh& mesh                     = *ctx->mesh;
    ThreadManager& manager         = *ctx->thread_mgr;
    FillingOptions filling_options = ctx->options;

    auto start_time = std::chrono::high_resolution_clock::now();

    stats.original_vertices = mesh.number_of_vertices();
    stats.original_faces    = mesh.number_of_faces();

    if (verbose) {
        std::cerr << "\n[Partitioned] Phase 1: Detecting holes...\n";
    }

    HoleDetectorCtx detect_ctx { &mesh, verbose };
    std::vector<HoleInfo> all_holes;
    detect_all_holes_ctx(detect_ctx, all_holes);
    stats.num_holes_detected = all_holes.size();

    if (all_holes.empty()) {
        stats.final_vertices = filling_options.holes_only ? 0 : stats.original_vertices;
        stats.final_faces    = filling_options.holes_only ? 0 : stats.original_faces;
        if (filling_options.holes_only) {
            mesh.clear();
        }
        return stats;
    }

    std::vector<HoleInfo> holes;
    holes.reserve(all_holes.size());
    size_t selection_boundary_skipped = 0;
    size_t oversized_skipped          = 0;

    double ref_diagonal = filling_options.reference_bbox_diagonal;
    if (ref_diagonal <= 0.0) {
        std::vector<Point_3> all_points;
        all_points.reserve(mesh.number_of_vertices());
        for (auto v : mesh.vertices()) {
            all_points.push_back(mesh.point(v));
        }
        if (!all_points.empty()) {
            auto bbox    = CGAL::bounding_box(all_points.begin(), all_points.end());
            ref_diagonal = std::sqrt(CGAL::to_double(CGAL::squared_distance(bbox.min(), bbox.max())));
        }
    }

    for (const auto& hole : all_holes) {
        if (hole.boundary_size > filling_options.max_hole_boundary_vertices) {
            oversized_skipped++;
            continue;
        }

        if (ref_diagonal > 0.0 && hole.estimated_diameter > ref_diagonal * filling_options.max_hole_diameter_ratio) {
            oversized_skipped++;
            continue;
        }

        if (filling_options.guard_selection_boundary && !filling_options.selection_boundary_vertices.empty()) {
            bool all_boundary = true;
            for (const auto& v : hole.boundary_vertices) {
                uint32_t v_idx = static_cast<uint32_t>(v.idx());
                if (filling_options.selection_boundary_vertices.find(v_idx)
                    == filling_options.selection_boundary_vertices.end()) {
                    all_boundary = false;
                    break;
                }
            }
            if (all_boundary) {
                selection_boundary_skipped++;
                if (verbose) {
                    std::cerr << "[Partitioned] Skipping selection boundary hole: " << hole.boundary_size
                              << " vertices\n";
                }
                continue;
            }
        }

        holes.push_back(hole);
    }

    stats.num_holes_skipped = selection_boundary_skipped + oversized_skipped;
    if (holes.empty()) {
        stats.final_vertices = filling_options.holes_only ? 0 : stats.original_vertices;
        stats.final_faces    = filling_options.holes_only ? 0 : stats.original_faces;
        if (filling_options.holes_only) {
            mesh.clear();
        }
        return stats;
    }

    if (verbose) {
        std::cerr << "[Partitioned] Found " << all_holes.size() << " hole(s), " << holes.size()
                  << " fillable (skipped: " << selection_boundary_skipped << " selection boundaries, "
                  << oversized_skipped << " oversized)\n";
    }

    unsigned int rings = filling_options.fairing_continuity;
    if (rings == 0) {
        rings = 1;  // At least one ring needed to build a partition/submesh
    }

    size_t total_boundary_edges = 0;
    for (const auto& hole : holes) {
        total_boundary_edges += hole.boundary_size;
    }

    MeshPartitionerCtx part_ctx { &mesh, rings };
    const size_t requested_partitions = manager.config.filling_threads;

    size_t max_by_edge_budget = requested_partitions;
    if (filling_options.min_partition_boundary_edges > 0) {
        max_by_edge_budget = std::max<size_t>(1, total_boundary_edges / filling_options.min_partition_boundary_edges);
    }

    const size_t partitions_requested = std::min(requested_partitions, max_by_edge_budget);
    auto partitions                   = partition_holes_by_count(holes, partitions_requested);
    const size_t effective_threads    = partitions.size();

    if (verbose) {
        if (filling_options.min_partition_boundary_edges > 0) {
            std::cerr << "[Partitioned] Boundary edge budget: " << total_boundary_edges << " edges, minimum "
                      << filling_options.min_partition_boundary_edges << " per partition -> up to "
                      << max_by_edge_budget << " partition(s)\n";
        }

        std::vector<size_t> partition_edge_load(partitions.size(), 0);
        for (size_t i = 0; i < partitions.size(); ++i) {
            size_t boundary_sum = 0;
            for (size_t idx : partitions[i]) {
                boundary_sum += holes[idx].boundary_size;
            }
            partition_edge_load[i] = boundary_sum;
        }

        std::cerr << "[Partitioned] Created " << partitions.size() << " partition(s) for " << effective_threads
                  << " thread(s)" << (effective_threads < requested_partitions ? " (clamped by hole count)" : "")
                  << ":\n";
        for (size_t i = 0; i < partitions.size(); ++i) {
            std::cerr << "  Partition " << i << ": " << partitions[i].size() << " hole(s), " << partition_edge_load[i]
                      << " boundary edges\n";
        }
    }

    std::vector<HoleWithNeighborhood> neighborhoods(holes.size());
    for (size_t i = 0; i < holes.size(); ++i) {
        neighborhoods[i] = partition_compute_neighborhood(part_ctx, holes[i]);
    }

    if (verbose) {
        std::cerr << "[Partitioned] Computed " << neighborhoods.size() << " neighborhood(s) with "
                  << partition_ring_count(part_ctx) << "-ring radius\n";
    }

    SubmeshExtractorCtx extractor_ctx { &mesh };
    std::vector<Submesh> submeshes(partitions.size());
    for (size_t i = 0; i < partitions.size(); ++i) {
        submeshes[i] = submesh_extract_partition(extractor_ctx, partitions[i], holes, neighborhoods);
    }

    if (debug) {
        std::string prefix = MeshRepair::DebugPath::start_step("partition");
        for (size_t i = 0; i < submeshes.size(); ++i) {
            std::ostringstream oss;
            oss << prefix << "_partition_" << std::setw(3) << std::setfill('0') << i << "_unfilled.ply";
            std::string debug_file = oss.str();
            CGAL::IO::write_PLY(debug_file, submeshes[i].mesh, CGAL::parameters::use_binary_mode(true));
        }
    }

    std::vector<FilledSubmesh> filled_submeshes;
    filled_submeshes.reserve(submeshes.size());
    for (size_t i = 0; i < submeshes.size(); ++i) {
        filled_submeshes.push_back(fill_submesh_holes(std::move(submeshes[i]), filling_options));
        stats.num_holes_filled += filled_submeshes.back().stats.num_holes_filled;
        stats.num_holes_failed += filled_submeshes.back().stats.num_holes_failed;
        stats.num_holes_skipped += filled_submeshes.back().stats.num_holes_skipped;
        if (!filled_submeshes.back().stats.hole_details.empty()) {
            stats.hole_details.insert(stats.hole_details.end(),
                                      std::make_move_iterator(filled_submeshes.back().stats.hole_details.begin()),
                                      std::make_move_iterator(filled_submeshes.back().stats.hole_details.end()));
        }
    }

    if (debug) {
        std::string prefix = MeshRepair::DebugPath::start_step("partition_filled");
        for (size_t i = 0; i < filled_submeshes.size(); ++i) {
            std::ostringstream oss;
            oss << prefix << "_partition_" << std::setw(3) << std::setfill('0') << i << "_filled.ply";
            std::string debug_file = oss.str();
            CGAL::IO::write_PLY(debug_file, filled_submeshes[i].submesh.mesh, CGAL::parameters::use_binary_mode(true));
        }
    }

    std::vector<Submesh> filled_meshes;
    filled_meshes.reserve(filled_submeshes.size());
    for (auto& fs : filled_submeshes) {
        filled_meshes.push_back(std::move(fs.submesh));
    }

    Mesh merged = mesh_merger_merge(mesh, filled_meshes, verbose, filling_options.holes_only, debug);
    mesh        = std::move(merged);

    // Cleanup after merge to ensure consistent indexing and remove garbage
    if (mesh.has_garbage()) {
        mesh.collect_garbage();
    }
    MeshPreprocessor cleanup(mesh);
    cleanup.remove_isolated_vertices();
    if (!filling_options.holes_only && filling_options.keep_largest_component) {
        cleanup.keep_only_largest_connected_component();
    }

    if (debug) {
        std::string debug_file = MeshRepair::DebugPath::step_file(
            filling_options.holes_only ? "merged_partitions_holes_only_clean" : "merged_partitions_clean");
        CGAL::IO::write_PLY(debug_file, mesh, CGAL::parameters::use_binary_mode(true));
    }

    stats.final_vertices = mesh.number_of_vertices();
    stats.final_faces    = mesh.number_of_faces();

    auto end_time       = std::chrono::high_resolution_clock::now();
    stats.total_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return stats;
}

FilledSubmesh
fill_submesh_holes(Submesh submesh, const FillingOptions& options)
{
    FillingOptions thread_options = options;
    thread_options.verbose        = false;

    HoleFillerCtx ctx;
    ctx.mesh    = &submesh.mesh;
    ctx.options = thread_options;

    MeshStatistics stats = fill_all_holes_ctx(&ctx, submesh.holes);

    FilledSubmesh result;
    result.submesh = std::move(submesh);
    result.stats   = std::move(stats);
    return result;
}

int
process_pipeline_c(Mesh& mesh, ThreadManager& thread_manager, const FillingOptions& options, bool verbose,
                   MeshStatistics* out_stats)
{
    PipelineContext ctx;
    ctx.mesh             = &mesh;
    ctx.thread_mgr       = &thread_manager;
    ctx.options          = options;
    MeshStatistics stats = pipeline_process_pipeline(&ctx, verbose);
    if (out_stats) {
        *out_stats = stats;
    }
    return 0;
}

int
process_batch_c(Mesh& mesh, ThreadManager& thread_manager, const FillingOptions& options, bool verbose,
                MeshStatistics* out_stats)
{
    PipelineContext ctx;
    ctx.mesh             = &mesh;
    ctx.thread_mgr       = &thread_manager;
    ctx.options          = options;
    MeshStatistics stats = pipeline_process_batch(&ctx, verbose);
    if (out_stats) {
        *out_stats = stats;
    }
    return 0;
}

}  // namespace MeshRepair
#include <CGAL/Polygon_mesh_processing/repair.h>
