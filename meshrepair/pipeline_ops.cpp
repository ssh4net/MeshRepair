#include "pipeline_ops.h"
#include "parallel_detection.h"
#include "debug_path.h"
#include "mesh_preprocessor.h"
#include "logger.h"
#include <CGAL/bounding_box.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>

namespace MeshRepair {

namespace {

    struct ParallelLogEntry {
        uint64_t seq;
        LogLevel level;
        LogCategory category;
        std::string message;
    };

    struct ParallelLogBuffer {
        std::atomic<uint64_t> counter { 0 };
        std::mutex mutex;
        std::vector<ParallelLogEntry> entries;
        bool enabled = true;
    };

    void append_parallel_log(ParallelLogBuffer* buffer, LogLevel level, LogCategory category, const std::string& msg)
    {
        if (!buffer || !buffer->enabled) {
            switch (level) {
            case LogLevel::Error: logError(category, msg); break;
            case LogLevel::Warn: logWarn(category, msg); break;
            case LogLevel::Detail: logDetail(category, msg); break;
            case LogLevel::Debug: logDebug(category, msg); break;
            case LogLevel::Info:
            default: logInfo(category, msg); break;
            }
            return;
        }

        uint64_t seq = buffer->counter.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(buffer->mutex);
        buffer->entries.push_back(ParallelLogEntry { seq, level, category, msg });
    }

    void flush_parallel_logs(ParallelLogBuffer& buffer)
    {
        std::vector<ParallelLogEntry> entries;
        {
            std::lock_guard<std::mutex> lock(buffer.mutex);
            if (buffer.entries.empty()) {
                return;
            }
            entries.swap(buffer.entries);
        }

        std::sort(entries.begin(), entries.end(),
                  [](const ParallelLogEntry& a, const ParallelLogEntry& b) { return a.seq < b.seq; });

        for (const auto& entry : entries) {
            switch (entry.level) {
            case LogLevel::Error: logError(entry.category, entry.message); break;
            case LogLevel::Warn: logWarn(entry.category, entry.message); break;
            case LogLevel::Detail: logDetail(entry.category, entry.message); break;
            case LogLevel::Debug: logDebug(entry.category, entry.message); break;
            case LogLevel::Info:
            default: logInfo(entry.category, entry.message); break;
            }
        }
    }

    void detect_holes_async(PipelineContext& ctx, BoundedQueue<HoleInfo>& hole_queue, std::atomic<bool>& detection_done,
                            std::atomic<size_t>& holes_detected, ParallelLogBuffer* log_buffer)
    {
        Mesh& mesh = *ctx.mesh;

        std::vector<halfedge_descriptor> border_halfedges;
        auto& pool = ctx.thread_mgr->detection_pool;
        if (pool.threadCount() > 1) {
            border_halfedges = find_border_halfedges_parallel(mesh, pool, false);
        } else {
            for (auto h : mesh.halfedges()) {
                if (mesh.is_border(h)) {
                    border_halfedges.push_back(h);
                }
            }
        }

        if (ctx.options.verbose) {
            append_parallel_log(log_buffer, LogLevel::Info, LogCategory::Fill,
                                "[Detection] Found " + std::to_string(border_halfedges.size()) + " border halfedges");
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
                append_parallel_log(log_buffer, LogLevel::Info, LogCategory::Fill,
                                    "[Pipeline] Hole detected (" + std::to_string(hole.boundary_size)
                                        + " vertices), queued for filling");
            }
        }

        detection_done.store(true);

        if (ctx.options.verbose) {
            append_parallel_log(log_buffer, LogLevel::Info, LogCategory::Fill,
                                "[Pipeline] Detection complete: " + std::to_string(holes_detected.load())
                                    + " hole(s) found");
        }
    }

    void fill_holes_async(PipelineContext& ctx, BoundedQueue<HoleInfo>& hole_queue, std::atomic<bool>& detection_done,
                          std::vector<HoleStatistics>& results, std::mutex& results_mutex,
                          ParallelLogBuffer* log_buffer)
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
                append_parallel_log(log_buffer, LogLevel::Error, LogCategory::Fill,
                                    std::string("Exception during fill_hole: ") + e.what());
                stats.filled_successfully = false;
                stats.error_message       = std::string("Exception: ") + e.what();
            } catch (...) {
                append_parallel_log(log_buffer, LogLevel::Error, LogCategory::Fill,
                                    "Unknown exception during hole filling");
                stats.filled_successfully = false;
                stats.error_message       = "Unknown exception during hole filling";
            }

            {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(stats);

                if (ctx.options.verbose) {
                    if (stats.filled_successfully) {
                        std::string msg = "[Pipeline] Hole filled: " + std::to_string(stats.num_faces_added)
                                          + " faces, " + std::to_string(stats.num_vertices_added) + " vertices added"
                                          + (stats.fairing_succeeded ? "" : " [FAIRING FAILED]");
                        append_parallel_log(log_buffer, LogLevel::Info, LogCategory::Fill, msg);
                    } else {
                        if (!stats.error_message.empty()) {
                            append_parallel_log(log_buffer, LogLevel::Error, LogCategory::Fill,
                                                "[Pipeline] Hole filling FAILED: " + stats.error_message);
                        } else {
                            append_parallel_log(log_buffer, LogLevel::Error, LogCategory::Fill,
                                                "[Pipeline] Hole filling FAILED");
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

    auto fill_start = start_time;

    stats.original_vertices = mesh.number_of_vertices();
    stats.original_faces    = mesh.number_of_faces();

    thread_manager_enter_pipeline(mgr);

    size_t queue_memory = mgr.config.queue_size * sizeof(HoleInfo) * 100;
    BoundedQueue<HoleInfo> hole_queue(queue_memory);

    std::atomic<bool> detection_done { false };
    std::atomic<size_t> holes_detected { 0 };
    std::vector<HoleStatistics> results;
    std::mutex results_mutex;
    ParallelLogBuffer parallel_logs;

    if (verbose) {
        std::ostringstream start_log;
        start_log << "[Pipeline] Starting detection and filling in parallel\n";
        start_log << "[Pipeline] Detection: " << mgr.config.detection_threads << " thread(s)\n";
        start_log << "[Pipeline] Filling: " << mgr.config.filling_threads << " thread(s)\n";
        start_log << "[Pipeline] Queue size: " << mgr.config.queue_size << " holes";
        logInfo(LogCategory::Fill, start_log.str());
    }

    auto& detection_pool = mgr.detection_pool;

    if (verbose) {
        logInfo(LogCategory::Fill, "[Pipeline] Enqueueing detection task...");
    }

    std::atomic<bool> detection_finished { false };
    std::atomic<double> detection_time_ms { 0.0 };
    detection_pool.enqueue([&ctx, &hole_queue, &detection_done, &holes_detected, verbose, &detection_finished,
                            &detection_time_ms, &parallel_logs]() {
        if (verbose) {
            append_parallel_log(&parallel_logs, LogLevel::Info, LogCategory::Fill,
                                "[Pipeline] Detection thread started");
        }
        auto local_start = std::chrono::high_resolution_clock::now();
        detect_holes_async(*ctx, hole_queue, detection_done, holes_detected, &parallel_logs);
        auto local_end = std::chrono::high_resolution_clock::now();
        detection_time_ms.store(std::chrono::duration<double, std::milli>(local_end - local_start).count(),
                                std::memory_order_release);
        detection_finished.store(true, std::memory_order_release);
        if (verbose) {
            append_parallel_log(&parallel_logs, LogLevel::Info, LogCategory::Fill,
                                "[Pipeline] Detection thread finished");
        }
    });

    std::atomic<size_t> filling_active { 0 };
    std::vector<std::thread> filling_threads;

    fill_start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < mgr.config.filling_threads; ++i) {
        filling_threads.emplace_back([ctx, &hole_queue, &detection_done, &results, &results_mutex, i, verbose,
                                      &filling_active, &parallel_logs]() {
            filling_active.fetch_add(1, std::memory_order_relaxed);
            if (verbose) {
                append_parallel_log(&parallel_logs, LogLevel::Info, LogCategory::Fill,
                                    "[Pipeline] Filling thread " + std::to_string(i) + " started");
            }
            fill_holes_async(*ctx, hole_queue, detection_done, results, results_mutex, &parallel_logs);
            if (verbose) {
                append_parallel_log(&parallel_logs, LogLevel::Info, LogCategory::Fill,
                                    "[Pipeline] Filling thread " + std::to_string(i) + " finished");
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
    stats.detection_time_ms  = detection_time_ms.load(std::memory_order_acquire);
    stats.fill_time_ms       = std::chrono::duration<double, std::milli>(end_time - fill_start).count();
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

    flush_parallel_logs(parallel_logs);

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

    auto detect_start = std::chrono::high_resolution_clock::now();

    if (verbose) {
        logInfo(LogCategory::Fill, "[Batch] Detecting all holes first...");
    }

    HoleDetectorCtx detect_ctx { ctx->mesh, verbose };
    std::vector<HoleInfo> holes;
    detect_all_holes_ctx(detect_ctx, holes);

    auto detect_end          = std::chrono::high_resolution_clock::now();
    double detection_time_ms = std::chrono::duration<double, std::milli>(detect_end - detect_start).count();

    if (holes.empty()) {
        stats.original_vertices = mesh.number_of_vertices();
        stats.original_faces    = mesh.number_of_faces();
        stats.final_vertices    = stats.original_vertices;
        stats.final_faces       = stats.original_faces;
        stats.detection_time_ms = detection_time_ms;
        stats.total_time_ms     = detection_time_ms;
        return stats;
    }

    if (verbose) {
        logInfo(LogCategory::Fill, "[Batch] Filling " + std::to_string(holes.size()) + " hole(s)...");
    }

    thread_manager_enter_filling(mgr);

    HoleFillerCtx fill_ctx;
    fill_ctx.mesh                = &mesh;
    fill_ctx.options             = ctx->options;
    MeshStatistics fill_stats    = fill_all_holes_ctx(&fill_ctx, holes);
    fill_stats.detection_time_ms = detection_time_ms;
    fill_stats.total_time_ms += detection_time_ms;
    return fill_stats;
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

    auto start_time       = std::chrono::high_resolution_clock::now();
    auto detect_start     = start_time;
    auto detect_end       = start_time;
    auto partition_end    = start_time;
    auto neighborhood_end = start_time;
    auto extraction_end   = start_time;
    auto fill_end         = start_time;
    auto cleanup_end      = start_time;

    stats.original_vertices = mesh.number_of_vertices();
    stats.original_faces    = mesh.number_of_faces();

    if (verbose) {
        logInfo(LogCategory::Fill, "[Partitioned] Phase 1: Detecting holes...");
    }

    HoleDetectorCtx detect_ctx { &mesh, verbose };
    std::vector<HoleInfo> all_holes;
    detect_all_holes_ctx(detect_ctx, all_holes);
    detect_end               = std::chrono::high_resolution_clock::now();
    stats.detection_time_ms  = std::chrono::duration<double, std::milli>(detect_end - detect_start).count();
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
                    logInfo(LogCategory::Fill, "[Partitioned] Skipping selection boundary hole: "
                                                   + std::to_string(hole.boundary_size) + " vertices");
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
        cleanup_end         = detect_end;
        stats.total_time_ms = std::chrono::duration<double, std::milli>(cleanup_end - start_time).count();
        return stats;
    }

    if (verbose) {
        std::ostringstream holes_msg;
        holes_msg << "[Partitioned] Found " << all_holes.size() << " hole(s), " << holes.size()
                  << " fillable (skipped: " << selection_boundary_skipped << " selection boundaries, "
                  << oversized_skipped << " oversized)";
        logInfo(LogCategory::Fill, holes_msg.str());
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
        std::ostringstream partition_msg;
        if (filling_options.min_partition_boundary_edges > 0) {
            partition_msg << "[Partitioned] Boundary edge budget: " << total_boundary_edges << " edges, minimum "
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

        partition_msg << "[Partitioned] Created " << partitions.size() << " partition(s) for " << effective_threads
                      << " thread(s)" << (effective_threads < requested_partitions ? " (clamped by hole count)" : "")
                      << ":\n";
        for (size_t i = 0; i < partitions.size(); ++i) {
            partition_msg << "  Partition " << i << ": " << partitions[i].size() << " hole(s), "
                          << partition_edge_load[i] << " boundary edges\n";
        }
        logInfo(LogCategory::Fill, partition_msg.str());
    }
    partition_end           = std::chrono::high_resolution_clock::now();
    stats.partition_time_ms = std::chrono::duration<double, std::milli>(partition_end - detect_end).count();
    partition_end           = std::chrono::high_resolution_clock::now();
    stats.partition_time_ms = std::chrono::duration<double, std::milli>(partition_end - detect_end).count();

    std::vector<HoleWithNeighborhood> neighborhoods(holes.size());
    for (size_t i = 0; i < holes.size(); ++i) {
        neighborhoods[i] = partition_compute_neighborhood(part_ctx, holes[i]);
    }
    neighborhood_end           = std::chrono::high_resolution_clock::now();
    stats.neighborhood_time_ms = std::chrono::duration<double, std::milli>(neighborhood_end - partition_end).count();

    if (verbose) {
        logInfo(LogCategory::Fill, "[Partitioned] Computed " + std::to_string(neighborhoods.size())
                                       + " neighborhood(s) with " + std::to_string(partition_ring_count(part_ctx))
                                       + "-ring radius");
    }

    SubmeshExtractorCtx extractor_ctx { &mesh };
    std::vector<Submesh> submeshes(partitions.size());
    for (size_t i = 0; i < partitions.size(); ++i) {
        submeshes[i] = submesh_extract_partition(extractor_ctx, partitions[i], holes, neighborhoods);
    }
    extraction_end           = std::chrono::high_resolution_clock::now();
    stats.extraction_time_ms = std::chrono::duration<double, std::milli>(extraction_end - neighborhood_end).count();

    if (debug) {
        std::string prefix = MeshRepair::DebugPath::start_step("partition");
        for (size_t i = 0; i < submeshes.size(); ++i) {
            std::ostringstream oss;
            oss << prefix << "_partition_" << std::setw(3) << std::setfill('0') << i << "_unfilled.ply";
            std::string debug_file = oss.str();
            CGAL::IO::write_PLY(debug_file, submeshes[i].mesh, CGAL::parameters::use_binary_mode(true));
        }
    }

    std::vector<FilledSubmesh> filled_submeshes(submeshes.size());
    std::atomic<size_t> next_partition { 0 };
    const size_t worker_count = effective_threads > 0 ? effective_threads : 1;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back([&submeshes, &filled_submeshes, &filling_options, &next_partition]() {
            while (true) {
                size_t idx = next_partition.fetch_add(1, std::memory_order_relaxed);
                if (idx >= submeshes.size()) {
                    break;
                }
                FilledSubmesh filled  = fill_submesh_holes(std::move(submeshes[idx]), filling_options);
                filled_submeshes[idx] = std::move(filled);
            }
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    fill_end           = std::chrono::high_resolution_clock::now();
    stats.fill_time_ms = std::chrono::duration<double, std::milli>(fill_end - extraction_end).count();

    for (auto& fs : filled_submeshes) {
        stats.num_holes_filled += fs.stats.num_holes_filled;
        stats.num_holes_failed += fs.stats.num_holes_failed;
        stats.num_holes_skipped += fs.stats.num_holes_skipped;
        if (!fs.stats.hole_details.empty()) {
            stats.hole_details.insert(stats.hole_details.end(), std::make_move_iterator(fs.stats.hole_details.begin()),
                                      std::make_move_iterator(fs.stats.hole_details.end()));
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

    auto merge_start = fill_end;
    MergeTiming merge_timings;
    Mesh merged    = mesh_merger_merge(mesh, filled_meshes, verbose, filling_options.holes_only, debug, &merge_timings);
    mesh           = std::move(merged);
    auto merge_end = std::chrono::high_resolution_clock::now();
    stats.merge_time_ms                  = std::chrono::duration<double, std::milli>(merge_end - merge_start).count();
    stats.merge_dedup_ms                 = merge_timings.dedup_ms;
    stats.merge_copy_base_ms             = merge_timings.copy_base_ms;
    stats.merge_append_ms                = merge_timings.append_ms;
    stats.merge_repair_ms                = merge_timings.repair_ms;
    stats.merge_orient_ms                = merge_timings.orient_ms;
    stats.merge_convert_ms               = merge_timings.convert_ms;
    stats.merge_validation_removed       = merge_timings.validation_removed;
    stats.merge_validation_out_of_bounds = merge_timings.validation_out_of_bounds;
    stats.merge_validation_invalid_cycle = merge_timings.validation_invalid_cycle;
    stats.merge_validation_edge_orientation = merge_timings.validation_edge_orientation;
    stats.merge_validation_non_manifold     = merge_timings.validation_non_manifold;
    stats.merge_validation_passes           = merge_timings.validation_passes;

    // Cleanup after merge to ensure consistent indexing and remove garbage
    auto cleanup_start = merge_end;
    if (mesh.has_garbage()) {
        mesh.collect_garbage();
    }
    MeshPreprocessor cleanup(mesh);
    cleanup.remove_isolated_vertices();
    if (!filling_options.holes_only && filling_options.keep_largest_component) {
        cleanup.keep_only_largest_connected_component();
    }
    cleanup_end           = std::chrono::high_resolution_clock::now();
    stats.cleanup_time_ms = std::chrono::duration<double, std::milli>(cleanup_end - cleanup_start).count();

    if (debug) {
        std::string debug_file = MeshRepair::DebugPath::step_file(
            filling_options.holes_only ? "merged_partitions_holes_only_clean" : "merged_partitions_clean");
        CGAL::IO::write_PLY(debug_file, mesh, CGAL::parameters::use_binary_mode(true));
    }

    stats.final_vertices = mesh.number_of_vertices();
    stats.final_faces    = mesh.number_of_faces();

    auto end_time       = cleanup_end;
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
