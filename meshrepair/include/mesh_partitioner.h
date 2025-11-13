#pragma once

#include "types.h"
#include "hole_detector.h"
#include <CGAL/bounding_box.h>
#include <CGAL/Bbox_3.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace MeshRepair {

/// Information about a hole with its n-ring neighborhood
struct HoleWithNeighborhood {
    HoleInfo hole;

    // Vertices in n-ring neighborhood (for overlap detection)
    std::unordered_set<vertex_descriptor> n_ring_vertices;

    // Faces in n-ring neighborhood (for submesh extraction)
    std::unordered_set<face_descriptor> n_ring_faces;

    // Bounding box for quick overlap rejection
    CGAL::Bbox_3 bbox;

    HoleWithNeighborhood() = default;
    HoleWithNeighborhood(const HoleInfo& h) : hole(h) {}
};

/// Partitions holes into independent groups for parallel processing
class MeshPartitioner {
public:
    /// Constructor
    /// @param mesh The mesh containing holes
    /// @param continuity Fairing continuity level (0=C0, 1=C1, 2=C2)
    MeshPartitioner(const Mesh& mesh, unsigned int continuity);

    /// Compute n-ring neighborhood for a hole
    /// @param hole The hole to analyze
    /// @return Hole with neighborhood information
    HoleWithNeighborhood compute_neighborhood(const HoleInfo& hole) const;

    /// Partition holes into independent groups using greedy algorithm
    /// @param holes All holes to partition
    /// @return Vector of partitions, each partition is a vector of hole indices
    std::vector<std::vector<size_t>> partition_holes_greedy(
        const std::vector<HoleInfo>& holes) const;

    /// Partition holes with neighborhood information (more efficient if already computed)
    /// @param neighborhoods Pre-computed neighborhoods
    /// @return Vector of partitions, each partition is a vector of hole indices
    std::vector<std::vector<size_t>> partition_neighborhoods_greedy(
        const std::vector<HoleWithNeighborhood>& neighborhoods) const;

    /// Get number of rings used for neighborhood computation
    unsigned int get_ring_count() const { return n_rings_; }

private:
    const Mesh& mesh_;
    unsigned int n_rings_;  // continuity + 1

    /// Check if two hole neighborhoods have overlapping vertices
    /// @param a First hole neighborhood
    /// @param b Second hole neighborhood
    /// @return true if neighborhoods share any vertices
    bool has_overlap(const HoleWithNeighborhood& a,
                    const HoleWithNeighborhood& b) const;

    /// Collect all faces adjacent to vertices in the neighborhood
    /// @param vertices Vertices in neighborhood
    /// @return Set of faces
    std::unordered_set<face_descriptor> collect_adjacent_faces(
        const std::unordered_set<vertex_descriptor>& vertices) const;
};

} // namespace MeshRepair
