#include "polygon_soup_validation.h"
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace MeshRepair {

SoupValidationResult
validate_polygon_soup_basic(const std::vector<Point_3>& points, std::vector<std::vector<std::size_t>>& polygons)
{
    SoupValidationResult result;

    const std::size_t point_count = points.size();
    if (polygons.empty()) {
        return result;
    }

    struct EdgeEntry {
        std::size_t u;
        std::size_t v;
        std::size_t poly_id;
        bool forward;
    };

    // Scratch buffers reused across passes
    std::vector<uint8_t> remove_flags;
    std::vector<EdgeEntry> edge_entries;
    std::vector<std::vector<std::size_t>> vertex_to_polys(point_count);
    std::vector<std::size_t> active_vertices;

    const std::size_t max_passes = 5;
    for (std::size_t pass = 0; pass < max_passes; ++pass) {
        remove_flags.assign(polygons.size(), 0);

        // Clear only buckets touched in previous pass
        for (std::size_t v : active_vertices) {
            if (v < vertex_to_polys.size()) {
                vertex_to_polys[v].clear();
            }
        }
        active_vertices.clear();
        edge_entries.clear();
        edge_entries.reserve(polygons.size() * 3);

#if false
        // Full face/edge validation (disabled: keeping only non-manifold umbrellas for now)
        for (std::size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
            auto& poly = polygons[poly_id];
            result.polygons_inspected++;

            bool remove_poly = false;

            if (poly.size() < 3) {
                remove_poly = true;
                result.polygons_removed_invalid_cycle++;
            }

            if (!remove_poly) {
                for (std::size_t idx : poly) {
                    if (idx >= point_count) {
                        remove_poly = true;
                        result.polygons_removed_out_of_bounds++;
                        break;
                    }
                    if (vertex_marks[idx] == generation) {
                        remove_poly = true;
                        result.polygons_removed_invalid_cycle++;
                        break;
                    }
                    vertex_marks[idx] = generation;
                }

                ++generation;
                if (generation == 0) {
                    std::fill(vertex_marks.begin(), vertex_marks.end(), 0);
                    generation = 1;
                }
            }

            if (!remove_poly) {
                const std::size_t n = poly.size();
                for (std::size_t i = 0; i < n; ++i) {
                    std::size_t a = poly[i];
                    std::size_t b = poly[(i + 1) % n];
                    if (a == b) {
                        remove_poly = true;
                        result.polygons_removed_invalid_cycle++;
                        break;
                    }
                }
            }

            if (remove_poly) {
                remove_flags[poly_id] = 1;
                continue;
            }

            const std::size_t n = poly.size();
            for (std::size_t i = 0; i < n; ++i) {
                std::size_t a = poly[i];
                std::size_t b = poly[(i + 1) % n];

                std::size_t u = a < b ? a : b;
                std::size_t v = a < b ? b : a;
                bool forward  = (a == u);

                edge_entries.push_back({ u, v, poly_id, forward });
            }

            // Build vertex -> polygons map (sparse clear)
            for (std::size_t v : poly) {
                if (v >= vertex_to_polys.size()) {
                    continue;
                }
                auto& bucket = vertex_to_polys[v];
                if (bucket.empty()) {
                    active_vertices.push_back(v);
                }
                bucket.push_back(poly_id);
            }
        }
#else
        // Minimal path: only build vertex -> polygon adjacency for non-manifold detection
        for (std::size_t poly_id = 0; poly_id < polygons.size(); ++poly_id) {
            const auto& poly = polygons[poly_id];
            for (std::size_t v : poly) {
                if (v >= vertex_to_polys.size()) {
                    continue;
                }
                auto& bucket = vertex_to_polys[v];
                if (bucket.empty()) {
                    active_vertices.push_back(v);
                }
                bucket.push_back(poly_id);
            }
        }
#endif

        auto mark_for_removal = [&](std::size_t poly_id, std::size_t& counter) {
            if (poly_id < remove_flags.size() && !remove_flags[poly_id]) {
                remove_flags[poly_id] = 1;
                counter++;
            }
        };

#if false
        // Edge orientation/use checks using sorted edge list (disabled for now)
        std::sort(edge_entries.begin(), edge_entries.end(), [](const EdgeEntry& a, const EdgeEntry& b) {
            if (a.u == b.u) {
                if (a.v == b.v)
                    return a.poly_id < b.poly_id;
                return a.v < b.v;
            }
            return a.u < b.u;
        });

        for (std::size_t i = 0; i < edge_entries.size();) {
            const std::size_t start = i;
            const std::size_t u     = edge_entries[i].u;
            const std::size_t v     = edge_entries[i].v;

            std::size_t fwd = 0;
            std::size_t bwd = 0;
            while (i < edge_entries.size() && edge_entries[i].u == u && edge_entries[i].v == v) {
                if (edge_entries[i].forward)
                    ++fwd;
                else
                    ++bwd;
                ++i;
            }

            const std::size_t count = i - start;
            if (count > 2) {
                result.edges_overused++;
                for (std::size_t idx = start; idx < i; ++idx) {
                    mark_for_removal(edge_entries[idx].poly_id, result.polygons_removed_edge_orientation);
                }
                continue;
            }

            if (count == 2 && (fwd == 2 || bwd == 2)) {
                result.edges_with_same_direction++;
                for (std::size_t idx = start; idx < i; ++idx) {
                    mark_for_removal(edge_entries[idx].poly_id, result.polygons_removed_edge_orientation);
                }
            }
        }
#else
        (void)edge_entries;
#endif

        // Vertex manifold umbrella checks
        for (std::size_t v : active_vertices) {
            const auto& incident = vertex_to_polys[v];
            if (incident.size() < 2)
                continue;

            // Union-find over incident polygons using sorted neighbor pairs (no per-vertex hash map)
            const std::size_t local_count = incident.size();
            std::vector<std::size_t> parents(local_count);
            std::iota(parents.begin(), parents.end(), 0);

            auto find_root = [&parents](std::size_t idx) {
                while (parents[idx] != idx) {
                    parents[idx] = parents[parents[idx]];
                    idx          = parents[idx];
                }
                return idx;
            };

            auto unite = [&parents, &find_root](std::size_t a, std::size_t b) {
                std::size_t ra = find_root(a);
                std::size_t rb = find_root(b);
                if (ra != rb) {
                    parents[rb] = ra;
                }
            };

            std::vector<std::pair<std::size_t, std::size_t>> neighbor_pairs;
            neighbor_pairs.reserve(local_count * 2);

            bool invalid = false;
            for (std::size_t slot = 0; slot < local_count; ++slot) {
                const auto& poly = polygons[incident[slot]];
                const auto it    = std::find(poly.begin(), poly.end(), v);
                if (it == poly.end()) {
                    invalid = true;
                    break;
                }
                const std::size_t num_verts = poly.size();
                if (num_verts < 3) {
                    invalid = true;
                    break;
                }

                const std::size_t idx  = static_cast<std::size_t>(std::distance(poly.begin(), it));
                const std::size_t prev = poly[(idx + num_verts - 1) % num_verts];
                const std::size_t next = poly[(idx + 1) % num_verts];

                if (prev == next) {
                    invalid = true;
                    break;
                }

                neighbor_pairs.emplace_back(prev, slot);
                neighbor_pairs.emplace_back(next, slot);
            }

            if (invalid) {
                for (std::size_t poly_id : incident) {
                    mark_for_removal(poly_id, result.polygons_removed_non_manifold);
                }
                continue;
            }

            std::sort(neighbor_pairs.begin(), neighbor_pairs.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            bool has_connection = false;
            for (std::size_t i = 0; i < neighbor_pairs.size();) {
                std::size_t j = i + 1;
                while (j < neighbor_pairs.size() && neighbor_pairs[j].first == neighbor_pairs[i].first) {
                    ++j;
                }
                std::size_t span = j - i;
                if (span > 2) {
                    invalid = true;
                    break;
                }
                if (span == 2) {
                    unite(neighbor_pairs[i].second, neighbor_pairs[i + 1].second);
                    has_connection = true;
                }
                i = j;
            }

            if (invalid || !has_connection) {
                for (std::size_t poly_id : incident) {
                    mark_for_removal(poly_id, result.polygons_removed_non_manifold);
                }
                continue;
            }

            const std::size_t root0 = find_root(0);
            bool single_fan         = true;
            for (std::size_t k = 1; k < local_count; ++k) {
                if (find_root(k) != root0) {
                    single_fan = false;
                    break;
                }
            }

            if (!single_fan) {
                for (std::size_t poly_id : incident) {
                    mark_for_removal(poly_id, result.polygons_removed_non_manifold);
                }
            }
        }

        auto new_end = std::remove_if(polygons.begin(), polygons.end(),
                                      [&remove_flags, idx = std::size_t(0)](const auto&) mutable {
                                          return idx < remove_flags.size() && remove_flags[idx++];
                                      });

        const std::size_t removed_this_pass = static_cast<std::size_t>(std::distance(new_end, polygons.end()));
        polygons.erase(new_end, polygons.end());
        result.polygons_removed_total += removed_this_pass;
        result.passes_executed++;

        if (removed_this_pass == 0) {
            break;
        }
    }

    return result;
}

}  // namespace MeshRepair
