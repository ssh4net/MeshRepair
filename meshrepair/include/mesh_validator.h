#pragma once

#include "types.h"
#include <string>

namespace MeshRepair {

/**
 * @brief Mesh validation and quality checking
 */
class MeshValidator {
public:
    /**
     * @brief Check if mesh is valid
     * @param mesh Mesh to validate
     * @return true if mesh passes all validity checks
     */
    static bool is_valid(const Mesh& mesh);

    /**
     * @brief Check if mesh is a triangle mesh
     * @param mesh Mesh to check
     * @return true if all faces are triangles
     */
    static bool is_triangle_mesh(const Mesh& mesh);

    /**
     * @brief Check if mesh is closed (no boundary edges)
     * @param mesh Mesh to check
     * @return true if mesh has no holes
     */
    static bool is_closed(const Mesh& mesh);

    /**
     * @brief Count number of connected components
     * @param mesh Mesh to analyze
     * @return Number of disconnected components
     */
    static size_t count_connected_components(const Mesh& mesh);

    /**
     * @brief Get mesh bounding box diagonal length
     * @param mesh Mesh to measure
     * @return Diagonal length
     */
    static double get_bbox_diagonal(const Mesh& mesh);

    /**
     * @brief Print mesh statistics
     * @param mesh Mesh to analyze
     * @param detailed Include detailed analysis
     */
    static void print_statistics(const Mesh& mesh, bool detailed = false);

    /**
     * @brief Generate validation report
     * @param mesh Mesh to validate
     * @return Human-readable validation report
     */
    static std::string generate_report(const Mesh& mesh);
};

} // namespace MeshRepair
