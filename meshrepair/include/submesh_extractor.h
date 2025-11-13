#pragma once

#include "types.h"
#include "hole_detector.h"
#include "mesh_partitioner.h"
#include <map>
#include <unordered_set>
#include <vector>

namespace MeshRepair {

/// Represents an extracted submesh with hole information
struct Submesh {
    /// The extracted mesh (owns the data)
    Mesh mesh;

    /// Holes in this submesh (with halfedges mapped to new mesh)
    std::vector<HoleInfo> holes;

    /// Map from original mesh vertex descriptors to submesh vertex descriptors
    std::map<vertex_descriptor, vertex_descriptor> old_to_new_vertex;

    /// Map from submesh vertex descriptors to original mesh vertex descriptors
    std::map<vertex_descriptor, vertex_descriptor> new_to_old_vertex;

    /// Statistics
    size_t original_hole_count = 0;

    Submesh() = default;

    // Move-only type
    Submesh(const Submesh&) = delete;
    Submesh& operator=(const Submesh&) = delete;
    Submesh(Submesh&&) = default;
    Submesh& operator=(Submesh&&) = default;
};

/// Extracts independent submeshes from a mesh
class SubmeshExtractor {
public:
    /// Constructor
    /// @param mesh The original mesh
    explicit SubmeshExtractor(const Mesh& mesh);

    /// Extract submesh containing specified faces
    /// @param faces Faces to include in submesh
    /// @param holes Holes whose boundaries lie in these faces
    /// @return Extracted submesh with mapped holes
    Submesh extract(
        const std::unordered_set<face_descriptor>& faces,
        const std::vector<HoleInfo>& holes) const;

    /// Extract submesh for a partition (convenience function)
    /// @param partition_indices Indices of holes in this partition
    /// @param all_holes All holes in the mesh
    /// @param neighborhoods Pre-computed neighborhoods for all holes
    /// @return Extracted submesh
    Submesh extract_partition(
        const std::vector<size_t>& partition_indices,
        const std::vector<HoleInfo>& all_holes,
        const std::vector<HoleWithNeighborhood>& neighborhoods) const;

private:
    const Mesh& mesh_;

    /// Find corresponding halfedge in new mesh by mapping vertices
    /// @param old_halfedge Halfedge in original mesh
    /// @param new_mesh The new mesh
    /// @param vertex_map Map from old to new vertex descriptors
    /// @return Corresponding halfedge in new mesh (or null if not found)
    halfedge_descriptor find_mapped_halfedge(
        halfedge_descriptor old_halfedge,
        const Mesh& new_mesh,
        const std::map<vertex_descriptor, vertex_descriptor>& vertex_map) const;
};

} // namespace MeshRepair
