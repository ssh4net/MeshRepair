#include "mesh_loader.h"
#include <CGAL/IO/polygon_mesh_io.h>
#include <CGAL/IO/PLY.h>
#include <CGAL/IO/OBJ.h>
#include <CGAL/IO/OFF.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>

#ifdef HAVE_RAPIDOBJ
#    include <rapidobj/rapidobj.hpp>
#endif

namespace fs = std::filesystem;

namespace MeshRepair {

std::string MeshLoader::last_error_;

#ifdef HAVE_RAPIDOBJ
std::optional<Mesh>
MeshLoader::load_obj_rapidobj(const std::string& filename)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    // ========== PHASE 1: Parse OBJ with RapidOBJ ==========
    // Ignore materials - we only need geometry for hole filling
    auto result = rapidobj::ParseFile(filename, rapidobj::MaterialLibrary::Ignore());

    if (result.error) {
        last_error_ = "RapidOBJ parse error: " + result.error.code.message() + " at line "
                      + std::to_string(result.error.line_num);
        if (!result.error.line.empty()) {
            last_error_ += " [" + result.error.line + "]";
        }
        return std::nullopt;
    }

    auto parse_time = std::chrono::high_resolution_clock::now();

    // ========== PHASE 2: Triangulate (convert quads to triangles) ==========
    if (!rapidobj::Triangulate(result)) {
        last_error_ = "RapidOBJ triangulation failed";
        return std::nullopt;
    }

    auto triangulate_time = std::chrono::high_resolution_clock::now();

    // ========== PHASE 3: Count geometry ==========
    const auto& positions = result.attributes.positions;
    size_t num_vertices   = positions.size() / 3;

    size_t num_faces = 0;
    for (const auto& shape : result.shapes) {
        num_faces += shape.mesh.num_face_vertices.size();
    }

    if (num_vertices == 0) {
        last_error_ = "Mesh has no vertices";
        return std::nullopt;
    }

    if (num_faces == 0) {
        last_error_ = "Mesh has no faces";
        return std::nullopt;
    }

    // ========== PHASE 4: Pre-allocate CGAL mesh ==========
    Mesh mesh;

    // Reserve capacity (avoids reallocation)
    // Conservative estimate: faces * 3 edges (actual ~1.5x faces for manifold mesh)
    mesh.reserve(num_vertices, num_faces * 3, num_faces);

    // ========== PHASE 5: Add vertices ==========
    std::vector<Mesh::Vertex_index> vertex_map(num_vertices);

    for (size_t i = 0; i < num_vertices; ++i) {
        size_t idx = i * 3;
        Point_3 p(static_cast<double>(positions[idx + 0]), static_cast<double>(positions[idx + 1]),
                  static_cast<double>(positions[idx + 2]));
        vertex_map[i] = mesh.add_vertex(p);
    }

    auto vertices_time = std::chrono::high_resolution_clock::now();

    // ========== PHASE 6: Release RapidOBJ memory early ==========
    // Keep only shapes (indices), release positions array
    auto shapes = std::move(result.shapes);
    result      = {};  // Free positions, normals, texcoords

    // ========== PHASE 7: Add faces ==========
    size_t faces_added  = 0;
    size_t faces_failed = 0;

    for (const auto& shape : shapes) {
        const auto& shape_mesh = shape.mesh;
        size_t idx             = 0;

        for (size_t f = 0; f < shape_mesh.num_face_vertices.size(); ++f) {
            uint8_t num_verts = shape_mesh.num_face_vertices[f];

            if (num_verts == 3) {
                auto v0 = vertex_map[shape_mesh.indices[idx + 0].position_index];
                auto v1 = vertex_map[shape_mesh.indices[idx + 1].position_index];
                auto v2 = vertex_map[shape_mesh.indices[idx + 2].position_index];

                if (mesh.add_face(v0, v1, v2) != Mesh::null_face()) {
                    ++faces_added;
                } else {
                    ++faces_failed;
                }
            } else {
                // Should not happen after triangulation
                ++faces_failed;
            }

            idx += num_verts;
        }
    }

    auto faces_time = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration<double, std::milli>(faces_time - start_time).count();

    // ========== PHASE 8: Report ==========
    std::cout << "Loaded mesh from: " << filename << "\n"
              << "  Vertices: " << mesh.number_of_vertices() << "\n"
              << "  Faces: " << mesh.number_of_faces() << " (added: " << faces_added;

    if (faces_failed > 0) {
        std::cout << ", failed: " << faces_failed;
    }

    std::cout << ")\n"
              << "  Edges: " << mesh.number_of_edges() << "\n"
              << "  Timing:\n"
              << "    Parse: " << std::chrono::duration<double, std::milli>(parse_time - start_time).count() << " ms\n"
              << "    Triangulate: " << std::chrono::duration<double, std::milli>(triangulate_time - parse_time).count()
              << " ms\n"
              << "    Add vertices: "
              << std::chrono::duration<double, std::milli>(vertices_time - triangulate_time).count() << " ms\n"
              << "    Add faces: " << std::chrono::duration<double, std::milli>(faces_time - vertices_time).count()
              << " ms\n"
              << "    Total: " << total_time << " ms\n";

    if (faces_failed > 0) {
        std::cerr << "Warning: " << faces_failed << " faces failed to add (non-manifold or duplicate)\n";
    }

    return mesh;
}
#endif  // HAVE_RAPIDOBJ

std::optional<Mesh>
MeshLoader::load(const std::string& filename, Format format, bool force_cgal_loader)
{
    last_error_.clear();

    // Check file exists
    if (!validate_input_file(filename)) {
        last_error_ = "File not found or not readable: " + filename;
        return std::nullopt;
    }

    // Auto-detect format if needed
    if (format == Format::AUTO) {
        format = detect_format(filename);
    }

#ifdef HAVE_RAPIDOBJ
    // Use RapidOBJ for OBJ files (10-50x faster than CGAL parser)
    // unless user explicitly requests CGAL loader
    if (format == Format::OBJ && !force_cgal_loader) {
        return load_obj_rapidobj(filename);
    }
#endif

    // Use CGAL's I/O for other formats (PLY, OFF, etc.)
    // or for OBJ when CGAL loader is explicitly requested
    Mesh mesh;
    bool success = CGAL::IO::read_polygon_mesh(filename, mesh);

    if (!success) {
        last_error_ = "Failed to parse mesh file: " + filename;
        return std::nullopt;
    }

    // Validate basic properties
    if (mesh.number_of_vertices() == 0) {
        last_error_ = "Mesh has no vertices";
        return std::nullopt;
    }

    if (mesh.number_of_faces() == 0) {
        last_error_ = "Mesh has no faces";
        return std::nullopt;
    }

    std::cout << "Loaded mesh from: " << filename;
    if (format == Format::OBJ) {
        std::cout << " (CGAL OBJ parser)";
    }
    std::cout << "\n"
              << "  Vertices: " << mesh.number_of_vertices() << "\n"
              << "  Faces: " << mesh.number_of_faces() << "\n"
              << "  Edges: " << mesh.number_of_edges() << "\n";

    return mesh;
}

bool
MeshLoader::save(const Mesh& mesh, const std::string& filename, Format format, bool binary_ply)
{
    last_error_.clear();

    // Auto-detect format if needed
    if (format == Format::AUTO) {
        format = detect_format(filename);
    }

    bool success = false;

    // Use format-specific writers for proper output
    if (format == Format::PLY) {
        // PLY: binary by default for efficiency (faster I/O and smaller files)
        // Can be overridden with binary_ply parameter
        success = CGAL::IO::write_PLY(filename, mesh, CGAL::parameters::use_binary_mode(binary_ply));
    } else if (format == Format::OBJ) {
        // OBJ: always ASCII (specification requirement)
        success = CGAL::IO::write_OBJ(filename, mesh);
    } else if (format == Format::OFF) {
        // OFF: always ASCII
        success = CGAL::IO::write_OFF(filename, mesh);
    } else {
        // Generic writer (auto-detects from extension)
        success = CGAL::IO::write_polygon_mesh(filename, mesh);
    }

    if (!success) {
        last_error_ = "Failed to write mesh to file: " + filename;
        return false;
    }

    std::cout << "Saved mesh to: " << filename << "\n"
              << "  Vertices: " << mesh.number_of_vertices() << "\n"
              << "  Faces: " << mesh.number_of_faces() << "\n";

    return true;
}

bool
MeshLoader::validate_input_file(const std::string& filename)
{
    if (!fs::exists(filename)) {
        return false;
    }

    if (!fs::is_regular_file(filename)) {
        return false;
    }

    // Check if readable
    std::ifstream file(filename);
    return file.good();
}

const std::string&
MeshLoader::get_last_error()
{
    return last_error_;
}

MeshLoader::Format
MeshLoader::detect_format(const std::string& filename)
{
    auto ext = fs::path(filename).extension().string();

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".obj") {
        return Format::OBJ;
    } else if (ext == ".ply") {
        return Format::PLY;
    } else if (ext == ".off") {
        return Format::OFF;
    }

    // Default to OBJ
    return Format::OBJ;
}

}  // namespace MeshRepair
