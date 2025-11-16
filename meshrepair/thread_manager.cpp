#include "thread_manager.h"
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#    include <windows.h>
#elif defined(__linux__)
#    include <pthread.h>
#    include <sched.h>
#elif defined(__APPLE__)
#    include <pthread.h>
#    include <mach/thread_policy.h>
#endif

namespace MeshRepair {

ThreadManager::ThreadManager(const ThreadingConfig& config)
    : config_(config)
    , detection_pool_(1, 1000)  // Start with 1 thread, will resize
    , filling_pool_(1, 1000)    // Start with 1 thread, will resize
{
    configure_thread_counts();

    if (config_.verbose) {
        print_config();
    }
}

void
ThreadManager::configure_thread_counts()
{
    config_.hw_cores = get_hardware_cores();

    // Set total threads
    if (config_.num_threads == 0) {
        config_.num_threads = get_default_thread_count();
    }

    // Validate thread count
    if (config_.num_threads < 1) {
        config_.num_threads = 1;
    }

    // For pipeline phase: split threads between detection and filling
    // Detection is typically faster, so give it fewer threads (33%)
    // Filling is slower, so give it more threads (67%)
    if (config_.num_threads == 1) {
        // Special case: single-threaded mode
        config_.detection_threads = 1;
        config_.filling_threads   = 1;
    } else {
        config_.detection_threads = std::max<size_t>(1, config_.num_threads / 3);     // 33%
        config_.filling_threads   = config_.num_threads - config_.detection_threads;  // 67%
    }
}

void
ThreadManager::print_config() const
{
    std::cout << "Threading configuration:\n";
    std::cout << "  Hardware cores: " << config_.hw_cores << "\n";
    std::cout << "  Worker threads: " << config_.num_threads << "\n";
    std::cout << "  Queue size: " << config_.queue_size << " holes\n";
    std::cout << "  Pipeline split: " << config_.detection_threads << " detection + " << config_.filling_threads
              << " filling\n\n";
}

void
ThreadManager::enter_detection_phase()
{
    // All threads for detection, none for filling
    detection_pool_.resize(config_.num_threads);
    filling_pool_.resize(0);

    if (config_.verbose) {
        std::cout << "[Threading] Detection phase: " << config_.num_threads << " thread(s)\n";
    }
}

void
ThreadManager::enter_pipeline_phase()
{
    // Split threads between detection and filling
    detection_pool_.resize(config_.detection_threads);
    filling_pool_.resize(config_.filling_threads);

    if (config_.verbose) {
        std::cout << "[Threading] Pipeline phase: " << config_.detection_threads << " detection + "
                  << config_.filling_threads << " filling thread(s)\n";
    }
}

void
ThreadManager::enter_filling_phase()
{
    // All threads for filling, none for detection
    detection_pool_.resize(0);
    filling_pool_.resize(config_.num_threads);

    if (config_.verbose) {
        std::cout << "[Threading] Filling phase: " << config_.num_threads << " thread(s)\n";
    }
}

}  // namespace MeshRepair
