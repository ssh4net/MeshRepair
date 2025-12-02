#include "mesh_loader.h"
#include "logger.h"
#include <CGAL/IO/OBJ.h>
#include <CGAL/IO/OFF.h>
#include <CGAL/IO/PLY.h>
#include <CGAL/IO/polygon_mesh_io.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef HAVE_RAPIDOBJ
#    include <rapidobj/rapidobj.hpp>
#endif

namespace fs = std::filesystem;

namespace MeshRepair {
namespace MeshLoader {

    namespace {
        std::string g_last_error;

        void set_error(const std::string& msg) { g_last_error = msg; }
    }  // namespace

#ifdef HAVE_RAPIDOBJ
    static bool load_obj_rapidobj(const std::string& filename, Mesh* out_mesh)
    {
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result     = rapidobj::ParseFile(filename, rapidobj::MaterialLibrary::Ignore());

        if (result.error) {
            set_error("RapidOBJ parse error: " + result.error.code.message() + " at line "
                      + std::to_string(result.error.line_num));
            return false;
        }

        if (!rapidobj::Triangulate(result)) {
            set_error("RapidOBJ triangulation failed");
            return false;
        }

        const auto& positions = result.attributes.positions;
        size_t num_vertices   = positions.size() / 3;
        if (num_vertices == 0) {
            set_error("Mesh has no vertices");
            return false;
        }

        size_t num_faces = 0;
        for (const auto& shape : result.shapes) {
            num_faces += shape.mesh.num_face_vertices.size();
        }
        if (num_faces == 0) {
            set_error("Mesh has no faces");
            return false;
        }

        Mesh mesh;
        mesh.reserve(num_vertices, num_faces * 3, num_faces);

        std::vector<Mesh::Vertex_index> vertex_map(num_vertices);
        for (size_t i = 0; i < num_vertices; ++i) {
            size_t idx = i * 3;
            Point_3 p(static_cast<double>(positions[idx + 0]), static_cast<double>(positions[idx + 1]),
                      static_cast<double>(positions[idx + 2]));
            vertex_map[i] = mesh.add_vertex(p);
        }

        // Release heavy arrays early
        auto shapes = std::move(result.shapes);
        result      = {};

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
                    ++faces_failed;
                }
                idx += num_verts;
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        std::ostringstream load_log;
        load_log << "Loaded mesh from: " << filename << " (RapidOBJ)\n"
                 << "  Vertices: " << mesh.number_of_vertices() << "\n"
                 << "  Faces: " << mesh.number_of_faces() << " (added: " << faces_added;
        if (faces_failed > 0) {
            load_log << ", failed: " << faces_failed;
        }
        load_log << ")\n"
                 << "  Edges: " << mesh.number_of_edges() << "\n"
                 << "  Total time: " << total_ms << " ms";
        logInfo(LogCategory::Cli, load_log.str());

        if (faces_failed > 0) {
            logWarn(LogCategory::Cli,
                    "Warning: " + std::to_string(faces_failed) + " faces failed to add (non-manifold or duplicate)");
        }

        *out_mesh = std::move(mesh);
        return true;
    }

    static bool load_obj_rapidobj_as_soup(const std::string& filename, PolygonSoup* out_soup)
    {
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result     = rapidobj::ParseFile(filename, rapidobj::MaterialLibrary::Ignore());

        if (result.error) {
            set_error("RapidOBJ parse error: " + result.error.code.message() + " at line "
                      + std::to_string(result.error.line_num));
            return false;
        }

        if (!rapidobj::Triangulate(result)) {
            set_error("RapidOBJ triangulation failed");
            return false;
        }

        PolygonSoup soup;
        const auto& positions = result.attributes.positions;
        size_t num_vertices   = positions.size() / 3;
        soup.points.reserve(num_vertices);
        for (size_t i = 0; i < num_vertices; ++i) {
            size_t idx = i * 3;
            soup.points.emplace_back(static_cast<double>(positions[idx + 0]), static_cast<double>(positions[idx + 1]),
                                     static_cast<double>(positions[idx + 2]));
        }

        size_t num_faces = 0;
        for (const auto& shape : result.shapes) {
            num_faces += shape.mesh.num_face_vertices.size();
        }
        soup.polygons.reserve(num_faces);

        for (const auto& shape : result.shapes) {
            const auto& shape_mesh = shape.mesh;
            size_t idx             = 0;
            for (size_t f = 0; f < shape_mesh.num_face_vertices.size(); ++f) {
                uint8_t num_verts = shape_mesh.num_face_vertices[f];
                if (num_verts == 3) {
                    std::vector<std::size_t> polygon;
                    polygon.reserve(3);
                    polygon.push_back(shape_mesh.indices[idx + 0].position_index);
                    polygon.push_back(shape_mesh.indices[idx + 1].position_index);
                    polygon.push_back(shape_mesh.indices[idx + 2].position_index);
                    soup.polygons.push_back(std::move(polygon));
                }
                idx += num_verts;
            }
        }

        auto end_time     = std::chrono::high_resolution_clock::now();
        soup.load_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        *out_soup         = std::move(soup);
        return true;
    }
#endif  // HAVE_RAPIDOBJ

    bool validate_input_file(const std::string& filename)
    {
        if (!fs::exists(filename) || !fs::is_regular_file(filename)) {
            return false;
        }

        std::ifstream file(filename);
        return file.good();
    }

    Format detect_format(const std::string& filename)
    {
        auto ext = fs::path(filename).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".obj") {
            return Format::OBJ;
        }
        if (ext == ".ply") {
            return Format::PLY;
        }
        if (ext == ".off") {
            return Format::OFF;
        }
        return Format::OBJ;
    }

    bool load_soup(const std::string& filename, Format format, bool force_cgal_loader, PolygonSoup* out_soup)
    {
        if (!out_soup) {
            set_error("Output soup pointer is null");
            return false;
        }
        if (!validate_input_file(filename)) {
            set_error("File not found or not readable: " + filename);
            return false;
        }

        if (format == Format::AUTO) {
            format = detect_format(filename);
        }

#ifdef HAVE_RAPIDOBJ
        if (format == Format::OBJ && !force_cgal_loader) {
            return load_obj_rapidobj_as_soup(filename, out_soup);
        }
#endif

        auto start_time = std::chrono::high_resolution_clock::now();

        PolygonSoup soup;
        bool success = false;

        if (format == Format::PLY) {
            success = CGAL::IO::read_PLY(filename, soup.points, soup.polygons);
        } else if (format == Format::OBJ) {
            success = CGAL::IO::read_OBJ(filename, soup.points, soup.polygons);
        } else if (format == Format::OFF) {
            success = CGAL::IO::read_OFF(filename, soup.points, soup.polygons);
        } else {
            set_error("Unsupported format for soup loading");
            return false;
        }

        if (!success) {
            set_error("Failed to parse polygon soup from file: " + filename);
            return false;
        }
        if (soup.points.empty()) {
            set_error("Polygon soup has no points");
            return false;
        }
        if (soup.polygons.empty()) {
            set_error("Polygon soup has no polygons");
            return false;
        }

        auto end_time     = std::chrono::high_resolution_clock::now();
        soup.load_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        *out_soup = std::move(soup);
        return true;
    }

    bool load_mesh(const std::string& filename, Format format, bool force_cgal_loader, Mesh* out_mesh)
    {
        if (!out_mesh) {
            set_error("Output mesh pointer is null");
            return false;
        }
        if (!validate_input_file(filename)) {
            set_error("File not found or not readable: " + filename);
            return false;
        }

        if (format == Format::AUTO) {
            format = detect_format(filename);
        }

#ifdef HAVE_RAPIDOBJ
        if (format == Format::OBJ && !force_cgal_loader) {
            return load_obj_rapidobj(filename, out_mesh);
        }
#endif

        Mesh mesh;
        bool success = CGAL::IO::read_polygon_mesh(filename, mesh);
        if (!success) {
            set_error("Failed to parse mesh file: " + filename);
            return false;
        }
        if (mesh.number_of_vertices() == 0) {
            set_error("Mesh has no vertices");
            return false;
        }
        if (mesh.number_of_faces() == 0) {
            set_error("Mesh has no faces");
            return false;
        }

        *out_mesh = std::move(mesh);
        return true;
    }

    bool save_mesh(const Mesh& mesh, const std::string& filename, Format format, bool binary_ply)
    {
        if (format == Format::AUTO) {
            format = detect_format(filename);
        }

        bool success = false;
        if (format == Format::PLY) {
            success = CGAL::IO::write_PLY(filename, mesh, CGAL::parameters::use_binary_mode(binary_ply));
        } else if (format == Format::OBJ) {
            success = CGAL::IO::write_OBJ(filename, mesh);
        } else if (format == Format::OFF) {
            success = CGAL::IO::write_OFF(filename, mesh);
        } else {
            success = CGAL::IO::write_polygon_mesh(filename, mesh);
        }

        if (!success) {
            set_error("Failed to write mesh to file: " + filename);
            return false;
        }
        return true;
    }

    const std::string& last_error() { return g_last_error; }

}  // namespace MeshLoader

int
mesh_loader_load(const char* filename, MeshLoader::Format format, bool force_cgal_loader, Mesh* out_mesh)
{
    if (!filename || !out_mesh) {
        return -1;
    }
    return MeshLoader::load_mesh(std::string(filename), format, force_cgal_loader, out_mesh) ? 0 : -1;
}

int
mesh_loader_load_soup(const char* filename, MeshLoader::Format format, bool force_cgal_loader, PolygonSoup* out_soup)
{
    if (!filename || !out_soup) {
        return -1;
    }
    return MeshLoader::load_soup(std::string(filename), format, force_cgal_loader, out_soup) ? 0 : -1;
}

int
mesh_loader_save(const Mesh& mesh, const char* filename, MeshLoader::Format format, bool binary_ply)
{
    if (!filename) {
        return -1;
    }
    return MeshLoader::save_mesh(mesh, std::string(filename), format, binary_ply) ? 0 : -1;
}

const char*
mesh_loader_last_error()
{
    return MeshLoader::last_error().c_str();
}

}  // namespace MeshRepair
