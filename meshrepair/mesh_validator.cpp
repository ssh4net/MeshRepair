#include "mesh_validator.h"
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/bounding_box.h>
#include <iostream>
#include <sstream>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace MeshRepair {

bool
MeshValidator::is_valid(const Mesh& mesh)
{
    return mesh.is_valid();
}

bool
MeshValidator::is_triangle_mesh(const Mesh& mesh)
{
    return CGAL::is_triangle_mesh(mesh);
}

bool
MeshValidator::is_closed(const Mesh& mesh)
{
    return CGAL::is_closed(mesh);
}

size_t
MeshValidator::count_connected_components(const Mesh& mesh)
{
    // Need non-const mesh for property map operations
    Mesh& non_const_mesh = const_cast<Mesh&>(mesh);

    Mesh::Property_map<face_descriptor, size_t> fccmap;
    fccmap = non_const_mesh.add_property_map<face_descriptor, size_t>("f:CC").first;

    size_t num_components = PMP::connected_components(non_const_mesh, fccmap);

    non_const_mesh.remove_property_map(fccmap);

    return num_components;
}

double
MeshValidator::get_bbox_diagonal(const Mesh& mesh)
{
    if (mesh.number_of_vertices() == 0) {
        return 0.0;
    }

    std::vector<Point_3> points;
    points.reserve(mesh.number_of_vertices());

    for (auto v : mesh.vertices()) {
        points.push_back(mesh.point(v));
    }

    auto bbox         = CGAL::bounding_box(points.begin(), points.end());
    auto diag_squared = CGAL::squared_distance(bbox.min(), bbox.max());
    return std::sqrt(CGAL::to_double(diag_squared));
}

void
MeshValidator::print_statistics(const Mesh& mesh, bool detailed)
{
    std::cerr << "\n=== Mesh Statistics ===\n";
    std::cerr << "  Vertices: " << mesh.number_of_vertices() << "\n";
    std::cerr << "  Faces: " << mesh.number_of_faces() << "\n";
    std::cerr << "  Edges: " << mesh.number_of_edges() << "\n";
    std::cerr << "  Halfedges: " << mesh.number_of_halfedges() << "\n";

    if (detailed) {
        std::cerr << "\n=== Validation ===\n";
        std::cerr << "  Valid: " << (is_valid(mesh) ? "YES" : "NO") << "\n";
        std::cerr << "  Triangle mesh: " << (is_triangle_mesh(mesh) ? "YES" : "NO") << "\n";
        std::cerr << "  Closed (watertight): " << (is_closed(mesh) ? "YES" : "NO") << "\n";

        size_t num_components = count_connected_components(mesh);
        std::cerr << "  Connected components: " << num_components << "\n";

        double diagonal = get_bbox_diagonal(mesh);
        std::cerr << "  Bounding box diagonal: " << diagonal << "\n";

        // Count border edges
        size_t border_edges = 0;
        for (auto h : mesh.halfedges()) {
            if (mesh.is_border(h)) {
                ++border_edges;
            }
        }
        std::cerr << "  Border edges: " << border_edges << "\n";
    }

    std::cerr << "=======================\n\n";
}

std::string
MeshValidator::generate_report(const Mesh& mesh)
{
    std::ostringstream report;

    report << "Mesh Validation Report\n";
    report << "======================\n\n";

    report << "Basic Properties:\n";
    report << "  Vertices: " << mesh.number_of_vertices() << "\n";
    report << "  Faces: " << mesh.number_of_faces() << "\n";
    report << "  Edges: " << mesh.number_of_edges() << "\n";

    report << "\nValidity Checks:\n";
    report << "  Mesh valid: " << (is_valid(mesh) ? "PASS" : "FAIL") << "\n";
    report << "  All triangles: " << (is_triangle_mesh(mesh) ? "PASS" : "FAIL") << "\n";
    report << "  Watertight: " << (is_closed(mesh) ? "PASS" : "FAIL") << "\n";

    size_t num_components = count_connected_components(mesh);
    report << "  Connected components: " << num_components;
    if (num_components == 1) {
        report << " (GOOD)\n";
    } else if (num_components > 1) {
        report << " (WARNING: Multiple components)\n";
    }

    double diagonal = get_bbox_diagonal(mesh);
    report << "\nGeometry:\n";
    report << "  Bounding box diagonal: " << diagonal << "\n";

    return report.str();
}

}  // namespace MeshRepair
