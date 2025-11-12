#include "hole_detector.h"
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/bounding_box.h>
#include <unordered_set>
#include <iostream>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace MeshRepair {

HoleDetector::HoleDetector(const Mesh& mesh, bool verbose) : mesh_(mesh), verbose_(verbose) {}

std::vector<HoleInfo> HoleDetector::detect_all_holes() {
    std::vector<HoleInfo> holes;
    std::unordered_set<halfedge_descriptor> processed;

    // Iterate over all halfedges to find border cycles
    for (auto h : mesh_.halfedges()) {
        if (mesh_.is_border(h) && processed.find(h) == processed.end()) {
            // Found a new hole boundary
            HoleInfo info = analyze_hole(mesh_, h);
            holes.push_back(info);

            // Mark all halfedges in this boundary as processed
            auto h_current = h;
            do {
                processed.insert(h_current);
                h_current = mesh_.next(h_current);
            } while (h_current != h);
        }
    }

    if (!holes.empty()) {
        if (verbose_) {
            std::cout << "Detected " << holes.size() << " hole(s):\n";
            for (size_t i = 0; i < holes.size(); ++i) {
                const auto& hole = holes[i];
                std::cout << "  Hole " << (i + 1) << ": "
                          << hole.boundary_size << " boundary vertices, "
                          << "diameter ~ " << std::fixed << hole.estimated_diameter << "\n";
            }
        }
    } else {
        if (verbose_) {
            std::cout << "No holes detected. Mesh is closed.\n";
        }
    }

    return holes;
}

bool HoleDetector::is_border_halfedge(const Mesh& mesh, halfedge_descriptor h) {
    return mesh.is_border(h);
}

HoleInfo HoleDetector::analyze_hole(const Mesh& mesh, halfedge_descriptor border_h) {
    HoleInfo info;
    info.boundary_halfedge = border_h;

    // Collect boundary vertices
    auto h = border_h;
    do {
        info.boundary_vertices.push_back(mesh.target(h));
        h = mesh.next(h);
    } while (h != border_h);

    info.boundary_size = info.boundary_vertices.size();

    // Estimate diameter using bounding box diagonal
    std::vector<Point_3> boundary_points;
    boundary_points.reserve(info.boundary_vertices.size());

    for (auto v : info.boundary_vertices) {
        boundary_points.push_back(mesh.point(v));
    }

    auto bbox = CGAL::bounding_box(boundary_points.begin(), boundary_points.end());
    auto diag_squared = CGAL::squared_distance(bbox.min(), bbox.max());
    info.estimated_diameter = std::sqrt(CGAL::to_double(diag_squared));

    // Rough area estimate (assumes roughly circular hole)
    // More accurate would be to compute actual polygon area
    double radius = info.estimated_diameter / 2.0;
    info.estimated_area = 3.14159 * radius * radius;

    return info;
}

size_t HoleDetector::count_border_edges(const Mesh& mesh) {
    size_t count = 0;
    for (auto h : mesh.halfedges()) {
        if (mesh.is_border(h)) {
            ++count;
        }
    }
    return count;
}

} // namespace MeshRepair
