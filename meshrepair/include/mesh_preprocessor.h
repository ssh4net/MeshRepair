#ifndef MESHREPAIR_MESH_PREPROCESSOR_H
#define MESHREPAIR_MESH_PREPROCESSOR_H

#include "types.h"
#include <string>

namespace MeshRepair {

struct PreprocessingStats {
    size_t duplicates_merged = 0;
    size_t non_manifold_vertices_found = 0;
    size_t non_manifold_vertices_removed = 0;  // Total non-manifold vertices removed across all passes
    size_t non_manifold_passes_executed = 0;   // Number of passes actually executed
    size_t isolated_vertices_removed = 0;
    double total_time_ms = 0.0;

    bool has_changes() const {
        return duplicates_merged > 0 ||
               non_manifold_vertices_removed > 0 ||
               isolated_vertices_removed > 0;
    }
};

struct PreprocessingOptions {
    bool remove_duplicates = true;
    bool remove_non_manifold = true;
    bool remove_isolated = true;
    size_t non_manifold_passes = 2;  // Number of passes for non-manifold removal
    bool verbose = false;
    bool debug = false;  // Dump intermediate meshes as binary PLY
};

class MeshPreprocessor {
public:
    MeshPreprocessor(Mesh& mesh, const PreprocessingOptions& options = PreprocessingOptions());

    // Run all enabled preprocessing steps
    PreprocessingStats preprocess();

    // Individual preprocessing operations
    size_t remove_duplicate_vertices();
    size_t remove_non_manifold_vertices();
    size_t remove_isolated_vertices();

    // Get last preprocessing statistics
    const PreprocessingStats& get_stats() const { return stats_; }

    // Print preprocessing report
    void print_report() const;

private:
    Mesh& mesh_;
    PreprocessingOptions options_;
    PreprocessingStats stats_;
};

} // namespace MeshRepair

#endif // MESHREPAIR_MESH_PREPROCESSOR_H
