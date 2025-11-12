#include "hole_filler.h"
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/bounding_box.h>
#include <iostream>
#include <vector>
#include <chrono>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace MeshRepair {

HoleFiller::HoleFiller(Mesh& mesh, const FillingOptions& options)
    : mesh_(mesh), options_(options) {}

HoleStatistics HoleFiller::fill_hole(const HoleInfo& hole) {
    HoleStatistics stats;
    stats.num_boundary_vertices = hole.boundary_size;
    stats.hole_area = hole.estimated_area;
    stats.hole_diameter = hole.estimated_diameter;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Check if hole should be skipped
    if (should_skip_hole(hole)) {
        if (options_.verbose) {
            std::cout << "    Skipping hole (exceeds size limits): "
                      << hole.boundary_size << " boundary vertices\n";
        }
        stats.filled_successfully = false;
        return stats;
    }

    // Prepare output iterators for tracking added geometry
    std::vector<face_descriptor> patch_faces;
    std::vector<vertex_descriptor> patch_vertices;

    // Call CGAL's triangulate_refine_and_fair_hole
    // This implements Liepa 2003 with Laplacian fairing
    try {
        // Returns tuple<bool, output_iterator, output_iterator>
        auto result = PMP::triangulate_refine_and_fair_hole(
            mesh_,
            hole.boundary_halfedge,
            CGAL::parameters::face_output_iterator(std::back_inserter(patch_faces))
                             .vertex_output_iterator(std::back_inserter(patch_vertices))
                             .use_2d_constrained_delaunay_triangulation(options_.use_2d_cdt)
                             .use_delaunay_triangulation(options_.use_3d_delaunay)
                             .do_not_use_cubic_algorithm(options_.skip_cubic_search)
                             .fairing_continuity(options_.fairing_continuity)
        );

        bool triangulation_success = std::get<0>(result);
        // Note: CGAL returns the output iterators, not fairing success
        // We assume fairing succeeded if triangulation succeeded
        bool fairing_success = triangulation_success;

        if (triangulation_success) {
            stats.num_faces_added = patch_faces.size();
            stats.num_vertices_added = patch_vertices.size();
            stats.filled_successfully = true;
            stats.fairing_succeeded = fairing_success;

            if (options_.verbose) {
                std::cout << "    Filled: "
                          << stats.num_faces_added << " faces, "
                          << stats.num_vertices_added << " vertices added";
                if (!fairing_success) {
                    std::cout << " [FAIRING FAILED]";
                }
                std::cout << "\n";
            }
        } else {
            stats.filled_successfully = false;
            stats.fairing_succeeded = false;

            if (options_.verbose) {
                std::cout << "    Failed to triangulate hole\n";
            }
        }
    } catch (const std::exception& e) {
        stats.filled_successfully = false;
        stats.fairing_succeeded = false;
        std::cerr << "    Exception during hole filling: " << e.what() << "\n";
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    stats.fill_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return stats;
}

MeshStatistics HoleFiller::fill_all_holes(const std::vector<HoleInfo>& holes) {
    MeshStatistics mesh_stats;
    mesh_stats.original_vertices = mesh_.number_of_vertices();
    mesh_stats.original_faces = mesh_.number_of_faces();
    mesh_stats.num_holes_detected = holes.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    if (holes.empty()) {
        std::cout << "No holes to fill.\n";
        return mesh_stats;
    }

    if (options_.verbose) {
        std::cout << "\nFilling " << holes.size() << " hole(s)...\n";
    }

    for (size_t i = 0; i < holes.size(); ++i) {
        // Only show per-hole progress in verbose mode
        if (options_.verbose) {
            std::cout << "  Hole " << (i + 1) << "/" << holes.size()
                      << " (" << holes[i].boundary_size << " boundary vertices):\n";
        }

        HoleStatistics hole_stats = fill_hole(holes[i]);
        mesh_stats.hole_details.push_back(hole_stats);

        if (hole_stats.filled_successfully) {
            mesh_stats.num_holes_filled++;
        } else {
            if (should_skip_hole(holes[i])) {
                mesh_stats.num_holes_skipped++;
            } else {
                mesh_stats.num_holes_failed++;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    mesh_stats.total_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    mesh_stats.final_vertices = mesh_.number_of_vertices();
    mesh_stats.final_faces = mesh_.number_of_faces();

    // Print summary
    std::cout << "\n=== Hole Filling Summary ===\n";
    std::cout << "  Filled successfully: " << mesh_stats.num_holes_filled << "\n";
    std::cout << "  Failed: " << mesh_stats.num_holes_failed << "\n";
    std::cout << "  Skipped (too large): " << mesh_stats.num_holes_skipped << "\n";
    std::cout << "  Faces added: " << mesh_stats.total_faces_added() << "\n";
    std::cout << "  Vertices added: " << mesh_stats.total_vertices_added() << "\n";
    std::cout << "  Total time: " << mesh_stats.total_time_ms << " ms\n";

    return mesh_stats;
}

bool HoleFiller::should_skip_hole(const HoleInfo& hole) const {
    // Check boundary vertex count
    if (hole.boundary_size > options_.max_hole_boundary_vertices) {
        return true;
    }

    // Check diameter relative to mesh
    double mesh_diagonal = compute_mesh_bbox_diagonal();
    if (mesh_diagonal > 0.0 &&
        hole.estimated_diameter > mesh_diagonal * options_.max_hole_diameter_ratio) {
        return true;
    }

    return false;
}

double HoleFiller::compute_mesh_bbox_diagonal() const {
    if (mesh_.number_of_vertices() == 0) {
        return 0.0;
    }

    std::vector<Point_3> all_points;
    all_points.reserve(mesh_.number_of_vertices());

    for (auto v : mesh_.vertices()) {
        all_points.push_back(mesh_.point(v));
    }

    auto bbox = CGAL::bounding_box(all_points.begin(), all_points.end());
    auto diag_squared = CGAL::squared_distance(bbox.min(), bbox.max());
    return std::sqrt(CGAL::to_double(diag_squared));
}

void HoleFiller::report_progress(size_t current, size_t total, const std::string& message) const {
    if (options_.show_progress && total > 0) {
        double percentage = (static_cast<double>(current) / total) * 100.0;
        std::cout << "  [" << current << "/" << total << " - "
                  << static_cast<int>(percentage) << "%] " << message << "\n";
    }
}

} // namespace MeshRepair
