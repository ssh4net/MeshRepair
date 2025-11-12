#pragma once

#include "types.h"
#include <vector>

namespace MeshRepair {

/**
 * @brief Information about a detected hole in the mesh
 */
struct HoleInfo {
    halfedge_descriptor boundary_halfedge;      // Starting halfedge on hole boundary
    std::vector<vertex_descriptor> boundary_vertices;
    size_t boundary_size = 0;
    double estimated_diameter = 0.0;
    double estimated_area = 0.0;
};

/**
 * @brief Hole detection and analysis
 *
 * Detects holes in triangle meshes by finding boundary cycles.
 * A hole is defined as a sequence of border halfedges forming a closed loop.
 */
class HoleDetector {
public:
    /**
     * @brief Construct hole detector for a mesh
     * @param mesh Mesh to analyze
     * @param verbose Enable verbose output
     */
    explicit HoleDetector(const Mesh& mesh, bool verbose = false);

    /**
     * @brief Detect all holes in the mesh
     * @return Vector of detected holes with analysis
     */
    std::vector<HoleInfo> detect_all_holes();

    /**
     * @brief Check if a halfedge is on a hole boundary
     * @param mesh Mesh to check
     * @param h Halfedge to test
     * @return true if halfedge is on boundary
     */
    static bool is_border_halfedge(const Mesh& mesh, halfedge_descriptor h);

    /**
     * @brief Analyze a hole boundary
     * @param mesh Mesh containing the hole
     * @param border_h Halfedge on hole boundary
     * @return Hole information and statistics
     */
    static HoleInfo analyze_hole(const Mesh& mesh, halfedge_descriptor border_h);

    /**
     * @brief Get total number of border edges in mesh
     * @param mesh Mesh to analyze
     * @return Number of border edges
     */
    static size_t count_border_edges(const Mesh& mesh);

private:
    const Mesh& mesh_;
    bool verbose_;
};

} // namespace MeshRepair
