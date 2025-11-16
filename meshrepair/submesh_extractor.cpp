#include "submesh_extractor.h"
#include <CGAL/boost/graph/Face_filtered_graph.h>
#include <CGAL/boost/graph/copy_face_graph.h>
#include <CGAL/boost/graph/iterator.h>
#include <iostream>

namespace MeshRepair {

SubmeshExtractor::SubmeshExtractor(const Mesh& mesh)
    : mesh_(mesh)
{
}

Submesh
SubmeshExtractor::extract(const std::unordered_set<face_descriptor>& faces, const std::vector<HoleInfo>& holes) const
{
    Submesh result;
    result.original_hole_count = holes.size();

    if (faces.empty()) {
        // Empty submesh
        return result;
    }

    // Create a vector property map (doesn't modify the mesh)
    // Map face descriptors to partition ID (1 = include, 0 = exclude)
    std::vector<std::size_t> face_partition_ids(mesh_.number_of_faces(), 0);

    // Use the built-in face index map (always available)
    auto face_id_map = get(boost::face_index, mesh_);

    // Mark all faces in our set with ID 1, others with ID 0
    for (auto f : faces) {
        std::size_t idx         = get(face_id_map, f);
        face_partition_ids[idx] = 1;
    }

    // Create an associative property map using the vector
    auto face_partition_pmap = boost::make_iterator_property_map(face_partition_ids.begin(), face_id_map);

    // Create filtered view using the property map
    typedef CGAL::Face_filtered_graph<Mesh> Filtered_graph;
    Filtered_graph filtered_mesh(mesh_, 1, face_partition_pmap);

    // Copy filtered graph to new mesh
    // This creates new vertex/face/halfedge descriptors
    CGAL::copy_face_graph(filtered_mesh, result.mesh,
                          CGAL::parameters::vertex_to_vertex_map(
                              boost::make_assoc_property_map(result.old_to_new_vertex)));

    // Build reverse map (new -> old)
    for (const auto& [old_v, new_v] : result.old_to_new_vertex) {
        result.new_to_old_vertex[new_v] = old_v;
    }

    // Translate hole boundary halfedges to new mesh
    for (const auto& old_hole : holes) {
        HoleInfo new_hole = old_hole;

        // Find corresponding halfedge in new mesh
        halfedge_descriptor mapped_he = find_mapped_halfedge(old_hole.boundary_halfedge, result.mesh,
                                                             result.old_to_new_vertex);

        if (mapped_he != Mesh::null_halfedge()) {
            // Verify it's actually a border halfedge in the new mesh
            if (result.mesh.is_border(mapped_he)) {
                new_hole.boundary_halfedge = mapped_he;
                result.holes.push_back(new_hole);
            }
        }
    }

    return result;
}

Submesh
SubmeshExtractor::extract_partition(const std::vector<size_t>& partition_indices,
                                    const std::vector<HoleInfo>& all_holes,
                                    const std::vector<HoleWithNeighborhood>& neighborhoods) const
{
    // Collect all faces from all holes in this partition
    std::unordered_set<face_descriptor> partition_faces;
    std::vector<HoleInfo> partition_holes;

    for (size_t idx : partition_indices) {
        // Add faces from neighborhood
        partition_faces.insert(neighborhoods[idx].n_ring_faces.begin(), neighborhoods[idx].n_ring_faces.end());

        // Add hole info
        partition_holes.push_back(all_holes[idx]);
    }

    return extract(partition_faces, partition_holes);
}

halfedge_descriptor
SubmeshExtractor::find_mapped_halfedge(halfedge_descriptor old_halfedge, const Mesh& new_mesh,
                                       const std::map<vertex_descriptor, vertex_descriptor>& vertex_map) const
{
    // Get source and target vertices in old mesh
    vertex_descriptor old_src = source(old_halfedge, mesh_);
    vertex_descriptor old_tgt = target(old_halfedge, mesh_);

    // Find corresponding vertices in new mesh
    auto new_src_it = vertex_map.find(old_src);
    auto new_tgt_it = vertex_map.find(old_tgt);

    if (new_src_it == vertex_map.end() || new_tgt_it == vertex_map.end()) {
        // Vertices not in submesh
        return Mesh::null_halfedge();
    }

    vertex_descriptor new_src = new_src_it->second;
    vertex_descriptor new_tgt = new_tgt_it->second;

    // Find halfedge from new_src to new_tgt in new mesh
    for (auto he : CGAL::halfedges_around_source(new_src, new_mesh)) {
        if (target(he, new_mesh) == new_tgt) {
            return he;
        }
    }

    // Halfedge not found (shouldn't happen if faces are consistent)
    return Mesh::null_halfedge();
}

}  // namespace MeshRepair
