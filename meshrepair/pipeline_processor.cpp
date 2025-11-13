#include "pipeline_processor.h"
#include "parallel_detection.h"
#include "thread_safe_cout.h"
#include <iostream>
#include <chrono>
#include <unordered_set>

namespace MeshRepair {

PipelineProcessor::PipelineProcessor(
    Mesh& mesh,
    ThreadManager& thread_manager,
    const FillingOptions& filling_options)
    : mesh_(mesh)
    , thread_manager_(thread_manager)
    , filling_options_(filling_options)
{}

MeshStatistics PipelineProcessor::process_pipeline(bool verbose) {
    auto start_time = std::chrono::high_resolution_clock::now();

    MeshStatistics stats;
    stats.original_vertices = mesh_.number_of_vertices();
    stats.original_faces = mesh_.number_of_faces();

    // Enter pipeline phase: split threads between detection and filling
    thread_manager_.enter_pipeline_phase();

    // Create bounded queue for pipeline
    size_t queue_memory = thread_manager_.get_queue_size() * sizeof(HoleInfo) * 100; // Rough estimate
    BoundedQueue<HoleInfo> hole_queue(queue_memory);

    // Shared state
    std::atomic<bool> detection_done{false};
    std::atomic<size_t> holes_detected{0};
    std::vector<HoleStatistics> results;
    std::mutex results_mutex;

    if (verbose) {
        std::cout << "[Pipeline] Starting detection and filling in parallel\n";
        std::cout << "[Pipeline] Detection: " << thread_manager_.get_detection_threads() << " thread(s)\n";
        std::cout << "[Pipeline] Filling: " << thread_manager_.get_filling_threads() << " thread(s)\n";
        std::cout << "[Pipeline] Queue size: " << thread_manager_.get_queue_size() << " holes\n\n";
    }

    // Start detection in background (producer)
    auto& detection_pool = thread_manager_.get_detection_pool();

    if (verbose) {
        std::cout << "[Pipeline] Enqueueing detection task...\n";
    }

    auto detection_future = detection_pool.enqueue(
        [this, &hole_queue, &detection_done, &holes_detected, verbose]() {
            if (verbose) {
                thread_safe_cout() << "[Pipeline] Detection thread started\n";
            }
            detect_holes_async(hole_queue, detection_done, holes_detected);
            if (verbose) {
                thread_safe_cout() << "[Pipeline] Detection thread finished\n";
            }
        }
    );

    if (verbose) {
        std::cout << "[Pipeline] Detection task enqueued\n";
    }

    // Start filling threads (consumers)
    auto& filling_pool = thread_manager_.get_filling_pool();
    std::vector<std::future<void>> filling_futures;

    if (verbose) {
        std::cout << "[Pipeline] Enqueueing " << thread_manager_.get_filling_threads() << " filling tasks...\n";
    }

    for (size_t i = 0; i < thread_manager_.get_filling_threads(); ++i) {
        if (verbose) {
            std::cout << "[Pipeline] Enqueueing filling task " << i << "...\n";
        }

        filling_futures.push_back(
            filling_pool.enqueue(
                [this, &hole_queue, &detection_done, &results, &results_mutex, i, verbose]() {
                    if (verbose) {
                        thread_safe_cout() << "[Pipeline] Filling thread " << i << " started\n";
                    }
                    fill_holes_async(hole_queue, detection_done, results, results_mutex);
                    if (verbose) {
                        thread_safe_cout() << "[Pipeline] Filling thread " << i << " finished\n";
                    }
                }
            )
        );

        if (verbose) {
            std::cout << "[Pipeline] Filling task " << i << " enqueued\n";
        }
    }

    if (verbose) {
        std::cout << "[Pipeline] All tasks enqueued\n";
    }

    // Wait for detection to complete
    if (verbose) {
        std::cout << "[Pipeline] Waiting for detection thread to complete...\n";
    }

    try {
        detection_future.get();

        if (verbose) {
            std::cout << "[Pipeline] Detection thread completed successfully\n";
        }
    } catch (const std::exception& e) {
        std::cout << "[Pipeline] ERROR: Detection thread threw exception: " << e.what() << "\n";
        // Signal filling threads to stop
        hole_queue.finish();
        // Wait for filling threads to exit
        for (auto& f : filling_futures) {
            try { f.get(); } catch (...) {}
        }
        throw;  // Re-throw to caller
    } catch (...) {
        std::cout << "[Pipeline] ERROR: Detection thread threw unknown exception\n";
        hole_queue.finish();
        for (auto& f : filling_futures) {
            try { f.get(); } catch (...) {}
        }
        throw;
    }

    // Signal filling threads that no more holes are coming
    if (verbose) {
        std::cout << "[Pipeline] About to call hole_queue.finish()...\n";
    }

    hole_queue.finish();

    if (verbose) {
        std::cout << "[Pipeline] hole_queue.finish() completed, waiting for filling to finish...\n";
    }

    // Wait for all filling to complete
    if (verbose) {
        std::cout << "[Pipeline] Waiting for " << filling_futures.size() << " filling thread(s) to complete...\n";
    }

    for (size_t i = 0; i < filling_futures.size(); ++i) {
        try {
            if (verbose) {
                std::cout << "[Pipeline] Waiting for filling thread " << i << "...\n";
            }
            filling_futures[i].get();
            if (verbose) {
                std::cout << "[Pipeline] Filling thread " << i << " completed\n";
            }
        } catch (const std::exception& e) {
            std::cout << "[Pipeline] ERROR: Filling thread " << i << " threw exception: "
                      << e.what() << "\n";
        } catch (...) {
            std::cout << "[Pipeline] ERROR: Filling thread " << i << " threw unknown exception\n";
        }
    }

    if (verbose) {
        std::cout << "[Pipeline] All filling threads completed\n";
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    // Build statistics
    stats.num_holes_detected = holes_detected.load();
    stats.total_time_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time).count();

    // Aggregate results
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

    stats.final_vertices = mesh_.number_of_vertices();
    stats.final_faces = mesh_.number_of_faces();

    return stats;
}

void PipelineProcessor::detect_holes_async(
    BoundedQueue<HoleInfo>& hole_queue,
    std::atomic<bool>& detection_done,
    std::atomic<size_t>& holes_detected)
{
    // Use parallel border detection if multiple threads available
    std::vector<halfedge_descriptor> border_halfedges;

    auto& pool = thread_manager_.get_detection_pool();
    if (pool.threadCount() > 1) {
        border_halfedges = find_border_halfedges_parallel(
            mesh_, pool, filling_options_.verbose);
    } else {
        // Sequential fallback
        for (auto h : mesh_.halfedges()) {
            if (mesh_.is_border(h)) {
                border_halfedges.push_back(h);
            }
        }

        if (filling_options_.verbose) {
            std::cout << "  [Detection] Found " << border_halfedges.size()
                      << " border halfedges\n";
        }
    }

    // Group into hole cycles (sequential - complex topology)
    std::unordered_set<halfedge_descriptor> processed;

    for (auto h : border_halfedges) {
        if (processed.find(h) != processed.end()) {
            continue;
        }

        // Found new hole - analyze it
        HoleInfo hole = HoleDetector::analyze_hole(mesh_, h);

        // Mark all halfedges in this cycle as processed
        auto h_current = h;
        do {
            processed.insert(h_current);
            h_current = mesh_.next(h_current);
        } while (h_current != h);

        // Push to queue (blocks if queue is full - provides backpressure)
        hole_queue.push(hole);
        size_t count = holes_detected.fetch_add(1) + 1;

        if (filling_options_.verbose) {
            std::cout << "  [Pipeline] Hole " << count
                      << " detected (" << hole.boundary_size << " vertices), queued for filling\n";
        }
    }

    detection_done.store(true);

    if (filling_options_.verbose) {
        std::cout << "  [Pipeline] Detection complete: " << holes_detected.load() << " hole(s) found\n";
    }
}

void PipelineProcessor::fill_holes_async(
    BoundedQueue<HoleInfo>& hole_queue,
    std::atomic<bool>& detection_done,
    std::vector<HoleStatistics>& results,
    std::mutex& results_mutex)
{
    // Suppress unused parameter warning - detection_done was used for debug output
    (void)detection_done;

    // Note: CGAL mesh is NOT thread-safe for concurrent writes!
    // Strategy: All threads can READ mesh concurrently
    // But WRITE operations must be serialized

    // Since CGAL's triangulate_refine_and_fair_hole does BOTH computation AND writing,
    // we must serialize the entire operation per hole.
    // Future optimization: separate computation from application.

    static std::mutex mesh_modification_mutex;

    // CRITICAL: Also need mutex for console output to prevent race conditions
    // Windows console I/O is particularly sensitive to multi-threaded writes
    static std::mutex console_mutex;

    while (true) {
        HoleInfo hole;

        // Try to get a hole from the queue
        if (!hole_queue.pop(hole)) {
            // Queue is finished and empty
            break;
        }

        // Fill the hole (with mesh modification lock)
        // TODO: This serializes everything - no real parallelism yet
        // To truly parallelize, we need to:
        // 1. Compute triangulation without modifying mesh (parallel-safe)
        // 2. Apply triangulation result (sequential, but fast)
        HoleStatistics stats;

        // CRITICAL: Use try-catch to ensure mutex is ALWAYS released even if CGAL crashes
        try {
            std::unique_lock<std::mutex> lock(mesh_modification_mutex);

            // Temporarily disable verbose output during parallel filling
            // to avoid console I/O race conditions
            FillingOptions non_verbose_opts = filling_options_;
            non_verbose_opts.verbose = false;

            // Create filler for this thread
            HoleFiller filler(mesh_, non_verbose_opts);

            stats = filler.fill_hole(hole);

            // Explicitly unlock before exception handling
            lock.unlock();
        } catch (const std::exception& e) {
            // CGAL threw an exception - mesh might be corrupted, but at least unlock
            thread_safe_cerr() << "  [ERROR] Exception during fill_hole: " << e.what() << "\n";
            stats.filled_successfully = false;
            stats.error_message = std::string("Exception: ") + e.what();
        } catch (...) {
            // Unknown exception - likely CGAL internal error
            thread_safe_cerr() << "  [ERROR] Unknown exception during fill_hole\n";
            stats.filled_successfully = false;
            stats.error_message = "Unknown exception during hole filling";
        }

        // Store result (and optionally print with synchronized console access)
        {
            std::lock_guard<std::mutex> lock(results_mutex);

            results.push_back(stats);

            // If verbose mode is enabled, print result with thread-safe output
            if (filling_options_.verbose) {
                if (stats.filled_successfully) {
                    thread_safe_cout() << "  [Pipeline] Hole filled: "
                                       << stats.num_faces_added << " faces, "
                                       << stats.num_vertices_added << " vertices added"
                                       << (stats.fairing_succeeded ? "" : " [FAIRING FAILED]")
                                       << "\n";
                } else {
                    // Failure - show error message
                    if (!stats.error_message.empty()) {
                        thread_safe_cout() << "  [Pipeline] Hole filling FAILED: " << stats.error_message << "\n";
                    } else {
                        thread_safe_cout() << "  [Pipeline] Hole filling FAILED\n";
                    }
                }
            }
        }
    }
}

MeshStatistics PipelineProcessor::process_batch(bool verbose) {
    // Traditional approach: detect all holes first, then fill
    if (verbose) {
        std::cout << "[Batch] Detecting all holes first...\n";
    }

    HoleDetector detector(mesh_, verbose);
    auto holes = detector.detect_all_holes();

    if (holes.empty()) {
        MeshStatistics stats;
        stats.original_vertices = mesh_.number_of_vertices();
        stats.original_faces = mesh_.number_of_faces();
        stats.final_vertices = stats.original_vertices;
        stats.final_faces = stats.original_faces;
        return stats;
    }

    if (verbose) {
        std::cout << "[Batch] Filling " << holes.size() << " hole(s)...\n";
    }

    // Use all threads for filling
    thread_manager_.enter_filling_phase();

    HoleFiller filler(mesh_, filling_options_);
    return filler.fill_all_holes(holes);
}

} // namespace MeshRepair
