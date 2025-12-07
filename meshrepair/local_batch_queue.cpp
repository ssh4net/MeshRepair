#include "local_batch_queue.h"
#include "mesh_validator.h"
#include "debug_path.h"
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <chrono>
#include <memory>

namespace MeshRepair {

namespace PMP = CGAL::Polygon_mesh_processing;

namespace {

bool
should_abort(const std::shared_ptr<std::atomic<bool>>& flag,
             const std::chrono::steady_clock::time_point& start_time,
             double timeout_ms)
{
    if (flag && flag->load(std::memory_order_relaxed)) {
        return true;
    }
    if (timeout_ms > 0.0) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - start_time).count();
        if (elapsed > timeout_ms) {
            return true;
        }
    }
    return false;
}

RepairJobResult
run_single_job(const RepairJobConfig& job)
{
    RepairJobResult result;
    result.status = RepairJobStatus::Ok;
    auto start_time = std::chrono::steady_clock::now();

    try {
        PolygonSoup soup;
        Mesh mesh;
        std::shared_ptr<std::atomic<bool>> cancel_flag = job.cancel_token;
        if (!cancel_flag) {
            cancel_flag = std::make_shared<std::atomic<bool>>(false);
        }

        if (!job.temp_dir.empty()) {
            DebugPath::set_base_directory(job.temp_dir);
        }

        if (mesh_loader_load_soup(job.input_path.c_str(), MeshLoader::Format::AUTO, job.force_cgal_loader, &soup)
            != 0) {
            result.status     = RepairJobStatus::LoadFailed;
            result.error_text = "Load failed (" + job.input_path + "): " + std::string(mesh_loader_last_error());
        } else {
            if (should_abort(cancel_flag, start_time, job.timeout_ms)) {
                result.status = RepairJobStatus::Cancelled;
            }

            if (job.enable_preprocessing) {
                PreprocessingOptions prep_opts = job.preprocess_opt;
                prep_opts.verbose             = prep_opts.verbose || job.verbose;
                prep_opts.debug               = prep_opts.debug || job.debug_dump;

                PreprocessingStats prep_stats;
                if (preprocess_soup_c(&soup, &mesh, &prep_opts, &prep_stats) != 0) {
                    result.status     = RepairJobStatus::PreprocessFailed;
                    result.error_text = "Preprocess failed for " + job.input_path;
                }
            } else {
                PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, mesh);
            }

            if (result.status == RepairJobStatus::Ok) {
                if (job.validate_input) {
                    if (!MeshValidator::is_triangle_mesh(mesh)) {
                        result.status     = RepairJobStatus::ValidationFailed;
                        result.error_text = "Mesh is not a triangle mesh";
                    } else if (!MeshValidator::is_valid(mesh)) {
                        result.status     = RepairJobStatus::ValidationFailed;
                        result.error_text = "Mesh failed validity checks";
                    }
                }
            }

            if (result.status == RepairJobStatus::Ok && should_abort(cancel_flag, start_time, job.timeout_ms)) {
                result.status = RepairJobStatus::Cancelled;
            }

            if (result.status == RepairJobStatus::Ok) {
                ThreadingConfig thread_cfg;
                thread_cfg.num_threads = job.thread_count;
                thread_cfg.queue_size  = job.queue_size;
                thread_cfg.verbose     = job.verbose;

                ThreadManager mgr;
                thread_manager_init(mgr, thread_cfg);

                FillingOptions filling_opts = job.filling_options;
                if (!job.use_partitioned && filling_opts.holes_only) {
                    filling_opts.holes_only = false;
                }

                MeshStatistics stats;
                if (job.use_partitioned) {
                    ParallelPipelineCtx ctx;
                    ctx.mesh       = &mesh;
                    ctx.thread_mgr = &mgr;
                    ctx.options    = filling_opts;
                    ctx.cancel_flag = cancel_flag.get();
                    ctx.start_time  = &start_time;
                    ctx.timeout_ms  = job.timeout_ms;
                    stats           = parallel_fill_partitioned(&ctx, job.verbose, job.debug_dump);
                } else {
                    PipelineContext ctx;
                    ctx.mesh       = &mesh;
                    ctx.thread_mgr = &mgr;
                    ctx.options    = filling_opts;
                    ctx.cancel_flag = cancel_flag.get();
                    ctx.start_time  = &start_time;
                    ctx.timeout_ms  = job.timeout_ms;

                    if (mgr.config.num_threads > 1) {
                        stats = pipeline_process_pipeline(&ctx, job.verbose);
                    } else {
                        stats = pipeline_process_batch(&ctx, job.verbose);
                    }
                }

                if (should_abort(cancel_flag, start_time, job.timeout_ms)) {
                    result.status     = RepairJobStatus::Cancelled;
                    result.error_text = "Cancelled";
                }

                bool use_binary_ply = !job.ascii_ply;
                if (result.status == RepairJobStatus::Ok
                    && mesh_loader_save(mesh, job.output_path.c_str(), MeshLoader::Format::AUTO, use_binary_ply) != 0) {
                    result.status     = RepairJobStatus::SaveFailed;
                    result.error_text
                        = "Save failed (" + job.output_path + "): " + std::string(mesh_loader_last_error());
                } else {
                    result.status = RepairJobStatus::Ok;
                    result.stats  = stats;
                }
            }
        }
    } catch (const std::exception& e) {
        result.status     = RepairJobStatus::InternalError;
        result.error_text = e.what();
    } catch (...) {
        result.status     = RepairJobStatus::InternalError;
        result.error_text = "Unknown exception";
    }

    auto end_time         = std::chrono::steady_clock::now();
    result.total_time_ms  = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

void
worker_loop(RepairQueue* queue)
{
    while (true) {
        RepairJobConfig job;
        uint64_t job_id = 0;

        {
            std::unique_lock<std::mutex> lock(queue->mtx);
            queue->cv_jobs.wait(lock, [queue] { return queue->stopping || queue->count > 0; });

            if (queue->stopping && queue->count == 0) {
                return;
            }

            size_t idx = queue->head;
            job        = queue->job_ring[idx];
            job_id     = queue->job_ids[idx];
            queue->head = (idx + 1) % queue->job_ring.size();
            queue->count--;
            queue->cv_space.notify_one();
        }

        CompletedJob completed;
        completed.job_id = job_id;
        completed.result = run_single_job(job);

        {
            std::lock_guard<std::mutex> lock(queue->mtx);
            queue->completed.push_back(completed);
        }
        queue->cv_results.notify_one();
    }
}

}  // namespace

void
repair_queue_init(RepairQueue& queue, const RepairQueueConfig& config)
{
    queue.config = config;
    if (queue.config.capacity == 0) {
        queue.config.capacity = 1;
    }
    if (queue.config.worker_threads == 0) {
        queue.config.worker_threads = 1;
    }

    queue.job_ring.assign(queue.config.capacity, RepairJobConfig());
    queue.job_ids.assign(queue.config.capacity, 0);
    queue.completed.clear();
    queue.head      = 0;
    queue.tail      = 0;
    queue.count     = 0;
    queue.stopping  = false;
    queue.next_id   = 1;

    queue.workers.clear();
    queue.workers.reserve(queue.config.worker_threads);
    for (size_t i = 0; i < queue.config.worker_threads; ++i) {
        queue.workers.emplace_back(worker_loop, &queue);
    }
}

void
repair_queue_shutdown(RepairQueue& queue)
{
    {
        std::lock_guard<std::mutex> lock(queue.mtx);
        queue.stopping = true;
    }
    queue.cv_jobs.notify_all();
    queue.cv_space.notify_all();
    queue.cv_results.notify_all();

    for (auto& worker : queue.workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    queue.workers.clear();
}

bool
repair_queue_enqueue(RepairQueue& queue, const RepairJobConfig& job, uint64_t* out_job_id)
{
    std::unique_lock<std::mutex> lock(queue.mtx);
    if (queue.stopping || queue.count >= queue.job_ring.size()) {
        return false;
    }

    size_t idx     = queue.tail;
    uint64_t job_id = queue.next_id++;

    queue.job_ring[idx] = job;
    queue.job_ids[idx]  = job_id;

    queue.tail = (idx + 1) % queue.job_ring.size();
    queue.count++;

    if (out_job_id) {
        *out_job_id = job_id;
    }

    queue.cv_jobs.notify_one();
    return true;
}

bool
repair_queue_pop_result(RepairQueue& queue, CompletedJob* out_result, bool wait)
{
    std::unique_lock<std::mutex> lock(queue.mtx);
    if (wait) {
        queue.cv_results.wait(lock, [&queue] { return queue.stopping || !queue.completed.empty(); });
    }

    if (queue.completed.empty()) {
        return false;
    }

    if (out_result) {
        *out_result = queue.completed.front();
    }
    queue.completed.pop_front();
    queue.cv_space.notify_one();
    return true;
}

size_t
repair_queue_pending(RepairQueue& queue)
{
    std::lock_guard<std::mutex> lock(queue.mtx);
    return queue.count;
}

}  // namespace MeshRepair
