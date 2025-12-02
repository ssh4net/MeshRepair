#pragma once

#include "hole_ops.h"
#include "types.h"
#include <CGAL/Bbox_3.h>
#include <unordered_set>
#include <vector>

namespace MeshRepair {

struct HoleWithNeighborhood {
    HoleInfo hole;
    std::unordered_set<vertex_descriptor> n_ring_vertices;
    std::unordered_set<face_descriptor> n_ring_faces;
    CGAL::Bbox_3 bbox;
};

struct MeshPartitionerCtx {
    const Mesh* mesh     = nullptr;
    unsigned int n_rings = 1;  // continuity + 1
};

HoleWithNeighborhood
partition_compute_neighborhood(const MeshPartitionerCtx& ctx, const HoleInfo& hole);
std::vector<std::vector<size_t>>
partition_holes_by_count(const std::vector<HoleInfo>& holes, size_t num_partitions);
unsigned int
partition_ring_count(const MeshPartitionerCtx& ctx);

struct Submesh {
    Mesh mesh;
    std::vector<HoleInfo> holes;
    std::map<vertex_descriptor, vertex_descriptor> old_to_new_vertex;
    std::map<vertex_descriptor, vertex_descriptor> new_to_old_vertex;
    size_t original_hole_count = 0;
};

struct SubmeshExtractorCtx {
    const Mesh* mesh = nullptr;
};

struct MergeTiming {
    double dedup_ms     = 0.0;
    double copy_base_ms = 0.0;
    double append_ms    = 0.0;
    double repair_ms    = 0.0;
    double orient_ms    = 0.0;
    double convert_ms   = 0.0;
    double total_ms     = 0.0;

    // Validation stats
    size_t validation_removed          = 0;
    size_t validation_out_of_bounds    = 0;
    size_t validation_invalid_cycle    = 0;
    size_t validation_edge_orientation = 0;
    size_t validation_non_manifold     = 0;
    size_t validation_passes           = 0;
};

Submesh
submesh_extract(const SubmeshExtractorCtx& ctx, const std::unordered_set<face_descriptor>& faces,
                const std::vector<HoleInfo>& holes);
Submesh
submesh_extract_partition(const SubmeshExtractorCtx& ctx, const std::vector<size_t>& partition_indices,
                          const std::vector<HoleInfo>& all_holes,
                          const std::vector<HoleWithNeighborhood>& neighborhoods);

Mesh
mesh_merger_merge(const Mesh& original_mesh, const std::vector<Submesh>& submeshes, bool verbose = false,
                  bool holes_only = false, bool debug_dump = false, MergeTiming* timings = nullptr);

}  // namespace MeshRepair
