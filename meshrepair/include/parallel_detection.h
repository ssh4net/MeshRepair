#ifndef MESHREPAIR_PARALLEL_DETECTION_H
#define MESHREPAIR_PARALLEL_DETECTION_H

#include "types.h"
#include "worker_pool.h"
#include "parallel_utils.h"
#include "logger.h"
#include <vector>
#include <future>
#include <string>

namespace MeshRepair {

/**
 * @brief Find border halfedges in parallel
 * @param mesh Mesh to check
 * @param pool Thread pool for parallel execution
 * @param verbose Enable verbose output
 * @return Vector of border halfedges
 */
inline std::vector<halfedge_descriptor>
find_border_halfedges_parallel(const Mesh& mesh, ThreadPool& /*pool*/, bool verbose = false)
{
    // Simplified: sequential scan (fire-and-forget pool not used to avoid futures)
    if (verbose) {
        logInfo(LogCategory::Fill, "[Parallel] Finding border halfedges (sequential fallback)");
    }
    std::vector<halfedge_descriptor> all_borders;
    all_borders.reserve(mesh.number_of_halfedges());
    for (auto h : mesh.halfedges()) {
        if (mesh.is_border(h)) {
            all_borders.push_back(h);
        }
    }
    if (verbose) {
        logInfo(LogCategory::Fill, "[Parallel] Found " + std::to_string(all_borders.size()) + " border halfedges");
    }
    return all_borders;
}

}  // namespace MeshRepair

#endif  // MESHREPAIR_PARALLEL_DETECTION_H
