#ifndef MESHREPAIR_MESH_PREPROCESSOR_H
#define MESHREPAIR_MESH_PREPROCESSOR_H

#include "types.h"
#include <string>
#include <vector>

namespace MeshRepair {

// Forward declaration
class ThreadPool;
struct PolygonSoup;  // Forward declaration from mesh_loader.h

struct PreprocessingStats {
    size_t duplicates_merged             = 0;
    size_t non_manifold_vertices_removed = 0;  // Non-manifold polygons removed in soup space
    size_t face_fans_collapsed           = 0;  // 3-face fans collapsed
    size_t long_edge_polygons_removed    = 0;  // Polygons removed due to long edges
    size_t isolated_vertices_removed     = 0;
    size_t connected_components_found    = 0;  // Number of connected components found
    size_t small_components_removed      = 0;  // Number of small components removed
    double total_time_ms                 = 0.0;

    // Detailed timing breakdown
    double soup_cleanup_time_ms = 0.0;  // Time in soup space (duplicates, non-manifold, etc.)
    double duplicates_time_ms   = 0.0;
    double degenerate_time_ms   = 0.0;
    double non_manifold_time_ms = 0.0;
    double long_edge_time_ms    = 0.0;
    double face_fans_time_ms    = 0.0;
    double orient_time_ms       = 0.0;
    double soup_to_mesh_time_ms = 0.0;  // Time converting soup to mesh
    double mesh_cleanup_time_ms = 0.0;  // Time in mesh space (isolated vertices, components)

    bool has_changes() const
    {
        return duplicates_merged > 0 || non_manifold_vertices_removed > 0 || face_fans_collapsed > 0
               || long_edge_polygons_removed > 0 || isolated_vertices_removed > 0 || small_components_removed > 0;
    }
};

struct PreprocessingOptions {
    bool remove_duplicates      = true;
    bool remove_non_manifold    = true;
    bool remove_3_face_fans     = true;  // Remove 3-triangle fans around single vertex
    bool remove_isolated        = true;
    bool keep_largest_component = true;  // Keep only largest connected component
    bool remove_long_edges      = false; // Remove polygons with overly long edges (disabled by default)
    double long_edge_max_ratio  = 0.125; // Edge length limit as fraction of mesh bbox diagonal
    size_t non_manifold_passes  = 10;    // Max recursion depth for local search (not global passes!)
    bool verbose                = false;
    bool debug                  = false;  // Dump intermediate meshes as binary PLY
};

class MeshPreprocessor {
public:
    MeshPreprocessor(Mesh& mesh, const PreprocessingOptions& options = PreprocessingOptions());

    // Run preprocessing on polygon soup directly (RECOMMENDED - avoids mesh->soup extraction)
    // Takes soup as input, modifies in place, converts to mesh at end
    static PreprocessingStats preprocess_soup(PolygonSoup& soup, Mesh& output_mesh,
                                              const PreprocessingOptions& options = PreprocessingOptions());

    static void plyDump(MeshRepair::PolygonSoup& soup, const std::string debug_file, const std::string message,
                        bool verbose);

    // Individual preprocessing operations (mesh-level only)
    size_t remove_isolated_vertices();
    size_t keep_only_largest_connected_component();

    // Get last preprocessing statistics
    const PreprocessingStats& get_stats() const { return stats_; }

    // Print preprocessing report
    void print_report() const;

    // Set thread pool for parallel operations (optional)
    void set_thread_pool(ThreadPool* pool) { thread_pool_ = pool; }

private:
    Mesh& mesh_;
    PreprocessingOptions options_;
    PreprocessingStats stats_;
    ThreadPool* thread_pool_ = nullptr;  // Optional thread pool
};

// C-style helpers (status + out params, 0 on success)
int
preprocess_soup_c(PolygonSoup* soup, Mesh* out_mesh, const PreprocessingOptions* options,
                  PreprocessingStats* out_stats);

}  // namespace MeshRepair

#endif  // MESHREPAIR_MESH_PREPROCESSOR_H
