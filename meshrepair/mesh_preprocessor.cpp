#include "mesh_preprocessor.h"
#include "mesh_loader.h"  // For PolygonSoup
#include "polygon_soup_repair.h"
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/boost/graph/Euler_operations.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/IO/PLY.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <iomanip>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace MeshRepair {

MeshPreprocessor::MeshPreprocessor(Mesh& mesh, const PreprocessingOptions& options)
    : mesh_(mesh)
    , options_(options)
    , stats_()
{
}

PreprocessingStats
MeshPreprocessor::preprocess()
{
    auto start_time = std::chrono::high_resolution_clock::now();

    size_t initial_vertices = mesh_.number_of_vertices();
    size_t initial_faces = mesh_.number_of_faces();

    if (options_.verbose) {
        std::cout << "\n=== Mesh Preprocessing ===\n";
        std::cout << "Initial mesh state:\n";
        std::cout << "  Vertices: " << initial_vertices << "\n";
        std::cout << "  Faces: " << initial_faces << "\n\n";
    }

    // =============================================================================
    // PHASE 1: POLYGON SOUP PROCESSING (Most cleanup happens here)
    // =============================================================================

    if (options_.verbose) {
        std::cout << "[Phase 1] Polygon Soup Cleanup\n";
    }

    // Step 1.1: Extract mesh to polygon soup
    if (options_.verbose) {
        std::cout << "[1/6] Extracting polygon soup...\n";
    }

    // OPTIMIZATION: Pre-allocate vectors with exact sizes to avoid reallocations
    size_t num_vertices = mesh_.number_of_vertices();
    size_t num_faces = mesh_.number_of_faces();

    std::vector<Point_3> points;
    std::vector<std::vector<std::size_t>> polygons;

    points.reserve(num_vertices);
    polygons.reserve(num_faces);

    // Extract vertices - build index map and populate points
    std::map<vertex_descriptor, std::size_t> vertex_index_map;
    std::size_t v_idx = 0;
    for (auto v : mesh_.vertices()) {
        vertex_index_map[v] = v_idx++;
        points.push_back(mesh_.point(v));
    }

    // Extract faces - pre-reserve typical polygon size to avoid reallocations
    for (auto f : mesh_.faces()) {
        std::vector<std::size_t> polygon;
        polygon.reserve(4);  // Reserve for typical triangle/quad (avoids reallocation)
        for (auto v : CGAL::vertices_around_face(CGAL::halfedge(f, mesh_), mesh_)) {
            polygon.push_back(vertex_index_map[v]);
        }
        polygons.push_back(std::move(polygon));  // Move to avoid copy
    }

    if (options_.debug) {
        std::cout << "  [DEBUG] Extracted soup: " << points.size() << " points, " << polygons.size() << " polygons\n";
    }

    // Step 1.2: Remove duplicate points in soup
    if (options_.remove_duplicates) {
        if (options_.verbose) {
            std::cout << "[2/6] Removing duplicate points...\n";
        }

        size_t points_before = points.size();
        PMP::merge_duplicate_points_in_polygon_soup(points, polygons);
        stats_.duplicates_merged = points_before - points.size();

        if (options_.verbose) {
            std::cout << "  Merged: " << stats_.duplicates_merged << " duplicate points\n\n";
        }
    }

    // Step 1.3: Remove duplicate polygons
    if (options_.remove_duplicates) {
        size_t polygons_before = polygons.size();
        PMP::merge_duplicate_polygons_in_polygon_soup(points, polygons);

        if (options_.debug && polygons_before > polygons.size()) {
            std::cout << "  [DEBUG] Removed " << (polygons_before - polygons.size()) << " duplicate polygons\n";
        }
    }

    // Step 1.4: Remove degenerate polygons (< 3 unique vertices)
    if (options_.verbose) {
        std::cout << "[3/6] Removing degenerate polygons...\n";
    }

    size_t before_degenerate = polygons.size();
    auto it = std::remove_if(polygons.begin(), polygons.end(), [](const std::vector<std::size_t>& poly) {
        if (poly.size() < 3)
            return true;
        std::set<std::size_t> unique_verts(poly.begin(), poly.end());
        return unique_verts.size() < 3;
    });
    polygons.erase(it, polygons.end());

    if (options_.verbose) {
        std::cout << "  Removed: " << (before_degenerate - polygons.size()) << " degenerate polygons\n\n";
    }

    // Step 1.5: Remove non-manifold vertices/edges in soup (CRITICAL!)
    // Uses recursive local search - checks affected vertices only after first pass
    if (options_.remove_non_manifold) {
        if (options_.verbose) {
            std::cout << "[4/7] Removing non-manifold vertices/edges (recursive local search)...\n";
        }

        // Use recursive local search with max depth from options (default: 10)
        size_t max_depth = (options_.non_manifold_passes > 0) ? options_.non_manifold_passes : 10;
        auto nm_result = PolygonSoupRepair::remove_non_manifold_polygons_detailed(polygons, max_depth, options_.debug);
        stats_.non_manifold_vertices_removed = nm_result.total_polygons_removed;

        if (options_.verbose) {
            std::cout << "  Removed: " << nm_result.total_polygons_removed << " polygon(s) "
                      << "in " << nm_result.iterations_executed << " iteration(s)";
            if (nm_result.hit_max_iterations) {
                std::cout << " (hit max limit of " << max_depth << ")";
            }
            std::cout << "\n\n";
        }
    }

    // Step 1.6: Remove 3-face fans (simplification)
    if (options_.remove_3_face_fans) {
        if (options_.verbose) {
            std::cout << "[5/7] Collapsing 3-face fans...\n";
        }

        size_t fans_collapsed = PolygonSoupRepair::remove_3_face_fans(points, polygons);
        stats_.face_fans_collapsed = fans_collapsed;

        if (options_.verbose) {
            std::cout << "  Collapsed: " << fans_collapsed << " 3-face fan(s)\n\n";
        }

        // Debug: Save mesh after fan removal
        if (options_.debug) {
            std::string debug_file = "debug_04_after_3_face_fans.ply";
            Mesh debug_mesh;
            PMP::polygon_soup_to_polygon_mesh(points, polygons, debug_mesh);
            if (CGAL::IO::write_PLY(debug_file, debug_mesh, CGAL::parameters::use_binary_mode(true))) {
                std::cout << "  [DEBUG] Saved soup (after 3-face fans): " << debug_file << "\n";
                std::cout << "  [DEBUG]   Mesh: " << debug_mesh.number_of_vertices() << " vertices, "
                          << debug_mesh.number_of_faces() << " faces\n";
            }
        }
    }

    // Step 1.7: Orient polygon soup (face normals)
    if (options_.verbose) {
        std::cout << "[6/7] Orienting polygon soup...\n";
    }

    // Debug: Save soup BEFORE orient
    if (options_.debug) {
        std::string debug_file = "debug_05_preprocess_soup_before_orient.ply";
        Mesh debug_mesh;
        PMP::polygon_soup_to_polygon_mesh(points, polygons, debug_mesh);

        if (CGAL::IO::write_PLY(debug_file, debug_mesh, CGAL::parameters::use_binary_mode(true))) {
            std::cout << "  [DEBUG] Saved soup (before orient): " << debug_file << "\n";
            std::cout << "  [DEBUG]   Mesh: " << debug_mesh.number_of_vertices() << " vertices, "
                      << debug_mesh.number_of_faces() << " faces\n";
        }
    }

    bool oriented = PMP::orient_polygon_soup(points, polygons);

    if (options_.verbose) {
        if (oriented) {
            std::cout << "  Oriented successfully\n\n";
        } else {
            std::cout << "  Warning: Some points were duplicated during orientation\n\n";
        }
    }

    // Step 1.8: Convert soup to mesh (ONCE!)
    if (options_.verbose) {
        std::cout << "[7/7] Converting soup to mesh...\n";
    }

    mesh_.clear();
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh_);

    if (options_.verbose) {
        std::cout << "  Mesh: " << mesh_.number_of_vertices() << " vertices, "
                  << mesh_.number_of_faces() << " faces\n\n";
    }

    // Debug: Save mesh after soup conversion
    if (options_.debug) {
        std::string debug_file = "debug_06_after_soup_cleanup.ply";
        if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
            if (options_.verbose) {
                std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
            }
        }
    }

    // =============================================================================
    // PHASE 2: MESH-LEVEL PROCESSING (Quick cleanups requiring halfedge structure)
    // =============================================================================

    if (options_.verbose) {
        std::cout << "[Phase 2] Mesh-Level Cleanup\n";
    }

    // Step 2.1: Remove isolated vertices (vertices with no incident faces)
    if (options_.remove_isolated) {
        if (options_.verbose) {
            std::cout << "[7/8] Removing isolated vertices...\n";
        }
        stats_.isolated_vertices_removed = remove_isolated_vertices();
        if (options_.verbose) {
            std::cout << "  Removed: " << stats_.isolated_vertices_removed << " isolated vertices\n\n";
        }

        // Debug: dump intermediate mesh
        if (options_.debug) {
            if (mesh_.has_garbage()) {
                mesh_.collect_garbage();
            }
            std::string debug_file = "debug_07_after_isolated.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
                if (options_.verbose) {
                    std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
                }
            }
        }
    }

    // Step 2.2: Keep only largest connected component
    if (options_.keep_largest_component) {
        if (options_.verbose) {
            std::cout << "[8/8] Keeping only largest connected component...\n";
        }
        stats_.small_components_removed = keep_only_largest_connected_component();
        if (options_.verbose) {
            std::cout << "  Found: " << stats_.connected_components_found << " component(s)\n";
            std::cout << "  Removed: " << stats_.small_components_removed << " small component(s)\n\n";
        }

        // Debug: dump intermediate mesh
        if (options_.debug) {
            if (mesh_.has_garbage()) {
                mesh_.collect_garbage();
            }
            std::string debug_file = "debug_08_after_components.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
                if (options_.verbose) {
                    std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
                }
            }
        }
    }

    auto end_time        = std::chrono::high_resolution_clock::now();
    stats_.total_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    // Collect garbage to compact the mesh and remove marked elements
    // This is CRITICAL before saving - otherwise face indices will be invalid
    if (mesh_.has_garbage()) {
        if (options_.verbose) {
            std::cout << "Collecting garbage (compacting mesh)...\n";
        }
        mesh_.collect_garbage();
    }

    // Validate mesh topology
    if (options_.verbose) {
        std::cout << "Validating mesh topology...\n";
    }
    bool is_valid = CGAL::is_valid_polygon_mesh(mesh_, options_.verbose);
    if (!is_valid) {
        std::cout << "WARNING: Mesh is not valid after preprocessing!\n";
        std::cout << "The mesh may have topological issues. Attempting to continue anyway...\n";
    }

    if (options_.verbose) {
        std::cout << "Final mesh state:\n";
        std::cout << "  Vertices: " << mesh_.number_of_vertices() << "\n";
        std::cout << "  Faces: " << mesh_.number_of_faces() << "\n";
        std::cout << "  Valid: " << (is_valid ? "YES" : "NO") << "\n";
        std::cout << "  Time: " << std::fixed << std::setprecision(2) << stats_.total_time_ms << " ms\n";
        std::cout << "==========================\n\n";
    }

    return stats_;
}

// ============================================================================
// Mesh-level helper functions (used by old preprocess() API)
// ============================================================================

size_t
MeshPreprocessor::remove_isolated_vertices()
{
    return PMP::remove_isolated_vertices(mesh_);
}

size_t
MeshPreprocessor::keep_only_largest_connected_component()
{
    // Create a face property map to store component IDs
    auto fccmap = mesh_.add_property_map<face_descriptor, std::size_t>("f:CC").first;

    // Compute connected components
    std::size_t num_components = PMP::connected_components(mesh_, fccmap);

    stats_.connected_components_found = num_components;

    if (num_components <= 1) {
        // Only one component, nothing to remove
        mesh_.remove_property_map(fccmap);
        return 0;
    }

    // Count faces in each component
    std::map<std::size_t, std::size_t> component_sizes;
    for (auto f : mesh_.faces()) {
        component_sizes[fccmap[f]]++;
    }

    // Find the largest component
    std::size_t largest_component_id   = 0;
    std::size_t largest_component_size = 0;
    for (const auto& pair : component_sizes) {
        if (pair.second > largest_component_size) {
            largest_component_id   = pair.first;
            largest_component_size = pair.second;
        }
    }

    if (options_.verbose) {
        std::cout << "  Largest component ID: " << largest_component_id << " with " << largest_component_size
                  << " faces\n";
    }

    // Remove all faces NOT in the largest component
    std::vector<face_descriptor> faces_to_remove;
    for (auto f : mesh_.faces()) {
        if (fccmap[f] != largest_component_id) {
            faces_to_remove.push_back(f);
        }
    }

    // Remove the faces
    for (auto f : faces_to_remove) {
        if (!mesh_.is_removed(f)) {
            CGAL::Euler::remove_face(CGAL::halfedge(f, mesh_), mesh_);
        }
    }

    // Clean up property map
    mesh_.remove_property_map(fccmap);

    // Remove isolated vertices created by face removal
    PMP::remove_isolated_vertices(mesh_);

    return num_components - 1;  // Number of small components removed
}

void
MeshPreprocessor::print_report() const
{
    std::cout << "\n=== Preprocessing Report ===\n";
    std::cout << "Duplicate vertices merged: " << stats_.duplicates_merged << "\n";
    std::cout << "Non-manifold polygons removed: " << stats_.non_manifold_vertices_removed << "\n";
    std::cout << "3-face fans collapsed: " << stats_.face_fans_collapsed << "\n";
    std::cout << "Isolated vertices removed: " << stats_.isolated_vertices_removed << "\n";
    std::cout << "Connected components found: " << stats_.connected_components_found << "\n";
    std::cout << "Small components removed: " << stats_.small_components_removed << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << stats_.total_time_ms << " ms\n";
    std::cout << "============================\n\n";
}

// Static function: preprocess polygon soup directly (avoids mesh->soup extraction!)
PreprocessingStats
MeshPreprocessor::preprocess_soup(PolygonSoup& soup, Mesh& output_mesh, const PreprocessingOptions& options)
{
    auto total_start = std::chrono::high_resolution_clock::now();
    auto soup_start = total_start;

    PreprocessingStats stats;

    size_t initial_points = soup.points.size();
    size_t initial_polygons = soup.polygons.size();

    if (options.verbose) {
        std::cout << "\n=== Mesh Preprocessing (Soup-Based) ===\n";
        std::cout << "Initial soup state:\n";
        std::cout << "  Points: " << initial_points << "\n";
        std::cout << "  Polygons: " << initial_polygons << "\n\n";
    }

    // =========================================================================
    // PHASE 1: POLYGON SOUP CLEANUP (already in soup space - no extraction!)
    // =========================================================================

    if (options.verbose) {
        std::cout << "[Phase 1] Polygon Soup Cleanup\n";
    }

    // Step 1.1: Remove duplicate points
    if (options.remove_duplicates) {
        if (options.verbose) {
            std::cout << "[1/6] Removing duplicate points...\n";
        }
        size_t points_before = soup.points.size();
        PMP::merge_duplicate_points_in_polygon_soup(soup.points, soup.polygons);
        stats.duplicates_merged = points_before - soup.points.size();
        if (options.verbose) {
            std::cout << "  Merged: " << stats.duplicates_merged << " duplicate points\n\n";
        }
    }

    // Step 1.2: Remove duplicate polygons
    if (options.remove_duplicates) {
        size_t polygons_before = soup.polygons.size();
        PMP::merge_duplicate_polygons_in_polygon_soup(soup.points, soup.polygons);
        if (options.debug && polygons_before > soup.polygons.size()) {
            std::cout << "  [DEBUG] Removed " << (polygons_before - soup.polygons.size()) << " duplicate polygons\n";
        }
    }

    // Step 1.3: Remove degenerate polygons
    if (options.verbose) {
        std::cout << "[2/6] Removing degenerate polygons...\n";
    }
    size_t before_degenerate = soup.polygons.size();
    auto it = std::remove_if(soup.polygons.begin(), soup.polygons.end(), [](const std::vector<std::size_t>& poly) {
        if (poly.size() < 3) return true;
        std::set<std::size_t> unique_verts(poly.begin(), poly.end());
        return unique_verts.size() < 3;
    });
    soup.polygons.erase(it, soup.polygons.end());
    if (options.verbose) {
        std::cout << "  Removed: " << (before_degenerate - soup.polygons.size()) << " degenerate polygons\n\n";
    }

    // Debug: Save mesh after removal
    if (options.debug)
        MeshPreprocessor::plyDump(soup, "debug_02_after_removal.ply", "Saved soup (after degenerate removal)");

    // Step 1.5: Remove 3-face fans
    if (options.remove_3_face_fans) {
        if (options.verbose) {
            std::cout << "[3/6] Collapsing 3-face fans...\n";
        }
        size_t fans_collapsed = PolygonSoupRepair::remove_3_face_fans(soup.points, soup.polygons);
        stats.face_fans_collapsed = fans_collapsed;
        if (options.verbose) {
            std::cout << "  Collapsed: " << fans_collapsed << " 3-face fan(s)\n\n";
        }
    }

    // Debug: Save mesh after fan removal
    if (options.debug)
        MeshPreprocessor::plyDump(soup, "debug_03_after_3_face_fans.ply", "Saved soup (after 3-face fans)");

    // Step 1.4: Remove non-manifold vertices/edges
    if (options.remove_non_manifold) {
        if (options.verbose) {
            std::cout << "[4/6] Removing non-manifold vertices/edges (recursive local search)...\n";
        }
        size_t max_depth = (options.non_manifold_passes > 0) ? options.non_manifold_passes : 10;
        auto nm_result = PolygonSoupRepair::remove_non_manifold_polygons_detailed(soup.polygons, max_depth, options.debug);
        stats.non_manifold_vertices_removed = nm_result.total_polygons_removed;
        if (options.verbose) {
            std::cout << "  Removed: " << nm_result.total_polygons_removed << " polygon(s) "
                      << "in " << nm_result.iterations_executed << " iteration(s)";
            if (nm_result.hit_max_iterations) {
                std::cout << " (hit max limit of " << max_depth << ")";
            }
            std::cout << "\n\n";
        }
    }

    // Debug: Save mesh after non-manifold removal
    if (options.debug)
        MeshPreprocessor::plyDump(soup, "debug_04_after_non_manifold_removal.ply",
                                  "Saved soup (after non-manifold removal)");

    // Step 1.6: Orient polygon soup
    if (options.verbose) {
        std::cout << "[5/6] Orienting polygon soup...\n";
    }
    bool oriented = PMP::orient_polygon_soup(soup.points, soup.polygons);
    if (options.verbose) {
        std::cout << (oriented ? "  Oriented successfully" : "  Warning: Some points duplicated during orientation") << "\n\n";
    }

    auto soup_end = std::chrono::high_resolution_clock::now();
    stats.soup_cleanup_time_ms = std::chrono::duration<double, std::milli>(soup_end - soup_start).count();

    // =========================================================================
    // PHASE 2: CONVERT TO MESH (ONCE!)
    // =========================================================================

    if (options.verbose) {
        std::cout << "[6/6] Converting soup to mesh (one-time conversion)...\n";
    }

    auto mesh_convert_start = std::chrono::high_resolution_clock::now();

    output_mesh.clear();
    PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, output_mesh);

    auto mesh_convert_end = std::chrono::high_resolution_clock::now();
    stats.soup_to_mesh_time_ms = std::chrono::duration<double, std::milli>(mesh_convert_end - mesh_convert_start).count();

    if (options.verbose) {
        std::cout << "  Mesh: " << output_mesh.number_of_vertices() << " vertices, "
                  << output_mesh.number_of_faces() << " faces\n\n";
    }

    // =========================================================================
    // PHASE 3: MESH-LEVEL CLEANUP (Quick operations requiring halfedge structure)
    // =========================================================================

    auto mesh_cleanup_start = std::chrono::high_resolution_clock::now();

    if (options.verbose) {
        std::cout << "[Phase 2] Mesh-Level Cleanup\n";
    }

    // Remove isolated vertices
    if (options.remove_isolated) {
        if (options.verbose) {
            std::cout << "[7/8] Removing isolated vertices...\n";
        }
        stats.isolated_vertices_removed = PMP::remove_isolated_vertices(output_mesh);
        if (options.verbose) {
            std::cout << "  Removed: " << stats.isolated_vertices_removed << " isolated vertices\n\n";
        }
    }

    // Keep largest component
    if (options.keep_largest_component) {
        if (options.verbose) {
            std::cout << "[8/8] Keeping only largest connected component...\n";
        }

        auto fccmap = output_mesh.add_property_map<face_descriptor, std::size_t>("f:CC").first;
        std::size_t num_components = PMP::connected_components(output_mesh, fccmap);
        stats.connected_components_found = num_components;

        if (num_components > 1) {
            std::map<std::size_t, std::size_t> component_sizes;
            for (auto f : output_mesh.faces()) {
                component_sizes[fccmap[f]]++;
            }

            std::size_t largest_id = 0;
            std::size_t largest_size = 0;
            for (const auto& pair : component_sizes) {
                if (pair.second > largest_size) {
                    largest_id = pair.first;
                    largest_size = pair.second;
                }
            }

            std::vector<face_descriptor> faces_to_remove;
            for (auto f : output_mesh.faces()) {
                if (fccmap[f] != largest_id) {
                    faces_to_remove.push_back(f);
                }
            }

            for (auto f : faces_to_remove) {
                if (!output_mesh.is_removed(f)) {
                    CGAL::Euler::remove_face(CGAL::halfedge(f, output_mesh), output_mesh);
                }
            }

            output_mesh.remove_property_map(fccmap);
            PMP::remove_isolated_vertices(output_mesh);
            stats.small_components_removed = num_components - 1;
        }

        if (options.verbose) {
            std::cout << "  Found: " << stats.connected_components_found << " component(s)\n";
            std::cout << "  Removed: " << stats.small_components_removed << " small component(s)\n\n";
        }
    }

    auto mesh_cleanup_end = std::chrono::high_resolution_clock::now();
    stats.mesh_cleanup_time_ms = std::chrono::duration<double, std::milli>(mesh_cleanup_end - mesh_cleanup_start).count();

    // Collect garbage
    if (output_mesh.has_garbage()) {
        if (options.verbose) {
            std::cout << "Collecting garbage (compacting mesh)...\n";
        }
        output_mesh.collect_garbage();
    }

    // Validate
    if (options.verbose) {
        std::cout << "Validating mesh topology...\n";
    }
    bool is_valid = CGAL::is_valid_polygon_mesh(output_mesh, options.verbose);
    if (!is_valid && options.verbose) {
        std::cout << "WARNING: Mesh is not valid after preprocessing!\n";
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    stats.total_time_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

    if (options.verbose) {
        std::cout << "Final mesh state:\n";
        std::cout << "  Vertices: " << output_mesh.number_of_vertices() << "\n";
        std::cout << "  Faces: " << output_mesh.number_of_faces() << "\n";
        std::cout << "  Valid: " << (is_valid ? "YES" : "NO") << "\n";
        std::cout << "  Timing breakdown:\n";
        std::cout << "    Soup cleanup: " << std::fixed << std::setprecision(2) << stats.soup_cleanup_time_ms << " ms\n";
        std::cout << "    Soup->Mesh conversion: " << stats.soup_to_mesh_time_ms << " ms\n";
        std::cout << "    Mesh cleanup: " << stats.mesh_cleanup_time_ms << " ms\n";
        std::cout << "    Total: " << stats.total_time_ms << " ms\n";
        std::cout << "========================================\n\n";
    }

    return stats;
}

void
MeshPreprocessor::plyDump(MeshRepair::PolygonSoup& soup, const std::string debug_file, const std::string message)
{
    Mesh debug_mesh;
    PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, debug_mesh);
    if (CGAL::IO::write_PLY(debug_file, debug_mesh, CGAL ::parameters::use_binary_mode(true))) {
        std::cout << "  [DEBUG] " << message << " : " << debug_file << "\n ";
        std::cout << "  [DEBUG]   Mesh: " << debug_mesh.number_of_vertices() << " vertices, "
                  << debug_mesh.number_of_faces() << " faces\n";
    }
}

}  // namespace MeshRepair
