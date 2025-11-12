#pragma once

#include "types.h"
#include "hole_detector.h"
#include "config.h"

namespace MeshRepair {

/**
 * @brief Configuration options for hole filling algorithm
 *
 * Based on Liepa 2003 "Filling Holes in Meshes" with Laplacian fairing.
 */
struct FillingOptions {
    // Fairing parameters (Laplacian smoothing)
    unsigned int fairing_continuity = Config::DEFAULT_FAIRING_CONTINUITY;  // 0=C0, 1=C1, 2=C2

    // Size limits to prevent processing excessively large holes
    size_t max_hole_boundary_vertices = Config::DEFAULT_MAX_HOLE_BOUNDARY;
    double max_hole_diameter_ratio = Config::DEFAULT_MAX_HOLE_DIAMETER_RATIO;

    // Algorithm preferences
    bool use_2d_cdt = Config::DEFAULT_USE_2D_CDT;              // Try 2D constrained Delaunay first
    bool use_3d_delaunay = Config::DEFAULT_USE_3D_DELAUNAY;    // Fallback to 3D Delaunay
    bool skip_cubic_search = Config::DEFAULT_SKIP_CUBIC;       // Skip exhaustive cubic search

    // Refinement
    bool refine = Config::DEFAULT_REFINE;                      // Refine patch to match local density

    // Output verbosity
    bool verbose = false;
    bool show_progress = true;
};

/**
 * @brief Mesh hole filling using CGAL's triangulate-refine-and-fair algorithm
 *
 * Implements:
 * - Constrained Delaunay triangulation (2D/3D)
 * - Mesh refinement for density matching
 * - Laplacian (harmonic) fairing for smooth blending
 *
 * References:
 * [1] Peter Liepa. "Filling Holes in Meshes." Eurographics Symposium
 *     on Geometry Processing, 2003.
 * [2] Mario Botsch et al. "On Linear Variational Surface Deformation Methods."
 *     IEEE TVCG, 2008.
 */
class HoleFiller {
public:
    /**
     * @brief Construct hole filler for a mesh
     * @param mesh Mesh to modify (will be updated in-place)
     * @param options Filling algorithm configuration
     */
    explicit HoleFiller(Mesh& mesh, const FillingOptions& options = FillingOptions{});

    /**
     * @brief Fill a single hole
     * @param hole Hole information from HoleDetector
     * @return Statistics about the filling operation
     */
    HoleStatistics fill_hole(const HoleInfo& hole);

    /**
     * @brief Fill all detected holes in the mesh
     * @param holes Vector of holes to fill
     * @return Overall statistics for all operations
     */
    MeshStatistics fill_all_holes(const std::vector<HoleInfo>& holes);

    /**
     * @brief Get current filling options
     * @return Current options
     */
    const FillingOptions& get_options() const { return options_; }

    /**
     * @brief Update filling options
     * @param options New options
     */
    void set_options(const FillingOptions& options) { options_ = options; }

private:
    Mesh& mesh_;
    FillingOptions options_;

    // Helper methods
    bool should_skip_hole(const HoleInfo& hole) const;
    double compute_mesh_bbox_diagonal() const;
    void report_progress(size_t current, size_t total, const std::string& message) const;
};

} // namespace MeshRepair
