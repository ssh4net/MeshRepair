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

std::vector<std::vector<size_t>> MeshPartitioner::partition_holes_by_count(
    const std::vector<HoleInfo>& holes,
    size_t num_partitions) const
{
    if (holes.empty()) {
        return {};
    }

    if (num_partitions == 0) {
        num_partitions = 1;
    }

    // Don't create more partitions than holes
    if (num_partitions > holes.size()) {
        num_partitions = holes.size();
    }

    // Simple count-based partitioning: divide holes evenly across partitions
    std::vector<std::vector<size_t>> partitions(num_partitions);

    size_t holes_per_partition = (holes.size() + num_partitions - 1) / num_partitions;

    // Distribute holes evenly
    for (size_t i = 0; i < holes.size(); ++i) {
        size_t partition_idx = i / holes_per_partition;

        // Safety check: ensure we don't exceed partition count
        if (partition_idx >= num_partitions) {
            partition_idx = num_partitions - 1;
        }

        partitions[partition_idx].push_back(i);
    }

    return partitions;
}

} // namespace MeshRepair
