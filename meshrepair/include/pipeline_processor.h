#ifndef MESHREPAIR_PIPELINE_PROCESSOR_H
#define MESHREPAIR_PIPELINE_PROCESSOR_H

#include "types.h"
#include "hole_detector.h"
#include "hole_filler.h"
#include "thread_manager.h"
#include "threadpool.h"
#include <vector>
#include <atomic>
#include <mutex>

namespace MeshRepair {

/**
 * @brief Pipeline processor for parallel hole detection and filling
 *
 * Implements producer-consumer pattern:
 * - Detection threads find holes and push to queue
 * - Filling threads pop from queue and fill holes
 * - Overlap maximizes CPU utilization
 */
class PipelineProcessor {
public:
    PipelineProcessor(
        Mesh& mesh,
        ThreadManager& thread_manager,
        const FillingOptions& filling_options);

    /**
     * @brief Pipeline processing: detect and fill holes simultaneously
     * @param verbose Enable verbose output
     * @return Mesh statistics
     */
    MeshStatistics process_pipeline(bool verbose);

    /**
     * @brief Batch processing: detect all holes first, then fill all
     * @param verbose Enable verbose output
     * @return Mesh statistics
     */
    MeshStatistics process_batch(bool verbose);

private:
    Mesh& mesh_;
    ThreadManager& thread_manager_;
    FillingOptions filling_options_;

    // Detection phase (producer)
    void detect_holes_async(
        BoundedQueue<HoleInfo>& hole_queue,
        std::atomic<bool>& detection_done,
        std::atomic<size_t>& holes_detected);

    // Filling phase (consumer)
    void fill_holes_async(
        BoundedQueue<HoleInfo>& hole_queue,
        std::atomic<bool>& detection_done,
        std::vector<HoleStatistics>& results,
        std::mutex& results_mutex);
};

} // namespace MeshRepair

#endif // MESHREPAIR_PIPELINE_PROCESSOR_H
