#ifndef MESHREPAIR_PARALLEL_DETECTION_H
#define MESHREPAIR_PARALLEL_DETECTION_H

#include "types.h"
#include "threadpool.h"
#include "parallel_utils.h"
#include <vector>
#include <future>

namespace MeshRepair {

/**
 * @brief Find border halfedges in parallel
 * @param mesh Mesh to check
 * @param pool Thread pool for parallel execution
 * @param verbose Enable verbose output
 * @return Vector of border halfedges
 */
inline std::vector<halfedge_descriptor>
find_border_halfedges_parallel(const Mesh& mesh, ThreadPool& pool, bool verbose = false) {
    if (verbose) {
        std::cout << "  [Parallel] Finding border halfedges using "
                  << pool.threadCount() << " thread(s)...\n";
    }

    // Partition halfedges
    auto partitions = partition_halfedges(mesh, pool.threadCount());

    if (partitions.empty()) {
        return std::vector<halfedge_descriptor>();
    }

    // Launch parallel search tasks
    std::vector<std::future<std::vector<halfedge_descriptor>>> futures;

    for (const auto& partition : partitions) {
        futures.push_back(pool.enqueue([&mesh, partition]() {
            std::vector<halfedge_descriptor> local_borders;

            // Check each halfedge in this partition
            for (auto h : partition.descriptors) {
                if (mesh.is_border(h)) {
                    local_borders.push_back(h);
                }
            }

            return local_borders;
        }));
    }

    // Gather results
    std::vector<halfedge_descriptor> all_borders;
    for (auto& future : futures) {
        auto local = future.get();
        all_borders.insert(all_borders.end(), local.begin(), local.end());
    }

    if (verbose) {
        std::cout << "  [Parallel] Found " << all_borders.size()
                  << " border halfedges\n";
    }

    return all_borders;
}

} // namespace MeshRepair

#endif // MESHREPAIR_PARALLEL_DETECTION_H
