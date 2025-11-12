#ifndef MESHREPAIR_PARALLEL_UTILS_H
#define MESHREPAIR_PARALLEL_UTILS_H

#include "types.h"
#include <vector>
#include <iterator>
#include <algorithm>

namespace MeshRepair {

/**
 * @brief Partition description for parallel processing
 */
template<typename DescriptorType>
struct MeshPartition {
    std::vector<DescriptorType> descriptors;
    size_t start_idx;
    size_t count;

    MeshPartition() : start_idx(0), count(0) {}
};

/**
 * @brief Partition vertices for parallel processing
 * @param mesh Mesh to partition
 * @param num_partitions Number of partitions to create
 * @return Vector of vertex partitions
 */
inline std::vector<MeshPartition<vertex_descriptor>>
partition_vertices(const Mesh& mesh, size_t num_partitions) {
    std::vector<MeshPartition<vertex_descriptor>> partitions;

    if (num_partitions == 0) {
        num_partitions = 1;
    }

    // Collect all vertices
    std::vector<vertex_descriptor> all_vertices;
    all_vertices.reserve(mesh.number_of_vertices());
    for (auto v : mesh.vertices()) {
        all_vertices.push_back(v);
    }

    size_t total = all_vertices.size();
    if (total == 0) {
        return partitions;
    }

    size_t chunk_size = (total + num_partitions - 1) / num_partitions;

    // Create partitions
    for (size_t i = 0; i < total; i += chunk_size) {
        MeshPartition<vertex_descriptor> partition;
        partition.start_idx = i;
        partition.count = std::min(chunk_size, total - i);

        auto start_it = all_vertices.begin() + i;
        auto end_it = start_it + partition.count;
        partition.descriptors.assign(start_it, end_it);

        partitions.push_back(std::move(partition));
    }

    return partitions;
}

/**
 * @brief Partition faces for parallel processing
 * @param mesh Mesh to partition
 * @param num_partitions Number of partitions to create
 * @return Vector of face partitions
 */
inline std::vector<MeshPartition<face_descriptor>>
partition_faces(const Mesh& mesh, size_t num_partitions) {
    std::vector<MeshPartition<face_descriptor>> partitions;

    if (num_partitions == 0) {
        num_partitions = 1;
    }

    // Collect all faces
    std::vector<face_descriptor> all_faces;
    all_faces.reserve(mesh.number_of_faces());
    for (auto f : mesh.faces()) {
        all_faces.push_back(f);
    }

    size_t total = all_faces.size();
    if (total == 0) {
        return partitions;
    }

    size_t chunk_size = (total + num_partitions - 1) / num_partitions;

    // Create partitions
    for (size_t i = 0; i < total; i += chunk_size) {
        MeshPartition<face_descriptor> partition;
        partition.start_idx = i;
        partition.count = std::min(chunk_size, total - i);

        auto start_it = all_faces.begin() + i;
        auto end_it = start_it + partition.count;
        partition.descriptors.assign(start_it, end_it);

        partitions.push_back(std::move(partition));
    }

    return partitions;
}

/**
 * @brief Partition halfedges for parallel processing
 * @param mesh Mesh to partition
 * @param num_partitions Number of partitions to create
 * @return Vector of halfedge partitions
 */
inline std::vector<MeshPartition<halfedge_descriptor>>
partition_halfedges(const Mesh& mesh, size_t num_partitions) {
    std::vector<MeshPartition<halfedge_descriptor>> partitions;

    if (num_partitions == 0) {
        num_partitions = 1;
    }

    // Collect all halfedges
    std::vector<halfedge_descriptor> all_halfedges;
    all_halfedges.reserve(mesh.number_of_halfedges());
    for (auto h : mesh.halfedges()) {
        all_halfedges.push_back(h);
    }

    size_t total = all_halfedges.size();
    if (total == 0) {
        return partitions;
    }

    size_t chunk_size = (total + num_partitions - 1) / num_partitions;

    // Create partitions
    for (size_t i = 0; i < total; i += chunk_size) {
        MeshPartition<halfedge_descriptor> partition;
        partition.start_idx = i;
        partition.count = std::min(chunk_size, total - i);

        auto start_it = all_halfedges.begin() + i;
        auto end_it = start_it + partition.count;
        partition.descriptors.assign(start_it, end_it);

        partitions.push_back(std::move(partition));
    }

    return partitions;
}

} // namespace MeshRepair

#endif // MESHREPAIR_PARALLEL_UTILS_H
