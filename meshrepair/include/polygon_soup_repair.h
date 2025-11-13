#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <utility>

namespace MeshRepair {

/// Parallel polygon soup repair utilities
class PolygonSoupRepair {
public:
    /// Detect and remove polygons containing non-manifold vertices
    ///
    /// A non-manifold vertex is a vertex whose incident faces do not form
    /// a single continuous fan (umbrella) around the vertex.
    ///
    /// Detection algorithm:
    /// 1. For each vertex, collect all incident polygons
    /// 2. Build an edge-adjacency graph of these polygons
    /// 3. Check if the graph forms a single connected component (one umbrella)
    /// 4. If not → non-manifold vertex → remove all incident polygons
    ///
    /// @param polygons Polygon soup (each polygon is a vector of vertex indices)
    /// @return Number of polygons removed
    template<typename PolygonRange>
    static size_t remove_non_manifold_polygons(PolygonRange& polygons) {
        if (polygons.empty()) {
            return 0;
        }

        // PHASE 1: Build vertex-to-polygons map
        using PolygonID = size_t;
        using VertexID = size_t;

        std::unordered_map<VertexID, std::vector<PolygonID>> vertex_to_polygons;

        for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
            const auto& polygon = polygons[poly_id];

            if (polygon.size() < 3) {
                continue; // Invalid polygon
            }

            for (VertexID v : polygon) {
                vertex_to_polygons[v].push_back(poly_id);
            }
        }

        // PHASE 2: Detect non-manifold vertices
        std::unordered_set<PolygonID> polygons_to_remove;

        for (const auto& [vertex, incident_polys] : vertex_to_polygons) {
            if (incident_polys.size() < 2) {
                continue; // Boundary or isolated vertex, not non-manifold
            }

            // Check if incident polygons form a single umbrella (fan)
            if (!is_single_umbrella(vertex, incident_polys, polygons)) {
                // Non-manifold vertex! Remove all incident polygons
                polygons_to_remove.insert(incident_polys.begin(), incident_polys.end());
            }
        }

        // PHASE 3: Also check for non-manifold edges (edges with > 2 polygons)
        using Edge = std::pair<size_t, size_t>;
        std::unordered_map<Edge, std::vector<PolygonID>, EdgeHash> edge_to_polygons;

        for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
            const auto& polygon = polygons[poly_id];
            size_t num_verts = polygon.size();

            if (num_verts < 3) {
                continue;
            }

            for (size_t i = 0; i < num_verts; ++i) {
                size_t v0 = polygon[i];
                size_t v1 = polygon[(i + 1) % num_verts];

                Edge edge = (v0 < v1) ? Edge{v0, v1} : Edge{v1, v0};
                edge_to_polygons[edge].push_back(poly_id);
            }
        }

        for (const auto& [edge, incident_polys] : edge_to_polygons) {
            if (incident_polys.size() > 2) {
                // Non-manifold edge!
                polygons_to_remove.insert(incident_polys.begin(), incident_polys.end());
            }
        }

        if (polygons_to_remove.empty()) {
            return 0;
        }

        // PHASE 4: Remove marked polygons
        auto new_end = std::remove_if(polygons.begin(), polygons.end(),
            [&polygons_to_remove, poly_id = size_t(0)](const auto& polygon) mutable {
                return polygons_to_remove.find(poly_id++) != polygons_to_remove.end();
            });

        size_t num_removed = std::distance(new_end, polygons.end());
        polygons.erase(new_end, polygons.end());

        return num_removed;
    }

private:
    /// Hash function for Edge (pair of size_t)
    struct EdgeHash {
        size_t operator()(const std::pair<size_t, size_t>& edge) const {
            // Cantor pairing function for perfect hash
            size_t a = edge.first;
            size_t b = edge.second;
            return (a + b) * (a + b + 1) / 2 + b;
        }
    };

    /// Check if polygons incident to a vertex form a single umbrella (fan)
    ///
    /// A single umbrella means:
    /// - The polygons can be ordered such that consecutive polygons share an edge
    /// - The ordering forms a single connected chain (possibly closed for interior vertices)
    ///
    /// @param vertex The vertex to check
    /// @param incident_polys Polygons incident to the vertex
    /// @param polygons All polygons in the soup
    /// @return true if forms a single umbrella, false if non-manifold
    template<typename PolygonRange>
    static bool is_single_umbrella(
        size_t vertex,
        const std::vector<size_t>& incident_polys,
        const PolygonRange& polygons)
    {
        if (incident_polys.size() < 2) {
            return true; // Not enough polygons to be non-manifold
        }

        // Build adjacency graph of incident polygons
        // Two polygons are adjacent if they share an edge containing the vertex
        using Edge = std::pair<size_t, size_t>;
        std::unordered_map<size_t, std::vector<size_t>> poly_adjacency;

        for (size_t poly_id : incident_polys) {
            const auto& polygon = polygons[poly_id];

            // Find edges containing the vertex
            size_t num_verts = polygon.size();
            for (size_t i = 0; i < num_verts; ++i) {
                if (polygon[i] == vertex) {
                    // Get the two adjacent vertices
                    size_t prev_v = polygon[(i + num_verts - 1) % num_verts];
                    size_t next_v = polygon[(i + 1) % num_verts];

                    // Check if any other incident polygon shares these edges
                    for (size_t other_poly_id : incident_polys) {
                        if (other_poly_id == poly_id) continue;

                        const auto& other_polygon = polygons[other_poly_id];

                        // Check if other polygon contains edge (vertex, prev_v) or (vertex, next_v)
                        if (contains_edge(other_polygon, vertex, prev_v) ||
                            contains_edge(other_polygon, vertex, next_v)) {
                            poly_adjacency[poly_id].push_back(other_poly_id);
                        }
                    }
                    break;
                }
            }
        }

        // Check if adjacency graph is connected
        // Use DFS to find all reachable polygons from the first one
        std::unordered_set<size_t> visited;
        std::vector<size_t> stack;

        stack.push_back(incident_polys[0]);
        visited.insert(incident_polys[0]);

        while (!stack.empty()) {
            size_t current = stack.back();
            stack.pop_back();

            for (size_t neighbor : poly_adjacency[current]) {
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    stack.push_back(neighbor);
                }
            }
        }

        // If we visited all incident polygons, it's a single umbrella
        return visited.size() == incident_polys.size();
    }

    /// Check if a polygon contains a directed edge (v0, v1) or (v1, v0)
    template<typename Polygon>
    static bool contains_edge(const Polygon& polygon, size_t v0, size_t v1) {
        size_t num_verts = polygon.size();
        for (size_t i = 0; i < num_verts; ++i) {
            size_t curr = polygon[i];
            size_t next = polygon[(i + 1) % num_verts];

            if ((curr == v0 && next == v1) || (curr == v1 && next == v0)) {
                return true;
            }
        }
        return false;
    }
};

} // namespace MeshRepair
