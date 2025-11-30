#include "submesh_ops.h"
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/boost/graph/iterator.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <vector>
#include <cstdint>
#include "polygon_soup_repair.h"
#include "debug_path.h"

namespace PMP = CGAL::Polygon_mesh_processing;

namespace MeshRepair {

HoleWithNeighborhood
partition_compute_neighborhood(const MeshPartitionerCtx& ctx, const HoleInfo& hole)
{
    HoleWithNeighborhood result;
    result.hole = hole;
    if (!ctx.mesh) {
        return result;
    }

    const Mesh& mesh = *ctx.mesh;
    std::unordered_set<vertex_descriptor> visited_vertices;
    std::unordered_set<face_descriptor> visited_faces;

    for (const auto& v : hole.boundary_vertices) {
        visited_vertices.insert(v);
    }

    std::vector<vertex_descriptor> frontier(hole.boundary_vertices.begin(), hole.boundary_vertices.end());

    for (unsigned int ring = 0; ring < ctx.n_rings; ++ring) {
        std::vector<vertex_descriptor> next_frontier;
        next_frontier.reserve(frontier.size() * 2);

        for (auto v : frontier) {
            auto hv = mesh.halfedge(v);
            if (hv == Mesh::null_halfedge()) {
                continue;
            }
            for (auto h : CGAL::halfedges_around_target(hv, mesh)) {
                auto f = mesh.face(h);
                if (f != Mesh::null_face()) {
                    visited_faces.insert(f);
                }

                auto v_source = mesh.source(h);
                if (visited_vertices.insert(v_source).second) {
                    next_frontier.push_back(v_source);
                }
            }
        }

        frontier.swap(next_frontier);
    }

    result.n_ring_vertices = std::move(visited_vertices);
    result.n_ring_faces    = std::move(visited_faces);

    std::vector<Point_3> bbox_points;
    bbox_points.reserve(result.n_ring_vertices.size());
    for (auto v : result.n_ring_vertices) {
        bbox_points.push_back(mesh.point(v));
    }
    if (!bbox_points.empty()) {
        result.bbox = CGAL::bbox_3(bbox_points.begin(), bbox_points.end());
    }

    return result;
}

std::vector<std::vector<size_t>>
partition_holes_by_count(const std::vector<HoleInfo>& holes, size_t num_partitions)
{
    if (num_partitions == 0) {
        num_partitions = 1;
    }
    // Do not create more partitions than holes (or 1 when holes are empty)
    size_t max_partitions = holes.empty() ? 1 : holes.size();
    num_partitions        = std::min(num_partitions, max_partitions);

    std::vector<std::vector<size_t>> partitions(num_partitions);
    if (holes.empty()) {
        return partitions;
    }

    // Greedy load balancing by boundary size (heaviest holes first)
    std::vector<size_t> order(holes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](size_t lhs, size_t rhs) { return holes[lhs].boundary_size > holes[rhs].boundary_size; });

    std::vector<size_t> partition_load(num_partitions, 0);
    for (size_t hole_idx : order) {
        auto min_it   = std::min_element(partition_load.begin(), partition_load.end());
        size_t target = static_cast<size_t>(std::distance(partition_load.begin(), min_it));
        partitions[target].push_back(hole_idx);
        partition_load[target] += holes[hole_idx].boundary_size;
    }

    return partitions;
}

unsigned int
partition_ring_count(const MeshPartitionerCtx& ctx)
{
    return ctx.n_rings;
}

namespace {
    halfedge_descriptor find_mapped_halfedge(halfedge_descriptor old_halfedge, const Mesh& old_mesh,
                                             const Mesh& new_mesh,
                                             const std::map<vertex_descriptor, vertex_descriptor>& vertex_map)
    {
        auto from_it = vertex_map.find(old_mesh.source(old_halfedge));
        auto to_it   = vertex_map.find(old_mesh.target(old_halfedge));

        if (from_it == vertex_map.end() || to_it == vertex_map.end()) {
            return Mesh::null_halfedge();
        }

        auto new_from = from_it->second;
        auto new_to   = to_it->second;

        if (auto h_pair = halfedge(new_from, new_to, new_mesh); h_pair.second) {
            return h_pair.first;
        }

        auto hv_from = new_mesh.halfedge(new_from);
        if (hv_from != Mesh::null_halfedge()) {
            for (auto h : CGAL::halfedges_around_source(hv_from, new_mesh)) {
                if (new_mesh.target(h) == new_to) {
                    return h;
                }
            }
        }

        auto hv_to = new_mesh.halfedge(new_to);
        if (hv_to != Mesh::null_halfedge()) {
            for (auto h : CGAL::halfedges_around_source(hv_to, new_mesh)) {
                if (new_mesh.target(h) == new_from) {
                    return new_mesh.opposite(h);  // Return oriented from new_from -> new_to
                }
            }
        }

        return Mesh::null_halfedge();
    }

    halfedge_descriptor find_boundary_halfedge(const Mesh& new_mesh,
                                               const std::vector<vertex_descriptor>& boundary_vertices)
    {
        if (boundary_vertices.size() < 2) {
            return Mesh::null_halfedge();
        }

        for (size_t i = 0; i < boundary_vertices.size(); ++i) {
            auto v0 = boundary_vertices[i];
            auto v1 = boundary_vertices[(i + 1) % boundary_vertices.size()];

            if (auto h_pair = halfedge(v0, v1, new_mesh); h_pair.second) {
                return h_pair.first;
            }
            if (auto h_pair = halfedge(v1, v0, new_mesh); h_pair.second) {
                return new_mesh.opposite(h_pair.first);
            }
        }

        return Mesh::null_halfedge();
    }
}  // namespace

Submesh
submesh_extract(const SubmeshExtractorCtx& ctx, const std::unordered_set<face_descriptor>& faces,
                const std::vector<HoleInfo>& holes)
{
    Submesh submesh;
    if (!ctx.mesh) {
        return submesh;
    }

    const Mesh& mesh = *ctx.mesh;

    std::unordered_map<vertex_descriptor, vertex_descriptor> vertex_map;

    for (auto f : faces) {
        std::vector<vertex_descriptor> face_vertices;
        for (auto v : vertices_around_face(mesh.halfedge(f), mesh)) {
            face_vertices.push_back(v);
            if (vertex_map.find(v) == vertex_map.end()) {
                auto new_v    = submesh.mesh.add_vertex(mesh.point(v));
                vertex_map[v] = new_v;
            }
        }

        if (face_vertices.size() >= 3) {
            submesh.mesh.add_face(vertex_map[face_vertices[0]], vertex_map[face_vertices[1]],
                                  vertex_map[face_vertices[2]]);
        }
    }

    submesh.old_to_new_vertex = std::map<vertex_descriptor, vertex_descriptor>(vertex_map.begin(), vertex_map.end());
    for (const auto& kv : submesh.old_to_new_vertex) {
        submesh.new_to_old_vertex[kv.second] = kv.first;
    }

    submesh.original_hole_count = holes.size();
    submesh.holes.reserve(holes.size());

    for (const auto& hole : holes) {
        HoleInfo new_hole = hole;

        std::vector<vertex_descriptor> new_boundary_vertices;
        new_boundary_vertices.reserve(hole.boundary_vertices.size());
        for (auto v : hole.boundary_vertices) {
            auto it = submesh.old_to_new_vertex.find(v);
            if (it != submesh.old_to_new_vertex.end()) {
                new_boundary_vertices.push_back(it->second);
            }
        }

        new_hole.boundary_size     = new_boundary_vertices.size();
        new_hole.boundary_vertices = std::move(new_boundary_vertices);

        if (new_hole.boundary_vertices.size() < 3) {
            continue;  // Degenerate boundary in submesh; skip
        }

        auto mapped_halfedge = find_mapped_halfedge(hole.boundary_halfedge, mesh, submesh.mesh,
                                                    submesh.old_to_new_vertex);
        if (mapped_halfedge == Mesh::null_halfedge()) {
            mapped_halfedge = find_boundary_halfedge(submesh.mesh, new_hole.boundary_vertices);
        }
        if (mapped_halfedge == Mesh::null_halfedge()) {
            continue;  // Skip holes that cannot be mapped into the submesh
        }

        new_hole.boundary_halfedge = mapped_halfedge;

        submesh.holes.push_back(std::move(new_hole));
    }

    return submesh;
}

Submesh
submesh_extract_partition(const SubmeshExtractorCtx& ctx, const std::vector<size_t>& partition_indices,
                          const std::vector<HoleInfo>& all_holes,
                          const std::vector<HoleWithNeighborhood>& neighborhoods)
{
    std::unordered_set<face_descriptor> faces;
    std::vector<HoleInfo> partition_holes;
    partition_holes.reserve(partition_indices.size());

    for (size_t idx : partition_indices) {
        partition_holes.push_back(all_holes[idx]);
        const auto& nbr = neighborhoods[idx];
        faces.insert(nbr.n_ring_faces.begin(), nbr.n_ring_faces.end());
    }

    return submesh_extract(ctx, faces, partition_holes);
}

namespace {
    uint64_t hash_face_indices(const std::vector<std::size_t>& sorted_indices)
    {
        constexpr uint64_t kOffset = 1469598103934665603ull;
        constexpr uint64_t kPrime  = 1099511628211ull;

        uint64_t h = kOffset ^ static_cast<uint64_t>(sorted_indices.size());
        for (auto idx : sorted_indices) {
            h ^= static_cast<uint64_t>(idx) + 0x9e3779b97f4a7c15ull;
            h *= kPrime;
        }
        return h;
    }
}  // namespace

Mesh
mesh_merger_merge(const Mesh& original_mesh, const std::vector<Submesh>& submeshes, bool verbose, bool holes_only,
                  bool debug_dump)
{
    std::vector<Point_3> points;
    std::vector<std::vector<std::size_t>> polygons;

    points.reserve(original_mesh.number_of_vertices());
    polygons.reserve(original_mesh.number_of_faces());

    for (auto v : original_mesh.vertices()) {
        points.push_back(original_mesh.point(v));
    }

    std::unordered_set<uint64_t> base_face_keys;
    std::vector<std::size_t> face_indices;
    if (holes_only) {
        base_face_keys.reserve(original_mesh.number_of_faces() * 2);
        for (auto f : original_mesh.faces()) {
            face_indices.clear();
            for (auto v : vertices_around_face(original_mesh.halfedge(f), original_mesh)) {
                face_indices.push_back(static_cast<std::size_t>(v.idx()));
            }
            std::sort(face_indices.begin(), face_indices.end());
            base_face_keys.insert(hash_face_indices(face_indices));
        }
    } else {
        for (auto f : original_mesh.faces()) {
            std::vector<std::size_t> poly;
            for (auto v : vertices_around_face(original_mesh.halfedge(f), original_mesh)) {
                poly.push_back(static_cast<std::size_t>(v.idx()));
            }
            polygons.push_back(std::move(poly));
        }
    }

    for (const auto& sub : submeshes) {
        std::size_t offset = points.size();
        points.reserve(points.size() + sub.mesh.number_of_vertices());
        polygons.reserve(polygons.size() + sub.mesh.number_of_faces());

        for (auto v : sub.mesh.vertices()) {
            points.push_back(sub.mesh.point(v));
        }

        for (auto f : sub.mesh.faces()) {
            std::vector<std::size_t> poly;
            std::vector<std::size_t> mapped_face;
            bool all_mapped = true;

            for (auto v : vertices_around_face(sub.mesh.halfedge(f), sub.mesh)) {
                poly.push_back(static_cast<std::size_t>(v.idx()) + offset);

                if (holes_only) {
                    auto it = sub.new_to_old_vertex.find(v);
                    if (it == sub.new_to_old_vertex.end()) {
                        all_mapped = false;
                    } else {
                        mapped_face.push_back(static_cast<std::size_t>(it->second.idx()));
                    }
                }
            }

            if (holes_only && all_mapped) {
                std::sort(mapped_face.begin(), mapped_face.end());
                uint64_t key = hash_face_indices(mapped_face);
                if (base_face_keys.find(key) != base_face_keys.end()) {
                    continue;  // Skip faces that already exist in the base mesh
                }
            }

            polygons.push_back(std::move(poly));
        }
    }

    if (verbose) {
        std::cerr << "[Merge] Combined soup (pre-repair): " << points.size() << " points, " << polygons.size()
                  << " polygons\n";
    }

    // Debug dump: raw combined soup before repair/orient
    if (verbose || debug_dump) {
        Mesh raw_mesh;
        PMP::polygon_soup_to_polygon_mesh(points, polygons, raw_mesh);
        std::string debug_file = DebugPath::step_file(holes_only ? "merged_partitions_holes_only_raw"
                                                                 : "merged_partitions_raw");
        CGAL::IO::write_PLY(debug_file, raw_mesh, CGAL::parameters::use_binary_mode(true));
    }

    PMP::repair_polygon_soup(points, polygons);

    size_t removed_non_manifold = PolygonSoupRepair::remove_non_manifold_polygons(polygons);
    if (verbose && removed_non_manifold > 0) {
        std::cerr << "[Merge] Removed " << removed_non_manifold << " non-manifold polygon(s)\n";
    }

    bool oriented = PMP::orient_polygon_soup(points, polygons);
    if (verbose && !oriented) {
        std::cerr << "[Merge] Warning: Some points were duplicated during orientation\n";
    }

    Mesh merged;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, merged);

    if (holes_only && debug_dump) {
        std::string debug_file = DebugPath::step_file("merged_partitions_holes_only");
        CGAL::IO::write_PLY(debug_file, merged, CGAL::parameters::use_binary_mode(true));
    }

    if (verbose) {
        std::cerr << "[Merge] Final mesh: " << merged.number_of_vertices() << " vertices, " << merged.number_of_faces()
                  << " faces\n";
    }

    return merged;
}

}  // namespace MeshRepair
