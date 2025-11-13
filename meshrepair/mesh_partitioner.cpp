#include "mesh_partitioner.h"
#include <CGAL/boost/graph/iterator.h>
#include <iostream>
#include <algorithm>

namespace MeshRepair {

MeshPartitioner::MeshPartitioner(const Mesh& mesh, unsigned int continuity)
    : mesh_(mesh)
    , n_rings_(continuity + 1)  // C0=1 ring, C1=2 rings, C2=3 rings
{}

HoleWithNeighborhood MeshPartitioner::compute_neighborhood(const HoleInfo& hole) const {
    HoleWithNeighborhood result(hole);

    // Step 1: Collect boundary vertices (ring 0)
    std::unordered_set<vertex_descriptor> current_ring;

    // Reserve approximate space (boundary size is known from hole)
    std::vector<Point_3> all_points;
    all_points.reserve(hole.boundary_size * (n_rings_ + 1));  // Rough estimate

    auto h = hole.boundary_halfedge;
    auto h_start = h;
    do {
        vertex_descriptor v = target(h, mesh_);
        current_ring.insert(v);
        result.n_ring_vertices.insert(v);
        all_points.push_back(mesh_.point(v));

        h = mesh_.next(h);
    } while (h != h_start);

    // Step 2: Expand n_rings_ times
    for (unsigned int ring = 1; ring <= n_rings_; ++ring) {
        std::unordered_set<vertex_descriptor> next_ring;

        for (vertex_descriptor v : current_ring) {
            // Iterate over all halfedges emanating from v
            for (auto he : CGAL::halfedges_around_target(v, mesh_)) {
                vertex_descriptor neighbor = source(he, mesh_);

                // Only add if not already visited
                if (result.n_ring_vertices.find(neighbor) == result.n_ring_vertices.end()) {
                    next_ring.insert(neighbor);
                    result.n_ring_vertices.insert(neighbor);
                    all_points.push_back(mesh_.point(neighbor));
                }

                // Collect faces in this neighborhood
                face_descriptor f = face(he, mesh_);
                if (f != Mesh::null_face()) {
                    result.n_ring_faces.insert(f);
                }
            }
        }

        current_ring = std::move(next_ring);
    }

    // Step 3: Also collect faces adjacent to all vertices in the final neighborhood
    // This ensures we capture the full geometry context
    result.n_ring_faces = collect_adjacent_faces(result.n_ring_vertices);

    // Step 4: Compute bounding box for quick overlap tests
    if (!all_points.empty()) {
        result.bbox = CGAL::bbox_3(all_points.begin(), all_points.end());
    }

    return result;
}

std::unordered_set<face_descriptor> MeshPartitioner::collect_adjacent_faces(
    const std::unordered_set<vertex_descriptor>& vertices) const
{
    std::unordered_set<face_descriptor> faces;

    for (vertex_descriptor v : vertices) {
        for (auto he : CGAL::halfedges_around_target(v, mesh_)) {
            face_descriptor f = face(he, mesh_);
            if (f != Mesh::null_face()) {
                faces.insert(f);
            }
        }
    }

    return faces;
}

bool MeshPartitioner::has_overlap(
    const HoleWithNeighborhood& a,
    const HoleWithNeighborhood& b) const
{
    // Quick rejection: bounding box test (cheap)
    if (!CGAL::do_overlap(a.bbox, b.bbox)) {
        return false;
    }

    // Precise test: check for shared vertices
    // Iterate over smaller set for efficiency
    const auto& smaller = a.n_ring_vertices.size() < b.n_ring_vertices.size() ?
                          a.n_ring_vertices : b.n_ring_vertices;
    const auto& larger = a.n_ring_vertices.size() < b.n_ring_vertices.size() ?
                         b.n_ring_vertices : a.n_ring_vertices;

    for (vertex_descriptor v : smaller) {
        if (larger.find(v) != larger.end()) {
            return true;  // Found shared vertex
        }
    }

    return false;
}

std::vector<std::vector<size_t>> MeshPartitioner::partition_holes_greedy(
    const std::vector<HoleInfo>& holes) const
{
    // First, compute neighborhoods for all holes
    // Pre-allocate to enable indexed access (ready for parallel for_each)
    std::vector<HoleWithNeighborhood> neighborhoods(holes.size());

    // TODO: Can be parallelized with OpenMP: #pragma omp parallel for
    for (size_t i = 0; i < holes.size(); ++i) {
        neighborhoods[i] = compute_neighborhood(holes[i]);
    }

    // Then partition using the neighborhoods
    return partition_neighborhoods_greedy(neighborhoods);
}

std::vector<std::vector<size_t>> MeshPartitioner::partition_neighborhoods_greedy(
    const std::vector<HoleWithNeighborhood>& neighborhoods) const
{
    if (neighborhoods.empty()) {
        return {};
    }

    // Greedy partitioning: assign each hole to first available partition
    std::vector<std::vector<size_t>> partitions;
    std::vector<std::unordered_set<vertex_descriptor>> partition_vertices;

    for (size_t i = 0; i < neighborhoods.size(); ++i) {
        bool assigned = false;

        // Try to add to existing partition
        for (size_t p = 0; p < partitions.size(); ++p) {
            // Check if this hole's vertices conflict with partition p's vertices
            bool conflicts = false;

            for (vertex_descriptor v : neighborhoods[i].n_ring_vertices) {
                if (partition_vertices[p].find(v) != partition_vertices[p].end()) {
                    conflicts = true;
                    break;
                }
            }

            if (!conflicts) {
                // Add hole to this partition
                partitions[p].push_back(i);

                // Add all vertices from this hole's neighborhood to the partition
                partition_vertices[p].insert(
                    neighborhoods[i].n_ring_vertices.begin(),
                    neighborhoods[i].n_ring_vertices.end());

                assigned = true;
                break;
            }
        }

        // Create new partition if couldn't fit in existing ones
        if (!assigned) {
            partitions.push_back({i});
            partition_vertices.push_back(neighborhoods[i].n_ring_vertices);
        }
    }

    return partitions;
}

} // namespace MeshRepair
