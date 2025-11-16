#ifndef MESHREPAIR_THREAD_MANAGER_H
#define MESHREPAIR_THREAD_MANAGER_H

#include "threadpool.h"
#include <cstddef>

namespace MeshRepair {

/**
 * @brief Threading configuration
 */
struct ThreadingConfig {
    size_t num_threads = 0;   // 0 = auto (hw_cores / 2)
    size_t queue_size  = 10;  // Pipeline queue size (number of holes buffered)
    bool verbose       = false;

    // Derived values (set by ThreadManager)
    size_t detection_threads = 0;
    size_t filling_threads   = 0;
    size_t hw_cores          = 0;
};

/**
 * @brief Thread manager for mesh repair operations
 *
 * Manages two thread pools (detection and filling) with dynamic resizing
 * based on the current processing phase.
 */
class ThreadManager {
public:
    /**
     * @brief Construct thread manager with configuration
     * @param config Threading configuration
     */
    explicit ThreadManager(const ThreadingConfig& config);

    // Get thread counts for different phases
    size_t get_detection_threads() const { return config_.detection_threads; }
    size_t get_filling_threads() const { return config_.filling_threads; }
    size_t get_total_threads() const { return config_.num_threads; }
    size_t get_queue_size() const { return config_.queue_size; }

    // Thread pool access
    ThreadPool& get_detection_pool() { return detection_pool_; }
    ThreadPool& get_filling_pool() { return filling_pool_; }

    // Reconfigure pools for different phases
    void enter_detection_phase();  // All threads for detection
    void enter_pipeline_phase();   // Split: detection + filling
    void enter_filling_phase();    // All threads for filling

private:
    ThreadingConfig config_;
    ThreadPool detection_pool_;
    ThreadPool filling_pool_;

    void configure_thread_counts();
    void print_config() const;
};

}  // namespace MeshRepair

#endif  // MESHREPAIR_THREAD_MANAGER_H
