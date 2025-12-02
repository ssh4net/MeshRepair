#pragma once

#include <vector>
#include <algorithm>
#include <utility>
#include <cstdint>
#include <limits>

namespace MeshRepair {

/// Result of non-manifold polygon removal
struct NonManifoldRemovalResult {
    size_t total_polygons_removed = 0;      // Total polygons removed across all iterations
    size_t iterations_executed    = 0;      // Number of iterations performed
    bool hit_max_iterations       = false;  // True if stopped due to max_depth limit
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
    static NonManifoldRemovalResult remove_non_manifold_polygons_detailed(PolygonRange& polygons, size_t max_depth = 10,
                                                                          bool verbose = false)
    {
        NonManifoldRemovalResult result;

        using PolygonID = size_t;
        using VertexID  = size_t;

        (void)verbose;

        if (polygons.empty()) {
            return result;
        }

        size_t max_vertex = 0;
        for (const auto& polygon : polygons) {
            for (VertexID v : polygon) {
                if (v > max_vertex)
                    max_vertex = v;
            }
        }

        struct EdgeEntry {
            size_t v0;
            size_t v1;
            PolygonID poly_id;
        };

        struct EdgeSpan {
            size_t v0;
            size_t v1;
            size_t start;
            size_t count;
        };

        struct Workspace {
            std::vector<uint8_t> polygonRemoveFlags;
            std::vector<PolygonID> polygons_to_remove;
            std::vector<uint8_t> vertexCheckNextFlags;
            std::vector<VertexID> vertices_to_check;
            std::vector<VertexID> vertices_to_check_next;

            std::vector<size_t> vertex_count;
            std::vector<size_t> vertex_offsets;
            std::vector<PolygonID> vertex_poly_data;

            std::vector<EdgeEntry> edge_entries;
            std::vector<EdgeSpan> edge_spans;
            std::vector<size_t> edge_count_by_v0;
            std::vector<size_t> edge_offsets_by_v0;
        };
        static thread_local Workspace ws;

        auto& polygonRemoveFlags     = ws.polygonRemoveFlags;
        auto& polygons_to_remove     = ws.polygons_to_remove;
        auto& vertexCheckNextFlags   = ws.vertexCheckNextFlags;
        auto& vertices_to_check      = ws.vertices_to_check;
        auto& vertices_to_check_next = ws.vertices_to_check_next;
        auto& vertex_count           = ws.vertex_count;
        auto& vertex_offsets         = ws.vertex_offsets;
        auto& vertex_poly_data       = ws.vertex_poly_data;
        auto& edge_entries           = ws.edge_entries;
        auto& edge_spans             = ws.edge_spans;
        auto& edge_count_by_v0       = ws.edge_count_by_v0;
        auto& edge_offsets_by_v0     = ws.edge_offsets_by_v0;

        edge_entries.reserve(polygons.size() * 3);
        polygons_to_remove.clear();
        vertices_to_check.clear();
        vertices_to_check_next.clear();

        auto resetSizedVector = [](std::vector<size_t>& vec, size_t size) {
            if (vec.size() < size) {
                vec.resize(size);
            }
            std::fill(vec.begin(), vec.begin() + size, 0);
        };

        // Iterative passes
        for (size_t pass = 0; pass < max_depth; ++pass) {
            // PHASE 1: Build vertex-to-polygons map using TWO-PASS flat buffer approach

            // Resize arrays
            resetSizedVector(vertex_count, max_vertex + 1);

            // Pass 1: Count polygons per vertex
            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                if (polygon.size() < 3)
                    continue;

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
            std::fill(vertex_count.begin(), vertex_count.end(), 0);

            // Pass 2: Fill polygon data
            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                if (polygon.size() < 3)
                    continue;

                for (VertexID v : polygon) {
                    size_t idx            = vertex_offsets[v] + vertex_count[v]++;
                    vertex_poly_data[idx] = poly_id;
                }
            }

            // Build canonicalized edge list grouped by v0 to avoid global sort
            edge_entries.clear();
            edge_spans.clear();
            resetSizedVector(edge_count_by_v0, max_vertex + 1);

            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                size_t num_verts    = polygon.size();
                if (num_verts < 3)
                    continue;

                for (size_t i = 0; i < num_verts; ++i) {
                    size_t v0 = polygon[i];
                    size_t v1 = polygon[(i + 1) % num_verts];
                    if (v1 < v0) {
                        std::swap(v0, v1);
                    }
                    edge_count_by_v0[v0]++;
                }
            }

            edge_offsets_by_v0.resize(max_vertex + 2);
            edge_offsets_by_v0[0] = 0;
            for (size_t v0 = 0; v0 <= max_vertex; ++v0) {
                edge_offsets_by_v0[v0 + 1] = edge_offsets_by_v0[v0] + edge_count_by_v0[v0];
            }

            edge_entries.resize(edge_offsets_by_v0[max_vertex + 1]);
            edge_spans.reserve(edge_entries.size());

            // reuse counts as write indices
            std::fill(edge_count_by_v0.begin(), edge_count_by_v0.end(), 0);

            for (size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
                const auto& polygon = polygons[poly_id];
                size_t num_verts    = polygon.size();
                if (num_verts < 3)
                    continue;

                for (size_t i = 0; i < num_verts; ++i) {
                    size_t v0 = polygon[i];
                    size_t v1 = polygon[(i + 1) % num_verts];
                    if (v1 < v0) {
                        std::swap(v0, v1);
                    }
                    size_t slot        = edge_offsets_by_v0[v0] + edge_count_by_v0[v0]++;
                    edge_entries[slot] = { v0, v1, poly_id };
                }
            }

            // Sort edges within each v0 bucket by v1 (and poly as tie-breaker)
            for (size_t v0 = 0; v0 <= max_vertex; ++v0) {
                size_t start             = edge_offsets_by_v0[v0];
                size_t end               = edge_offsets_by_v0[v0 + 1];
                const size_t bucket_size = end - start;
                if (bucket_size > 1) {
                    if (bucket_size <= 8) {
                        // Insertion sort for tiny buckets
                        for (size_t i = start + 1; i < end; ++i) {
                            EdgeEntry key = edge_entries[i];
                            size_t j      = i;
                            while (j > start) {
                                const auto& prev = edge_entries[j - 1];
                                if (prev.v1 < key.v1 || (prev.v1 == key.v1 && prev.poly_id <= key.poly_id)) {
                                    break;
                                }
                                edge_entries[j] = prev;
                                --j;
                            }
                            edge_entries[j] = key;
                        }
                    } else {
                        std::sort(edge_entries.begin() + start, edge_entries.begin() + end,
                                  [](const EdgeEntry& a, const EdgeEntry& b) {
                                      if (a.v1 == b.v1) {
                                          return a.poly_id < b.poly_id;
                                      }
                                      return a.v1 < b.v1;
                                  });
                    }
                }
            }

            // Build spans pointing into grouped/sorted edge list
            for (size_t v0 = 0; v0 <= max_vertex; ++v0) {
                size_t start = edge_offsets_by_v0[v0];
                size_t end   = edge_offsets_by_v0[v0 + 1];
                size_t idx   = start;
                while (idx < end) {
                    size_t run_start = idx;
                    size_t v1        = edge_entries[idx].v1;
                    while (idx < end && edge_entries[idx].v1 == v1) {
                        ++idx;
                    }
                    edge_spans.push_back({ v0, v1, run_start, idx - run_start });
                }
            }

            // PHASE 2: Detect non-manifold vertices
            if (polygonRemoveFlags.size() < polygons.size()) {
                polygonRemoveFlags.resize(polygons.size(), 0);
            }
            std::fill(polygonRemoveFlags.begin(), polygonRemoveFlags.begin() + polygons.size(), 0);
            polygons_to_remove.clear();

            auto markPolygonForRemoval = [&](PolygonID id) {
                if (id >= polygonRemoveFlags.size())
                    return;
                if (!polygonRemoveFlags[id]) {
                    polygonRemoveFlags[id] = 1;
                    polygons_to_remove.push_back(id);
                }
            };

            if (vertices_to_check.empty()) {
                // First pass - check ALL vertices
                for (size_t vertex = 0; vertex <= max_vertex; ++vertex) {
                    size_t start = vertex_offsets[vertex];
                    size_t end   = vertex_offsets[vertex + 1];
                    size_t count = end - start;

                    if (count < 2)
                        continue;

                    // Create view of incident polygons
                    const PolygonID* incident_polys = &vertex_poly_data[start];

                    if (!is_single_umbrella_array(vertex, incident_polys, count, polygons)) {
                        // Non-manifold vertex! Remove all incident polygons
                        for (size_t i = 0; i < count; ++i) {
                            markPolygonForRemoval(incident_polys[i]);
                        }
                    }
                }
            } else {
                // Subsequent passes - check only affected vertices
                for (VertexID vertex : vertices_to_check) {
                    if (vertex > max_vertex)
                        continue;

                    size_t start = vertex_offsets[vertex];
                    size_t end   = vertex_offsets[vertex + 1];
                    size_t count = end - start;

                    if (count < 2)
                        continue;

                    const PolygonID* incident_polys = &vertex_poly_data[start];

                    if (!is_single_umbrella_array(vertex, incident_polys, count, polygons)) {
                        for (size_t i = 0; i < count; ++i) {
                            markPolygonForRemoval(incident_polys[i]);
                        }
                    }
                }
            }

            // PHASE 3: Check for non-manifold edges
            for (const auto& span : edge_spans) {
                if (span.count > 2) {
                    size_t end = span.start + span.count;
                    for (size_t idx = span.start; idx < end; ++idx) {
                        markPolygonForRemoval(edge_entries[idx].poly_id);
                    }
                }
            }

            // No more issues found
            if (polygons_to_remove.empty()) {
                result.iterations_executed = pass + 1;
                break;
            }

            // PHASE 4: Collect affected vertices
            vertices_to_check_next.clear();
            if (vertexCheckNextFlags.size() < max_vertex + 1) {
                vertexCheckNextFlags.resize(max_vertex + 1, 0);
            }

            auto markVertexForNextPass = [&](VertexID v) {
                if (v >= vertexCheckNextFlags.size()) {
                    vertexCheckNextFlags.resize(v + 1, 0);
                }
                if (!vertexCheckNextFlags[v]) {
                    vertexCheckNextFlags[v] = 1;
                    vertices_to_check_next.push_back(v);
                }
            };

            auto spanLess = [](const EdgeSpan& span, const EdgeSpan& key) {
                if (span.v0 == key.v0) {
                    return span.v1 < key.v1;
                }
                return span.v0 < key.v0;
            };

            auto findEdgeSpan = [&](size_t v0, size_t v1) -> const EdgeSpan* {
                if (v1 < v0) {
                    std::swap(v0, v1);
                }
                EdgeSpan key { v0, v1, 0, 0 };
                auto it = std::lower_bound(edge_spans.begin(), edge_spans.end(), key, spanLess);
                if (it != edge_spans.end() && it->v0 == v0 && it->v1 == v1) {
                    return &(*it);
                }
                return nullptr;
            };

            for (PolygonID poly_id : polygons_to_remove) {
                if (poly_id >= polygons.size())
                    continue;

                const auto& polygon = polygons[poly_id];
                for (VertexID v : polygon) {
                    markVertexForNextPass(v);
                }

                // Add neighbor vertices
                for (size_t i = 0; i < polygon.size(); ++i) {
                    size_t v0            = polygon[i];
                    size_t v1            = polygon[(i + 1) % polygon.size()];
                    const EdgeSpan* span = findEdgeSpan(v0, v1);
                    if (span) {
                        size_t end = span->start + span->count;
                        for (size_t idx = span->start; idx < end; ++idx) {
                            PolygonID neighbor_id = edge_entries[idx].poly_id;
                            if (neighbor_id != poly_id && neighbor_id < polygons.size()) {
                                for (VertexID neighbor_v : polygons[neighbor_id]) {
                                    markVertexForNextPass(neighbor_v);
                                }
                            }
                        }
                    }
                }
            }

            // PHASE 5: Remove marked polygons
            auto new_end = std::remove_if(polygons.begin(), polygons.end(),
                                          [&polygonRemoveFlags, poly_id = size_t(0)](const auto& polygon) mutable {
                                              (void)polygon;
                                              return poly_id < polygonRemoveFlags.size()
                                                     && polygonRemoveFlags[poly_id++];
                                          });

            size_t removed = std::distance(new_end, polygons.end());

            for (PolygonID id : polygons_to_remove) {
                if (id < polygonRemoveFlags.size()) {
                    polygonRemoveFlags[id] = 0;
                }
            }

            polygons.erase(new_end, polygons.end());
            polygonRemoveFlags.resize(polygons.size());
            result.total_polygons_removed += removed;

            for (VertexID v : vertices_to_check_next) {
                if (v < vertexCheckNextFlags.size()) {
                    vertexCheckNextFlags[v] = 0;
                }
            }

            vertices_to_check.swap(vertices_to_check_next);

            if (pass == max_depth - 1) {
                result.iterations_executed = max_depth;
                result.hit_max_iterations  = true;
                break;
            }
        }

        return result;
    }

    /// Legacy interface
    template<typename PolygonRange>
    static size_t remove_non_manifold_polygons(PolygonRange& polygons, size_t max_depth = 10, bool verbose = false)
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
        (void)points;

        if (polygons.empty())
            return 0;

        using VertexID  = size_t;
        using PolygonID = size_t;

        // Build vertex-to-polygons map using flat buffer approach
        size_t max_vertex = 0;
        for (const auto& polygon : polygons) {
            for (VertexID v : polygon) {
                if (v > max_vertex)
                    max_vertex = v;
            }
        }

        std::vector<size_t> vertex_count(max_vertex + 1, 0);
        std::vector<size_t> vertex_offsets(max_vertex + 2);

        // Count polygons per vertex
        for (const auto& polygon : polygons) {
            if (polygon.size() != 3)
                continue;  // Only process triangles
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
            if (polygon.size() != 3)
                continue;

            for (VertexID v : polygon) {
                size_t idx            = vertex_offsets[v] + vertex_count[v]++;
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
            size_t end   = vertex_offsets[center_v + 1];
            size_t count = end - start;

            // Must have exactly 3 incident triangular faces
            if (count != 3)
                continue;

            const PolygonID* incident_polys = &vertex_poly_data[start];

            // Check if all 3 polygons are triangles
            bool all_triangles = true;
            for (size_t i = 0; i < 3; ++i) {
                if (polygons[incident_polys[i]].size() != 3) {
                    all_triangles = false;
                    break;
                }
            }
            if (!all_triangles)
                continue;

            // Collect boundary vertices in CORRECT WINDING ORDER
            // We need to preserve orientation, so extract ordered boundary loop

            // First, get all boundary vertices (6 total, 3 unique)
            std::vector<VertexID> all_boundary_verts;
            all_boundary_verts.reserve(6);

            for (size_t i = 0; i < 3; ++i) {
                const auto& tri = polygons[incident_polys[i]];
                for (VertexID v : tri) {
                    if (v != center_v) {
                        all_boundary_verts.push_back(v);
                    }
                }
            }

            // Count unique boundary vertices
            std::vector<VertexID> unique_boundary = all_boundary_verts;
            std::sort(unique_boundary.begin(), unique_boundary.end());
            auto last = std::unique(unique_boundary.begin(), unique_boundary.end());
            unique_boundary.erase(last, unique_boundary.end());

            // Must have exactly 3 unique boundary vertices (forming outer triangle)
            if (unique_boundary.size() != 3)
                continue;

            // Verify this forms a valid fan by checking edge connectivity
            // Each boundary vertex should appear in exactly 2 of the 3 triangles
            bool valid_fan = true;
            for (VertexID bv : unique_boundary) {
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

            if (!valid_fan)
                continue;

            // Valid 3-face fan found!
            // Mark the 3 triangles for removal
            for (size_t i = 0; i < 3; ++i) {
                polygon_to_remove[incident_polys[i]] = true;
            }

            // Mark center vertex for removal
            vertex_to_remove[center_v] = true;

            // Create replacement triangle with CORRECT WINDING ORDER
            // Extract boundary edge loop from first triangle to preserve orientation
            std::vector<size_t> boundary_ordered;
            boundary_ordered.reserve(3);

            // Start with first triangle and extract boundary vertices in order
            const auto& first_tri = polygons[incident_polys[0]];
            for (size_t j = 0; j < 3; ++j) {
                if (first_tri[j] != center_v) {
                    VertexID v1 = first_tri[j];
                    VertexID v2 = first_tri[(j + 1) % 3];

                    // If this edge is on the boundary (not shared with center)
                    if (v2 != center_v) {
                        // Found boundary edge: v1 -> v2
                        boundary_ordered.push_back(v1);
                        boundary_ordered.push_back(v2);

                        // Find the third boundary vertex (not v1, not v2, not center)
                        for (VertexID v : unique_boundary) {
                            if (v != v1 && v != v2) {
                                boundary_ordered.push_back(v);
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            // Fallback: if ordered extraction failed, use sorted (preserves old behavior)
            if (boundary_ordered.size() != 3) {
                boundary_ordered = unique_boundary;
            }

            std::vector<size_t> new_tri;
            new_tri.reserve(3);
            new_tri.push_back(boundary_ordered[0]);
            new_tri.push_back(boundary_ordered[1]);
            new_tri.push_back(boundary_ordered[2]);
            new_triangles.push_back(std::move(new_tri));

            fans_found++;
        }

        if (fans_found == 0)
            return 0;

        // Remove marked polygons
        auto poly_end = std::remove_if(polygons.begin(), polygons.end(),
                                       [&polygon_to_remove, idx = size_t(0)](const auto&) mutable {
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
    /// Optimized version that takes array pointer instead of vector
    template<typename PolygonRange>
    static bool is_single_umbrella_array(size_t vertex, const size_t* incident_polys, size_t num_polys,
                                         const PolygonRange& polygons)
    {
        if (num_polys < 2)
            return true;

        constexpr size_t STACK_CAPACITY          = 16;
        constexpr size_t STACK_NEIGHBOR_CAPACITY = STACK_CAPACITY * 4;  // 2 neighbors per polygon
        constexpr size_t SMALL_NEIGHBOR_CAPACITY = 64;                  // sorted small-buffer path (neighbor,poly)
        const size_t invalid                     = std::numeric_limits<size_t>::max();
        const bool use_stack                     = num_polys <= STACK_CAPACITY;

        // thread_local heap scratch to avoid allocations on hot path
        struct Scratch {
            std::vector<size_t> parents;
            std::vector<uint8_t> ranks;
            std::vector<size_t> neighborKeys;
            std::vector<size_t> neighborValues;
            std::vector<uint32_t> neighborMarks;
            uint32_t generation = 1;
        };
        static thread_local Scratch scratch;

        size_t parent_stack[STACK_CAPACITY];
        uint8_t rank_stack[STACK_CAPACITY];
        size_t neighbor_keys_stack[STACK_NEIGHBOR_CAPACITY];
        size_t neighbor_values_stack[STACK_NEIGHBOR_CAPACITY];
        uint8_t neighbor_used_stack[STACK_NEIGHBOR_CAPACITY];
        std::pair<size_t, size_t> neighbor_pairs_stack[SMALL_NEIGHBOR_CAPACITY];

        size_t* parents;
        uint8_t* ranks;
        size_t* neighbor_keys;
        size_t* neighbor_values;
        uint32_t* neighbor_marks = nullptr;
        size_t neighbor_capacity;
        uint32_t mark_value = 1;

        if (use_stack) {
            for (size_t i = 0; i < num_polys; ++i) {
                parent_stack[i] = i;
                rank_stack[i]   = 0;
            }
            std::fill_n(neighbor_used_stack, STACK_NEIGHBOR_CAPACITY, 0);
            parents           = parent_stack;
            ranks             = rank_stack;
            neighbor_keys     = neighbor_keys_stack;
            neighbor_values   = neighbor_values_stack;
            neighbor_capacity = STACK_NEIGHBOR_CAPACITY;
        } else {
            if (scratch.parents.size() < num_polys) {
                scratch.parents.resize(num_polys);
            }
            if (scratch.ranks.size() < num_polys) {
                scratch.ranks.resize(num_polys);
            }
            for (size_t i = 0; i < num_polys; ++i) {
                scratch.parents[i] = i;
                scratch.ranks[i]   = 0;
            }

            auto nextPow2 = [](size_t v) {
                size_t power = 1;
                while (power < v) {
                    power <<= 1;
                }
                return power;
            };

            neighbor_capacity = nextPow2(std::max<size_t>(STACK_NEIGHBOR_CAPACITY, num_polys * 4));
            if (scratch.neighborKeys.size() < neighbor_capacity) {
                scratch.neighborKeys.assign(neighbor_capacity, invalid);
                scratch.neighborValues.resize(neighbor_capacity);
                scratch.neighborMarks.assign(neighbor_capacity, 0);
            } else {
                scratch.neighborKeys.resize(neighbor_capacity);
                scratch.neighborValues.resize(neighbor_capacity);
                scratch.neighborMarks.resize(neighbor_capacity);
            }

            mark_value = ++scratch.generation;
            if (mark_value == 0) {
                std::fill(scratch.neighborMarks.begin(), scratch.neighborMarks.end(), 0);
                scratch.generation = 1;
                mark_value         = 1;
            }

            parents         = scratch.parents.data();
            ranks           = scratch.ranks.data();
            neighbor_keys   = scratch.neighborKeys.data();
            neighbor_values = scratch.neighborValues.data();
            neighbor_marks  = scratch.neighborMarks.data();
        }

        auto hashNeighbor = [](size_t v) -> size_t { return v * 11400714819323198485ull; };

        auto findRoot = [&](size_t idx) {
            while (parents[idx] != idx) {
                parents[idx] = parents[parents[idx]];
                idx          = parents[idx];
            }
            return idx;
        };

        auto unite = [&](size_t a, size_t b) {
            size_t rootA = findRoot(a);
            size_t rootB = findRoot(b);
            if (rootA == rootB)
                return;
            if (ranks[rootA] < ranks[rootB]) {
                std::swap(rootA, rootB);
            }
            parents[rootB] = rootA;
            if (ranks[rootA] == ranks[rootB]) {
                ranks[rootA]++;
            }
        };

        const size_t mask   = neighbor_capacity - 1;
        bool has_connection = false;

        auto connectNeighborHash = [&](size_t neighbor, size_t poly_idx) {
            size_t slot = hashNeighbor(neighbor) & mask;
            while (true) {
                if (use_stack) {
                    if (!neighbor_used_stack[slot]) {
                        neighbor_used_stack[slot] = 1;
                        neighbor_keys[slot]       = neighbor;
                        neighbor_values[slot]     = poly_idx;
                        return;
                    }
                    if (neighbor_keys[slot] == neighbor) {
                        size_t other = neighbor_values[slot];
                        if (other != poly_idx) {
                            unite(other, poly_idx);
                            has_connection = true;
                        }
                        return;
                    }
                } else {
                    if (neighbor_marks[slot] != mark_value) {
                        neighbor_marks[slot]  = mark_value;
                        neighbor_keys[slot]   = neighbor;
                        neighbor_values[slot] = poly_idx;
                        return;
                    }
                    if (neighbor_keys[slot] == neighbor) {
                        size_t other = neighbor_values[slot];
                        if (other != poly_idx) {
                            unite(other, poly_idx);
                            has_connection = true;
                        }
                        return;
                    }
                }

                slot = (slot + 1) & mask;
            }
        };

        auto find_vertex_pos = [&](const auto& polygon) -> size_t {
            const size_t n = polygon.size();
            if (n == 3) {
                if (polygon[0] == vertex)
                    return 0;
                if (polygon[1] == vertex)
                    return 1;
                if (polygon[2] == vertex)
                    return 2;
                return n;
            }
            for (size_t i = 0; i < n; ++i) {
                if (polygon[i] == vertex)
                    return i;
            }
            return n;
        };

        const bool use_sorted_neighbors = (num_polys * 2) <= SMALL_NEIGHBOR_CAPACITY;
        size_t neighbor_pair_count      = 0;

        for (size_t poly_idx = 0; poly_idx < num_polys; ++poly_idx) {
            const auto& polygon    = polygons[incident_polys[poly_idx]];
            const size_t num_verts = polygon.size();
            if (num_verts < 2) {
                return false;
            }

            size_t position = find_vertex_pos(polygon);
            if (position == num_verts) {
                return false;
            }

            size_t prev_v = polygon[(position + num_verts - 1) % num_verts];
            size_t next_v = polygon[(position + 1) % num_verts];

            if (use_sorted_neighbors) {
                if (neighbor_pair_count + 2 <= SMALL_NEIGHBOR_CAPACITY) {
                    neighbor_pairs_stack[neighbor_pair_count++] = { prev_v, poly_idx };
                    neighbor_pairs_stack[neighbor_pair_count++] = { next_v, poly_idx };
                }
            } else {
                connectNeighborHash(prev_v, poly_idx);
                connectNeighborHash(next_v, poly_idx);
            }
        }

        if (use_sorted_neighbors) {
            auto begin = neighbor_pairs_stack;
            auto end   = neighbor_pairs_stack + neighbor_pair_count;
            if (neighbor_pair_count > 1) {
                std::sort(begin, end);  // std::pair lexicographic order
            }

            size_t i = 0;
            while (i < neighbor_pair_count) {
                size_t j = i + 1;
                while (j < neighbor_pair_count && neighbor_pairs_stack[j].first == neighbor_pairs_stack[i].first) {
                    ++j;
                }
                if (j - i > 1) {
                    // Union all polys sharing this neighbor
                    size_t base_poly = neighbor_pairs_stack[i].second;
                    for (size_t k = i + 1; k < j; ++k) {
                        unite(base_poly, neighbor_pairs_stack[k].second);
                    }
                    has_connection = true;
                }
                i = j;
            }

            if (!has_connection) {
                return false;
            }
        }

        if (!has_connection) {
            return false;
        }

        size_t root0 = findRoot(0);
        for (size_t i = 1; i < num_polys; ++i) {
            if (findRoot(i) != root0) {
                return false;
            }
        }

        return true;
    }
};

}  // namespace MeshRepair
