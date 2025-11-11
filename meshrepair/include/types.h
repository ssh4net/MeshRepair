#pragma once

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <vector>
#include <string>

namespace MeshRepair {

// Geometric kernel - exact predicates for robustness, inexact constructions for speed
using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using Vector_3 = Kernel::Vector_3;

// Mesh data structure (CGAL Surface_mesh - modern, efficient)
using Mesh = CGAL::Surface_mesh<Point_3>;
using vertex_descriptor = Mesh::Vertex_index;
using face_descriptor = Mesh::Face_index;
using edge_descriptor = Mesh::Edge_index;
using halfedge_descriptor = Mesh::Halfedge_index;

// Statistics for individual hole filling
struct HoleStatistics {
    size_t num_boundary_vertices = 0;
    size_t num_faces_added = 0;
    size_t num_vertices_added = 0;
    double hole_area = 0.0;
    double hole_diameter = 0.0;
    bool filled_successfully = false;
    bool fairing_succeeded = false;
    double fill_time_ms = 0.0;
};

// Overall mesh repair statistics
struct MeshStatistics {
    // Original mesh info
    size_t original_vertices = 0;
    size_t original_faces = 0;

    // Final mesh info
    size_t final_vertices = 0;
    size_t final_faces = 0;

    // Hole processing
    size_t num_holes_detected = 0;
    size_t num_holes_filled = 0;
    size_t num_holes_failed = 0;
    size_t num_holes_skipped = 0;

    // Timing
    double total_time_ms = 0.0;

    // Detailed per-hole statistics
    std::vector<HoleStatistics> hole_details;

    // Summary methods
    size_t total_faces_added() const {
        size_t total = 0;
        for (const auto& h : hole_details) {
            total += h.num_faces_added;
        }
        return total;
    }

    size_t total_vertices_added() const {
        size_t total = 0;
        for (const auto& h : hole_details) {
            total += h.num_vertices_added;
        }
        return total;
    }
};

} // namespace MeshRepair
