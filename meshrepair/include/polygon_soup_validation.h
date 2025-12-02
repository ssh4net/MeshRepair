#pragma once

#include "types.h"
#include <vector>
#include <cstddef>

namespace MeshRepair {

struct SoupValidationResult {
    size_t polygons_inspected                = 0;
    size_t polygons_removed_total            = 0;
    size_t polygons_removed_out_of_bounds    = 0;
    size_t polygons_removed_invalid_cycle    = 0;
    size_t polygons_removed_edge_orientation = 0;
    size_t polygons_removed_non_manifold     = 0;
    size_t edges_with_same_direction         = 0;
    size_t edges_overused                    = 0;
    size_t passes_executed                   = 0;
};

// Validator: removes polygons with out-of-range indices, repeated vertices,
// self-loops, edges used more than twice, edges used twice with the same direction,
// and non-manifold vertex umbrellas.
SoupValidationResult
validate_polygon_soup_basic(const std::vector<Point_3>& points, std::vector<std::vector<std::size_t>>& polygons);

}  // namespace MeshRepair
