#pragma once

#include "mesh_loader.h"
#include "mesh_preprocessor.h"
#include "pipeline_ops.h"
#include "worker_pool.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MeshRepair {

struct RepairJobConfig {
    std::string input_path;
    std::string output_path;
    FillingOptions filling_options      = FillingOptions();
    PreprocessingOptions preprocess_opt = PreprocessingOptions();
    bool enable_preprocessing           = true;
    bool use_partitioned                = true;
    bool validate_input                 = false;
    bool ascii_ply                      = false;
    bool force_cgal_loader              = false;
    bool verbose                        = false;
    bool debug_dump                     = false;
    std::string temp_dir;
    double timeout_ms = 0.0;  // 0 = no timeout
    std::shared_ptr<std::atomic<bool>> cancel_token;
    size_t thread_count                 = 0;   // 0 = auto
    size_t queue_size                   = 10;  // hole queue size for pipeline
};

enum class RepairJobStatus : uint8_t {
    Ok = 0,
    LoadFailed,
    PreprocessFailed,
    ValidationFailed,
    ProcessFailed,
    SaveFailed,
    Cancelled,
    InternalError
};

struct RepairJobResult {
    RepairJobStatus status   = RepairJobStatus::InternalError;
    MeshStatistics stats     = MeshStatistics();
    std::string error_text;
    double total_time_ms = 0.0;
};

struct CompletedJob {
    uint64_t job_id = 0;
    RepairJobResult result;
};

struct RepairQueueConfig {
    size_t capacity       = 4;
    size_t worker_threads = 1;
};

struct RepairQueue {
    RepairQueueConfig config;
    std::vector<RepairJobConfig> job_ring;
    std::vector<uint64_t> job_ids;
    std::deque<CompletedJob> completed;
    size_t head        = 0;
    size_t tail        = 0;
    size_t count       = 0;
    bool stopping      = false;
    uint64_t next_id   = 1;
    std::mutex mtx;
    std::condition_variable cv_jobs;
    std::condition_variable cv_space;
    std::condition_variable cv_results;
    std::vector<std::thread> workers;
};

void
repair_queue_init(RepairQueue& queue, const RepairQueueConfig& config);

void
repair_queue_shutdown(RepairQueue& queue);

bool
repair_queue_enqueue(RepairQueue& queue, const RepairJobConfig& job, uint64_t* out_job_id);

bool
repair_queue_pop_result(RepairQueue& queue, CompletedJob* out_result, bool wait);

size_t
repair_queue_pending(RepairQueue& queue);

}  // namespace MeshRepair
