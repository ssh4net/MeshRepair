#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <utility>

namespace MeshRepair {

/// Result of non-manifold polygon removal
struct NonManifoldRemovalResult {
    size_t total_polygons_removed = 0;   // Total polygons removed across all iterations
    size_t iterations_executed = 0;      // Number of iterations performed
    bool hit_max_iterations = false;     // True if stopped due to max_depth limit
};

/// Parallel polygon soup repair utilities
class PolygonSoupRepair {
public:
    /// Detect and remove polygons containing non-manifold vertices/edges
    ///
    /// Uses recursive local search instead of N global passes:
    /// 1. Initial pass: Check ALL vertices for non-manifold issues
    /// 2. Remove polygons containing non-manifold vertices/edges
    /// 3. Collect "affected vertices" (vertices in removed polygons + neighbors)
    /// 4. Recursively check only affected vertices until no more issues found
    ///
    /// PERFORMANCE OPTIMIZED: Uses flat buffers and two-pass algorithm to minimize allocations
    ///
    /// @param polygons Polygon soup (each polygon is a vector of vertex indices)
    /// @param max_depth Maximum recursion depth (default: 10)
    /// @param verbose Print debug info about passes (default: false)
    /// @return Detailed result with iteration count and removal statistics
    template<typename PolygonRange>
    static NonManifoldRemovalResult remove_non_manifold_polygons_detailed(
        PolygonRange& polygons,
        size_t max_depth = 10,
        bool verbose = false)
    {
        NonManifoldRemovalResult result;

        if (polygons.empty()) {
            return result;
        }

        using PolygonID = size_t;
        using VertexID = size_t;
        using Edge = std::pair<size_t, size_t>;

        // OPTIMIZATION: Reusable buffers allocated ONCE outside loop
        std::unordered_set<PolygonID> polygons_to_remove;
        std::unordered_set<VertexID> vertices_to_check;
        std::unordered_set<VertexID> vertices_to_check_next;

        // Flat buffer approach to avoid hash map overhead
        std::vector<size_t> vertex_count;          // Count of polygons per vertex
        std::vector<size_t> vertex_offsets;        // Start offset for each vertex
        std::vector<PolygonID> vertex_poly_data;   // Flat array of polygon IDs

        std::unordered_map<Edge, std::vector<PolygonID>, EdgeHash> edge_to_polygons;
        edge_to_polygons.reserve(polygons.size() * 3);

        // Iterative passes
        for (size_t pass = 0; pass < max_depth; ++pass) {
            // PHASE 1: Build vertex-to-polygons map using TWO-PASS flat buffer approach

            // Find max vertex ID to size arrays
            size_t max_vertex = 0;
            for (const auto& polygon : polygons) {
                for (VertexID v : polygon) {
                    if (v > max_vertex) max_vertex = v;
                }
            }

            // Resize arrays
            vertex_count.assign(max_vertex + 1, 0);

            // Pass 1: Count polygons per vertex
            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                if (polygon.size() < 3) continue;

                for (VertexID v : polygon) {
                    vertex_count[v]++;
                }
            }

            // Compute offsets (prefix sum)
            vertex_offsets.resize(max_vertex + 2);  // +1 for sentinel
            vertex_offsets[0] = 0;
            for (size_t v = 0; v <= max_vertex; ++v) {
                vertex_offsets[v + 1] = vertex_offsets[v] + vertex_count[v];
            }

            // Allocate flat data array
            vertex_poly_data.resize(vertex_offsets[max_vertex + 1]);

            // Reset counts (will reuse as write indices)
            vertex_count.assign(max_vertex + 1, 0);

            // Pass 2: Fill polygon data
            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                if (polygon.size() < 3) continue;

                for (VertexID v : polygon) {
                    size_t idx = vertex_offsets[v] + vertex_count[v]++;
                    vertex_poly_data[idx] = poly_id;
                }
            }

            // Build edge-to-polygons map (clear and reuse)
            edge_to_polygons.clear();
            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                size_t num_verts = polygon.size();
                if (num_verts < 3) continue;

                for (size_t i = 0; i < num_verts; ++i) {
                    size_t v0 = polygon[i];
                    size_t v1 = polygon[(i + 1) % num_verts];
                    Edge edge = (v0 < v1) ? Edge{v0, v1} : Edge{v1, v0};

                    auto& vec = edge_to_polygons[edge];
                    if (vec.empty()) {
                        vec.reserve(2);  // Most edges have 1-2 incident polygons
                    }
                    vec.push_back(poly_id);
                }
            }

            // PHASE 2: Detect non-manifold vertices
            polygons_to_remove.clear();

            if (vertices_to_check.empty()) {
                // First pass - check ALL vertices
                for (size_t vertex = 0; vertex <= max_vertex; ++vertex) {
                    size_t start = vertex_offsets[vertex];
                    size_t end = vertex_offsets[vertex + 1];
                    size_t count = end - start;

                    if (count < 2) continue;

                    // Create view of incident polygons
                    const PolygonID* incident_polys = &vertex_poly_data[start];

                    if (!is_single_umbrella_array(vertex, incident_polys, count, polygons)) {
                        // Non-manifold vertex! Remove all incident polygons
                        for (size_t i = 0; i < count; ++i) {
                            polygons_to_remove.insert(incident_polys[i]);
                        }
                    }
                }
            } else {
                // Subsequent passes - check only affected vertices
                for (VertexID vertex : vertices_to_check) {
                    if (vertex > max_vertex) continue;

                    size_t start = vertex_offsets[vertex];
                    size_t end = vertex_offsets[vertex + 1];
                    size_t count = end - start;

                    if (count < 2) continue;

                    const PolygonID* incident_polys = &vertex_poly_data[start];

                    if (!is_single_umbrella_array(vertex, incident_polys, count, polygons)) {
                        for (size_t i = 0; i < count; ++i) {
                            polygons_to_remove.insert(incident_polys[i]);
                        }
                    }
                }
            }

            // PHASE 3: Check for non-manifold edges
            for (const auto& [edge, incident_polys] : edge_to_polygons) {
                if (incident_polys.size() > 2) {
                    polygons_to_remove.insert(incident_polys.begin(), incident_polys.end());
                }
            }

            // No more issues found
            if (polygons_to_remove.empty()) {
                result.iterations_executed = pass + 1;
                break;
            }

            // PHASE 4: Collect affected vertices
            vertices_to_check_next.clear();

            for (PolygonID poly_id : polygons_to_remove) {
                if (poly_id >= polygons.size()) continue;

                const auto& polygon = polygons[poly_id];
                for (VertexID v : polygon) {
                    vertices_to_check_next.insert(v);
                }

                // Add neighbor vertices
                for (size_t i = 0; i < polygon.size(); ++i) {
                    size_t v0 = polygon[i];
                    size_t v1 = polygon[(i + 1) % polygon.size()];
                    Edge edge = (v0 < v1) ? Edge{v0, v1} : Edge{v1, v0};

                    auto it = edge_to_polygons.find(edge);
                    if (it != edge_to_polygons.end()) {
                        for (PolygonID neighbor_id : it->second) {
                            if (neighbor_id != poly_id && neighbor_id < polygons.size()) {
                                for (VertexID neighbor_v : polygons[neighbor_id]) {
                                    vertices_to_check_next.insert(neighbor_v);
                                }
                            }
                        }
                    }
                }
            }

            // PHASE 5: Remove marked polygons
            auto new_end = std::remove_if(polygons.begin(), polygons.end(),
                [&polygons_to_remove, poly_id = size_t(0)](const auto& polygon) mutable {
                    (void)polygon;
                    return polygons_to_remove.find(poly_id++) != polygons_to_remove.end();
                });

            size_t removed = std::distance(new_end, polygons.end());
            polygons.erase(new_end, polygons.end());
            result.total_polygons_removed += removed;

            vertices_to_check.swap(vertices_to_check_next);

            if (pass == max_depth - 1) {
                result.iterations_executed = max_depth;
                result.hit_max_iterations = true;
                break;
            }
        }

        return result;
    }

    /// Legacy interface
    template<typename PolygonRange>
    static size_t remove_non_manifold_polygons(
        PolygonRange& polygons,
        size_t max_depth = 10,
        bool verbose = false)
    {
        auto result = remove_non_manifold_polygons_detailed(polygons, max_depth, verbose);
        return result.total_polygons_removed;
    }

    /// Detect and remove 3-face fans around central vertices
    ///
    /// A 3-face fan is:
    /// - 3 triangular faces sharing a common central vertex
    /// - The fan has exactly 3 boundary edges (forming a triangle outline)
    /// - Can be replaced with a single triangle, removing the central vertex
    ///
    /// This simplification reduces polygon count and removes unnecessary vertices
    ///
    /// @param points Point array (vertices may be removed)
    /// @param polygons Polygon soup (3 triangles replaced with 1)
    /// @return Number of 3-face fans collapsed
    template<typename PointRange, typename PolygonRange>
    static size_t remove_3_face_fans(PointRange& points, PolygonRange& polygons)
    {
        if (polygons.empty()) return 0;

        using VertexID = size_t;
        using PolygonID = size_t;

        // Build vertex-to-polygons map using flat buffer approach
        size_t max_vertex = 0;
        for (const auto& polygon : polygons) {
            for (VertexID v : polygon) {
                if (v > max_vertex) max_vertex = v;
            }
        }

        std::vector<size_t> vertex_count(max_vertex + 1, 0);
        std::vector<size_t> vertex_offsets(max_vertex + 2);

        // Count polygons per vertex
        for (const auto& polygon : polygons) {
            if (polygon.size() != 3) continue;  // Only process triangles
            for (VertexID v : polygon) {
                vertex_count[v]++;
            }
        }

        // Compute offsets
        vertex_offsets[0] = 0;
        for (size_t v = 0; v <= max_vertex; ++v) {
            vertex_offsets[v + 1] = vertex_offsets[v] + vertex_count[v];
        }

        std::vector<PolygonID> vertex_poly_data(vertex_offsets[max_vertex + 1]);
        std::fill(vertex_count.begin(), vertex_count.end(), 0);

        // Fill polygon data
        for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
            const auto& polygon = polygons[poly_id];
            if (polygon.size() != 3) continue;

            for (VertexID v : polygon) {
                size_t idx = vertex_offsets[v] + vertex_count[v]++;
                vertex_poly_data[idx] = poly_id;
            }
        }

        // Find 3-face fans
        std::vector<bool> polygon_to_remove(polygons.size(), false);
        std::vector<bool> vertex_to_remove(max_vertex + 1, false);
        std::vector<std::vector<size_t>> new_triangles;
        size_t fans_found = 0;

        for (size_t center_v = 0; center_v <= max_vertex; ++center_v) {
            size_t start = vertex_offsets[center_v];
            size_t end = vertex_offsets[center_v + 1];
            size_t count = end - start;

            // Must have exactly 3 incident triangular faces
            if (count != 3) continue;

            const PolygonID* incident_polys = &vertex_poly_data[start];

            // Check if all 3 polygons are triangles
            bool all_triangles = true;
            for (size_t i = 0; i < 3; ++i) {
                if (polygons[incident_polys[i]].size() != 3) {
                    all_triangles = false;
                    break;
                }
            }
            if (!all_triangles) continue;

            // Collect boundary vertices (vertices not equal to center_v)
            std::vector<VertexID> boundary_verts;
            boundary_verts.reserve(6);  // 3 triangles, each has 2 boundary verts

            for (size_t i = 0; i < 3; ++i) {
                const auto& tri = polygons[incident_polys[i]];
                for (VertexID v : tri) {
                    if (v != center_v) {
                        boundary_verts.push_back(v);
                    }
                }
            }

            // Count unique boundary vertices
            std::sort(boundary_verts.begin(), boundary_verts.end());
            auto last = std::unique(boundary_verts.begin(), boundary_verts.end());
            boundary_verts.erase(last, boundary_verts.end());

            // Must have exactly 3 unique boundary vertices (forming outer triangle)
            if (boundary_verts.size() != 3) continue;

            // Verify this forms a valid fan by checking edge connectivity
            // Each boundary vertex should appear in exactly 2 of the 3 triangles
            bool valid_fan = true;
            for (VertexID bv : boundary_verts) {
                size_t appearances = 0;
                for (size_t i = 0; i < 3; ++i) {
                    const auto& tri = polygons[incident_polys[i]];
                    if (std::find(tri.begin(), tri.end(), bv) != tri.end()) {
                        appearances++;
                    }
                }
                if (appearances != 2) {
                    valid_fan = false;
                    break;
                }
            }

            if (!valid_fan) continue;

            // Valid 3-face fan found!
            // Mark the 3 triangles for removal
            for (size_t i = 0; i < 3; ++i) {
                polygon_to_remove[incident_polys[i]] = true;
            }

            // Mark center vertex for removal
            vertex_to_remove[center_v] = true;

            // Create replacement triangle from boundary vertices
            std::vector<size_t> new_tri;
            new_tri.reserve(3);
            new_tri.push_back(boundary_verts[0]);
            new_tri.push_back(boundary_verts[1]);
            new_tri.push_back(boundary_verts[2]);
            new_triangles.push_back(std::move(new_tri));

            fans_found++;
        }

        if (fans_found == 0) return 0;

        // Remove marked polygons
        auto poly_end = std::remove_if(polygons.begin(), polygons.end(),
            [&polygon_to_remove, idx = size_t(0)](const auto& poly) mutable {
                return polygon_to_remove[idx++];
            });
        polygons.erase(poly_end, polygons.end());

        // Add new triangles
        polygons.insert(polygons.end(), new_triangles.begin(), new_triangles.end());

        // Note: We don't actually remove vertices from points array to avoid re-indexing
        // The removed vertices become isolated and will be cleaned up by remove_isolated_vertices

        return fans_found;
    }

private:
    struct EdgeHash {
        size_t operator()(const std::pair<size_t, size_t>& edge) const {
            size_t a = edge.first;
            size_t b = edge.second;
            return (a + b) * (a + b + 1) / 2 + b;
        }
    };

    /// Optimized version that takes array pointer instead of vector
    template<typename PolygonRange>
    static bool is_single_umbrella_array(
        size_t vertex,
        const size_t* incident_polys,
        size_t num_polys,
        const PolygonRange& polygons)
    {
        if (num_polys < 2) return true;

        constexpr size_t STACK_CAPACITY = 16;
        constexpr size_t MAX_NEIGHBORS = 8;

        // Stack allocation for common case
        uint8_t visited_stack[STACK_CAPACITY];
        size_t stack_stack[STACK_CAPACITY];
        size_t adjacency_data_stack[STACK_CAPACITY * MAX_NEIGHBORS];
        size_t adjacency_count_stack[STACK_CAPACITY];

        uint8_t* visited;
        size_t* dfs_stack;
        size_t* adjacency_data;
        size_t* adjacency_count;
        size_t dfs_stack_size = 0;

        std::vector<uint8_t> visited_heap;
        std::vector<size_t> stack_heap;
        std::vector<size_t> adjacency_data_heap;
        std::vector<size_t> adjacency_count_heap;

        if (num_polys <= STACK_CAPACITY) {
            visited = visited_stack;
            dfs_stack = stack_stack;
            adjacency_data = adjacency_data_stack;
            adjacency_count = adjacency_count_stack;
            std::fill_n(visited, num_polys, 0);
            std::fill_n(adjacency_count, num_polys, 0);
        } else {
            visited_heap.resize(num_polys, 0);
            stack_heap.reserve(num_polys);
            adjacency_data_heap.resize(num_polys * MAX_NEIGHBORS);
            adjacency_count_heap.resize(num_polys, 0);
            visited = visited_heap.data();
            dfs_stack = stack_heap.data();
            adjacency_data = adjacency_data_heap.data();
            adjacency_count = adjacency_count_heap.data();
        }

        // Build adjacency
        for (size_t i = 0; i < num_polys; ++i) {
            const auto& polygon = polygons[incident_polys[i]];
            size_t num_verts = polygon.size();

            for (size_t j = 0; j < num_verts; ++j) {
                if (polygon[j] == vertex) {
                    size_t prev_v = polygon[(j + num_verts - 1) % num_verts];
                    size_t next_v = polygon[(j + 1) % num_verts];

                    for (size_t k = 0; k < num_polys; ++k) {
                        if (k == i) continue;

                        const auto& other = polygons[incident_polys[k]];
                        if (contains_edge(other, vertex, prev_v) ||
                            contains_edge(other, vertex, next_v)) {

                            if (adjacency_count[i] < MAX_NEIGHBORS) {
                                adjacency_data[i * MAX_NEIGHBORS + adjacency_count[i]++] = k;
                            }
                        }
                    }
                    break;
                }
            }
        }

        // DFS
        dfs_stack[dfs_stack_size++] = 0;
        visited[0] = true;
        size_t visited_count = 1;

        while (dfs_stack_size > 0) {
            size_t idx = dfs_stack[--dfs_stack_size];

            for (size_t i = 0; i < adjacency_count[idx]; ++i) {
                size_t neighbor = adjacency_data[idx * MAX_NEIGHBORS + i];
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    visited_count++;
                    dfs_stack[dfs_stack_size++] = neighbor;
                }
            }
        }

        return visited_count == num_polys;
    }

    template<typename Polygon>
    static inline bool contains_edge(const Polygon& polygon, size_t v0, size_t v1) {
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
