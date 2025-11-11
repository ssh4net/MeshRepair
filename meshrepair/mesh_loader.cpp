#include "mesh_loader.h"
#include <CGAL/IO/polygon_mesh_io.h>
#include <CGAL/IO/PLY.h>
#include <CGAL/IO/OBJ.h>
#include <CGAL/IO/OFF.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

namespace MeshRepair {

std::string MeshLoader::last_error_;

std::optional<Mesh> MeshLoader::load(const std::string& filename, Format format) {
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

    // Load mesh using CGAL's I/O
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

    std::cout << "Loaded mesh from: " << filename << "\n"
              << "  Vertices: " << mesh.number_of_vertices() << "\n"
              << "  Faces: " << mesh.number_of_faces() << "\n"
              << "  Edges: " << mesh.number_of_edges() << "\n";

    return mesh;
}

bool MeshLoader::save(const Mesh& mesh, const std::string& filename, Format format, bool binary_ply) {
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
        success = CGAL::IO::write_PLY(filename, mesh,
            CGAL::parameters::use_binary_mode(binary_ply));
    }
    else if (format == Format::OBJ) {
        // OBJ: always ASCII (specification requirement)
        success = CGAL::IO::write_OBJ(filename, mesh);
    }
    else if (format == Format::OFF) {
        // OFF: always ASCII
        success = CGAL::IO::write_OFF(filename, mesh);
    }
    else {
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

bool MeshLoader::validate_input_file(const std::string& filename) {
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

const std::string& MeshLoader::get_last_error() {
    return last_error_;
}

MeshLoader::Format MeshLoader::detect_format(const std::string& filename) {
    auto ext = fs::path(filename).extension().string();

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

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

} // namespace MeshRepair
