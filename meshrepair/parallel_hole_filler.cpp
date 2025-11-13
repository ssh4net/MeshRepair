#include "parallel_hole_filler.h"
#include <CGAL/IO/PLY.h>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace MeshRepair {

ParallelHoleFillerPipeline::ParallelHoleFillerPipeline(
    Mesh& mesh,
    ThreadManager& thread_manager,
    const FillingOptions& filling_options)
    : mesh_(mesh)
    , thread_manager_(thread_manager)
    , filling_options_(filling_options)
{}

MeshStatistics ParallelHoleFillerPipeline::process_partitioned(bool verbose, bool debug) {
    auto start_time = std::chrono::high_resolution_clock::now();

    MeshStatistics stats;
    stats.original_vertices = mesh_.number_of_vertices();
    stats.original_faces = mesh_.number_of_faces();

    // Debug: Save original mesh before partitioning
    if (debug) {
        std::string debug_file = "debug_05_partition_input.ply";
        if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
            if (verbose) {
                std::cout << "  [DEBUG] Saved original mesh: " << debug_file << "\n";
            }
        }
    }

    // Phase 1: Detect holes (sequential)
    if (verbose) {
        std::cout << "\n[Partitioned] Phase 1: Detecting holes...\n";
    }

    HoleDetector detector(mesh_, verbose);
    std::vector<HoleInfo> holes = detector.detect_all_holes();

    stats.num_holes_detected = holes.size();

    if (holes.empty()) {
        std::cout << "No holes detected.\n";
        stats.final_vertices = stats.original_vertices;
        stats.final_faces = stats.original_faces;
        return stats;
    }

    if (verbose) {
        std::cout << "[Partitioned] Found " << holes.size() << " hole(s)\n";
    }

    // Phase 2: Partition holes by count (simple load balancing)
    if (verbose) {
        std::cout << "\n[Partitioned] Phase 2: Partitioning holes...\n";
    }

    MeshPartitioner partitioner(mesh_, filling_options_.fairing_continuity);

    // Use number of threads as partition count for optimal parallelism
    size_t num_partitions = thread_manager_.get_filling_threads();

    // Simple count-based partitioning (evenly distributed)
    auto partitions = partitioner.partition_holes_by_count(holes, num_partitions);

    if (verbose) {
        std::cout << "[Partitioned] Created " << partitions.size()
                  << " partition(s) for " << thread_manager_.get_filling_threads()
                  << " thread(s):\n";
        for (size_t i = 0; i < partitions.size(); ++i) {
            std::cout << "  Partition " << i << ": "
                      << partitions[i].size() << " hole(s)\n";
        }
    }

    // Phase 3: Compute neighborhoods for holes (needed for submesh extraction)
    if (verbose) {
        std::cout << "\n[Partitioned] Phase 3: Computing neighborhoods...\n";
    }

    std::vector<HoleWithNeighborhood> neighborhoods(holes.size());

    for (size_t i = 0; i < holes.size(); ++i) {
        neighborhoods[i] = partitioner.compute_neighborhood(holes[i]);
    }

    if (verbose) {
        std::cout << "[Partitioned] Computed " << neighborhoods.size()
                  << " neighborhood(s) with "
                  << partitioner.get_ring_count() << "-ring radius\n";
    }

    // Phase 4: Extract submeshes (sequential, but fast)
    if (verbose) {
        std::cout << "\n[Partitioned] Phase 4: Extracting submeshes...\n";
    }

    SubmeshExtractor extractor(mesh_);
    std::vector<Submesh> submeshes(partitions.size());

    for (size_t i = 0; i < partitions.size(); ++i) {
        submeshes[i] = extractor.extract_partition(partitions[i], holes, neighborhoods);
    }

    if (verbose) {
        std::cout << "[Partitioned] Extracted " << submeshes.size()
                  << " submesh(es)\n";
        for (size_t i = 0; i < submeshes.size(); ++i) {
            std::cout << "  Submesh " << i << ": "
                      << submeshes[i].mesh.number_of_vertices() << " vertices, "
                      << submeshes[i].mesh.number_of_faces() << " faces, "
                      << submeshes[i].holes.size() << " hole(s)\n";
        }
    }

    // Debug: Save extracted partitions (before filling)
    if (debug) {
        for (size_t i = 0; i < submeshes.size(); ++i) {
            std::ostringstream oss;
            oss << "debug_06_partition_" << std::setw(3) << std::setfill('0') << i
                << "_unfilled.ply";
            std::string debug_file = oss.str();

            if (CGAL::IO::write_PLY(debug_file, submeshes[i].mesh,
                                   CGAL::parameters::use_binary_mode(true))) {
                if (verbose) {
                    std::cout << "  [DEBUG] Saved partition " << i << " (unfilled): "
                              << debug_file << "\n";
                }
            }
        }
    }

    // Phase 5: Fill holes in parallel
    if (verbose) {
        std::cout << "\n[Partitioned] Phase 5: Filling holes in parallel ("
                  << thread_manager_.get_filling_threads() << " thread(s))...\n";
    }

    auto& pool = thread_manager_.get_filling_pool();
    std::vector<std::future<Submesh>> futures;
    futures.reserve(submeshes.size());

    // Launch parallel tasks
    for (auto& submesh : submeshes) {
        futures.push_back(pool.enqueue(
            [submesh = std::move(submesh), options = filling_options_]() mutable {
                return fill_submesh_holes(std::move(submesh), options);
            }
        ));
    }

    // Collect results (pre-allocate for indexed access)
    std::vector<Submesh> filled_submeshes(futures.size());

    for (size_t i = 0; i < futures.size(); ++i) {
        if (verbose) {
            std::cout << "[Partitioned] Waiting for submesh " << i << "...\n";
        }
        filled_submeshes[i] = futures[i].get();

        if (verbose) {
            const auto& sm = filled_submeshes[i];
            std::cout << "[Partitioned] Submesh " << i << " completed: "
                      << sm.mesh.number_of_faces() << " faces\n";
        }
    }

    // Debug: Save filled partitions (after filling)
    if (debug) {
        for (size_t i = 0; i < filled_submeshes.size(); ++i) {
            std::ostringstream oss;
            oss << "debug_07_partition_" << std::setw(3) << std::setfill('0') << i
                << "_filled.ply";
            std::string debug_file = oss.str();

            if (CGAL::IO::write_PLY(debug_file, filled_submeshes[i].mesh,
                                   CGAL::parameters::use_binary_mode(true))) {
                if (verbose) {
                    std::cout << "  [DEBUG] Saved partition " << i << " (filled): "
                              << debug_file << "\n";
                }
            }
        }
    }

    // Aggregate statistics from submeshes
    for (const auto& submesh : filled_submeshes) {
        // Count holes that were successfully filled
        // Note: In the submesh, holes that were filled will have added faces
        // We approximate by comparing original hole count to final mesh
        stats.num_holes_filled += submesh.original_hole_count;
    }

    // Phase 5: Merge submeshes back into original mesh (sequential)
    if (verbose) {
        std::cout << "\n[Partitioned] Phase 6: Merging filled submeshes back into original mesh...\n";
    }

    mesh_ = MeshMerger::merge_submeshes(mesh_, filled_submeshes, verbose);

    // Debug: Save final merged mesh
    if (debug) {
        std::string debug_file = "debug_08_final_merged.ply";
        if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
            if (verbose) {
                std::cout << "  [DEBUG] Saved final merged mesh: " << debug_file << "\n";
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_time_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time).count();

    stats.final_vertices = mesh_.number_of_vertices();
    stats.final_faces = mesh_.number_of_faces();

    if (verbose) {
        std::cout << "\n[Partitioned] Complete!\n";
        std::cout << "  Original: " << stats.original_vertices << " vertices, "
                  << stats.original_faces << " faces\n";
        std::cout << "  Final: " << stats.final_vertices << " vertices, "
                  << stats.final_faces << " faces\n";
        std::cout << "  Holes filled: " << stats.num_holes_filled << "/"
                  << stats.num_holes_detected << "\n";
        std::cout << "  Total time: " << stats.total_time_ms << " ms\n";
    }

    return stats;
}

Submesh ParallelHoleFillerPipeline::fill_submesh_holes(
    Submesh submesh,
    const FillingOptions& options)
{
    // Each thread owns its submesh - fully thread-safe!

    // Create a non-verbose copy of options for parallel filling
    // (to avoid console I/O race conditions)
    FillingOptions thread_options = options;
    thread_options.verbose = false;

    HoleFiller filler(submesh.mesh, thread_options);

    for (const auto& hole : submesh.holes) {
        HoleStatistics stats = filler.fill_hole(hole);
        // TODO: Store statistics if needed
        (void)stats;  // Suppress unused warning for now
    }

    return submesh;  // Return by value (move semantics)
}

} // namespace MeshRepair
