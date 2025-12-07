#pragma once

#include "hole_ops.h"
#include "submesh_ops.h"
#include "worker_pool.h"
#include <atomic>
#include <chrono>
#include <vector>

namespace MeshRepair {

struct PipelineContext {
    Mesh* mesh                = nullptr;
    ThreadManager* thread_mgr = nullptr;
    FillingOptions options    = FillingOptions();
    std::atomic<bool>* cancel_flag = nullptr;
    const std::chrono::steady_clock::time_point* start_time = nullptr;
    double timeout_ms = 0.0;
};

struct ParallelPipelineCtx {
    Mesh* mesh                = nullptr;
    ThreadManager* thread_mgr = nullptr;
    FillingOptions options    = FillingOptions();
    std::atomic<bool>* cancel_flag = nullptr;
    const std::chrono::steady_clock::time_point* start_time = nullptr;
    double timeout_ms = 0.0;
};

MeshStatistics
pipeline_process_pipeline(PipelineContext* ctx, bool verbose);
MeshStatistics
pipeline_process_batch(PipelineContext* ctx, bool verbose);
MeshStatistics
parallel_fill_partitioned(ParallelPipelineCtx* ctx, bool verbose, bool debug);
struct FilledSubmesh {
    Submesh submesh;
    MeshStatistics stats;
};

FilledSubmesh
fill_submesh_holes(Submesh submesh, const FillingOptions& options);

int
process_pipeline_c(Mesh& mesh, ThreadManager& thread_manager, const FillingOptions& options, bool verbose,
                   MeshStatistics* out_stats);
int
process_batch_c(Mesh& mesh, ThreadManager& thread_manager, const FillingOptions& options, bool verbose,
                MeshStatistics* out_stats);

}  // namespace MeshRepair
