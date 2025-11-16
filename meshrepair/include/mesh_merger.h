#pragma once

#include "types.h"
#include "submesh_extractor.h"
#include <vector>

namespace MeshRepair {

/// Merges multiple submeshes back into a single mesh
class MeshMerger {
public:
    /// Merge multiple submeshes back into the original mesh
    /// @param original_mesh The original mesh (unmodified regions will be preserved)
    /// @param submeshes Vector of filled submeshes to merge
    /// @param verbose Print progress information
    /// @return Merged mesh
    static Mesh merge_submeshes(const Mesh& original_mesh, const std::vector<Submesh>& submeshes, bool verbose = false);

private:
    /// Intermediate polygon soup representation
    struct PolygonSoup {
        std::vector<Point_3> points;
        std::vector<std::vector<std::size_t>> polygons;

        PolygonSoup() = default;
    };

    /// Convert mesh to polygon soup
    /// @param mesh The mesh to convert
    /// @return Polygon soup representation
    static PolygonSoup to_soup(const Mesh& mesh);

    /// Convert polygon soup to mesh
    /// @param soup The polygon soup
    /// @return Reconstructed mesh
    static Mesh soup_to_mesh(const PolygonSoup& soup);
};

}  // namespace MeshRepair
