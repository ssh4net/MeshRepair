#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace MeshRepair {

// Polygon soup structure (points + polygons)
struct PolygonSoup {
    std::vector<Point_3> points;
    std::vector<std::vector<std::size_t>> polygons;
    double load_time_ms = 0.0;  // Time to load from file
};

// C-style loader namespace (no heap allocations beyond mesh/soup data)
namespace MeshLoader {
    enum class Format {
        OBJ,   // Wavefront OBJ format
        PLY,   // Stanford PLY format
        OFF,   // Object File Format (CGAL native)
        AUTO,  // Auto-detect from extension
    };

    // Validate file exists and is readable
    bool validate_input_file(const std::string& filename);

    // Loaders (return false on failure, populate last_error())  -----------------
    bool load_mesh(const std::string& filename, Format format, bool force_cgal_loader, Mesh* out_mesh);
    bool load_soup(const std::string& filename, Format format, bool force_cgal_loader, PolygonSoup* out_soup);

    // Saver ---------------------------------------------------------------------
    bool save_mesh(const Mesh& mesh, const std::string& filename, Format format, bool binary_ply);

    // Error reporting -----------------------------------------------------------
    const std::string& last_error();
}  // namespace MeshLoader

// C-style loader APIs (status + out params, 0 on success)
int
mesh_loader_load(const char* filename, MeshLoader::Format format, bool force_cgal_loader, Mesh* out_mesh);
int
mesh_loader_load_soup(const char* filename, MeshLoader::Format format, bool force_cgal_loader, PolygonSoup* out_soup);
int
mesh_loader_save(const Mesh& mesh, const char* filename, MeshLoader::Format format, bool binary_ply);
const char*
mesh_loader_last_error();

}  // namespace MeshRepair
