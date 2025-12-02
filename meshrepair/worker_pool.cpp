#include "worker_pool.h"
#include "include/logger.h"
#include <algorithm>
#include <sstream>
#include <string>

namespace MeshRepair {

namespace {

    ThreadingConfig configure_thread_counts(ThreadingConfig cfg)
    {
        cfg.hw_cores = get_hardware_cores();

        if (cfg.num_threads == 0) {
            cfg.num_threads = get_default_thread_count();
        }
        if (cfg.num_threads < 1) {
            cfg.num_threads = 1;
        }

        if (cfg.num_threads == 1) {
            cfg.detection_threads = 1;
            cfg.filling_threads   = 1;
        } else {
            cfg.detection_threads = std::max<size_t>(1, cfg.num_threads / 3);
            cfg.filling_threads   = cfg.num_threads - cfg.detection_threads;
        }

        return cfg;
    }

    void print_config(const ThreadingConfig& cfg)
    {
        std::ostringstream msg;
        msg << "Threading configuration:\n";
        msg << "  Hardware cores: " << cfg.hw_cores << "\n";
        msg << "  Worker threads: " << cfg.num_threads << "\n";
        msg << "  Queue size: " << cfg.queue_size << " holes\n";
        msg << "  Pipeline split: " << cfg.detection_threads << " detection + " << cfg.filling_threads << " filling";
        logInfo(LogCategory::Fill, msg.str());
    }

}  // namespace

void
thread_manager_init(ThreadManager& mgr, const ThreadingConfig& cfg)
{
    mgr.config = configure_thread_counts(cfg);
    mgr.detection_pool.resize(1);
    mgr.filling_pool.resize(1);
    if (mgr.config.verbose) {
        print_config(mgr.config);
    }
}

void
thread_manager_enter_detection(ThreadManager& mgr)
{
    mgr.detection_pool.resize(mgr.config.num_threads);
    mgr.filling_pool.resize(0);

    if (mgr.config.verbose) {
        logInfo(LogCategory::Fill,
                "[Threading] Detection phase: " + std::to_string(mgr.config.num_threads) + " thread(s)");
    }
}

void
thread_manager_enter_pipeline(ThreadManager& mgr)
{
    mgr.detection_pool.resize(mgr.config.detection_threads);
    mgr.filling_pool.resize(mgr.config.filling_threads);

    if (mgr.config.verbose) {
        logInfo(LogCategory::Fill, "[Threading] Pipeline phase: " + std::to_string(mgr.config.detection_threads)
                                       + " detection + " + std::to_string(mgr.config.filling_threads)
                                       + " filling thread(s)");
    }
}

void
thread_manager_enter_filling(ThreadManager& mgr)
{
    mgr.detection_pool.resize(0);
    mgr.filling_pool.resize(mgr.config.num_threads);

    if (mgr.config.verbose) {
        logInfo(LogCategory::Fill,
                "[Threading] Filling phase: " + std::to_string(mgr.config.num_threads) + " thread(s)");
    }
}

size_t
get_hardware_cores()
{
    unsigned int hw = std::thread::hardware_concurrency();
    return (hw > 0) ? hw : 4;
}

size_t
get_default_thread_count()
{
    size_t hw_cores = get_hardware_cores();
    return std::max<size_t>(1, hw_cores / 2);
}

}  // namespace MeshRepair
