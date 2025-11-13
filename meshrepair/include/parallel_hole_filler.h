#pragma once

#include "types.h"
#include "config.h"
#include "hole_detector.h"
#include "hole_filler.h"
#include "mesh_partitioner.h"
#include "submesh_extractor.h"
#include "mesh_merger.h"
#include "thread_manager.h"
#include <vector>

namespace MeshRepair {

/// Orchestrates partitioned parallel hole filling
class ParallelHoleFillerPipeline {
public:
    /// Constructor
    /// @param mesh The mesh to process (will be modified in place)
    /// @param thread_manager Thread pool manager
    /// @param filling_options Options for hole filling
    ParallelHoleFillerPipeline(
        Mesh& mesh,
        ThreadManager& thread_manager,
        const FillingOptions& filling_options);

    /// Process mesh using partitioned parallel approach
    /// @param verbose Print progress information
    /// @param debug Dump intermediate meshes as PLY files
    /// @return Statistics about the processing
    MeshStatistics process_partitioned(bool verbose, bool debug = false);

private:
    Mesh& mesh_;
    ThreadManager& thread_manager_;
    FillingOptions filling_options_;

    /// Fill holes in a single submesh (called in parallel)
    /// @param submesh The submesh to process (moved in, moved out)
    /// @return Processed submesh with holes filled
    static Submesh fill_submesh_holes(
        Submesh submesh,
        const FillingOptions& options);
};

} // namespace MeshRepair
