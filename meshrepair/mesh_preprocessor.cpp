#include "mesh_preprocessor.h"
#include "parallel_detection.h"
#include "threadpool.h"
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/merge_border_vertices.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/boost/graph/Euler_operations.h>
#include <CGAL/boost/graph/selection.h>
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
    : mesh_(mesh), options_(options), stats_()
{
}

PreprocessingStats MeshPreprocessor::preprocess() {
    auto start_time = std::chrono::high_resolution_clock::now();

    if (options_.verbose) {
        std::cout << "\n=== Mesh Preprocessing ===\n";
        std::cout << "Initial mesh state:\n";
        std::cout << "  Vertices: " << mesh_.number_of_vertices() << "\n";
        std::cout << "  Faces: " << mesh_.number_of_faces() << "\n\n";
    }

    // Step 1: Remove duplicate vertices
    if (options_.remove_duplicates) {
        if (options_.verbose) {
            std::cout << "[1/4] Removing duplicate vertices...\n";
        }
        stats_.duplicates_merged = remove_duplicate_vertices();
        if (options_.verbose) {
            std::cout << "  Merged: " << stats_.duplicates_merged << " vertices\n";
            std::cout << "\n";
        }

        // NOTE: Degenerate faces are already removed during polygon soup processing
        // in remove_duplicate_vertices(), so no need to call remove_degenerate_faces() here

        // Debug: dump intermediate mesh
        if (options_.debug) {
            // Collect garbage before saving to ensure valid indices
            if (mesh_.has_garbage()) {
                mesh_.collect_garbage();
            }
            std::string debug_file = "debug_01_after_duplicates.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
                if (options_.verbose) {
                    std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
                }
            } else {
                std::cout << "  [DEBUG] Warning: Failed to save " << debug_file << "\n\n";
            }
        }
    }

    // Step 2: Remove non-manifold vertices (with multiple passes)
    if (options_.remove_non_manifold) {
        if (options_.verbose) {
            std::cout << "[2/4] Removing non-manifold vertices (up to "
                      << options_.non_manifold_passes << " passes)...\n";
        }

        // Perform multiple passes to handle newly created non-manifold vertices
        size_t total_removed = 0;
        size_t pass = 0;

        for (pass = 0; pass < options_.non_manifold_passes; ++pass) {
            // Detect non-manifold vertices (use parallel if thread pool available)
            std::vector<halfedge_descriptor> nm_halfedges;

            // Note: CGAL's non_manifold_vertices must work on whole mesh (not parallelizable directly)
            // But we can make the check faster by early exit in future optimization
            PMP::non_manifold_vertices(mesh_, std::back_inserter(nm_halfedges));

            if (pass == 0) {
                stats_.non_manifold_vertices_found = nm_halfedges.size();
            }

            if (nm_halfedges.empty()) {
                if (options_.verbose) {
                    std::cout << "  Pass " << (pass + 1) << ": No non-manifold vertices found\n";
                }
                break;  // No more non-manifold vertices
            }

            if (options_.verbose) {
                std::cout << "  Pass " << (pass + 1) << ": Found " << nm_halfedges.size()
                          << " non-manifold vertices\n";
            }

            // Remove them
            size_t removed_this_pass = remove_non_manifold_vertices();
            total_removed += removed_this_pass;

            if (options_.verbose) {
                std::cout << "  Pass " << (pass + 1) << ": Removed " << removed_this_pass
                          << " vertices\n";
            }

            // If nothing was removed, no point in continuing
            if (removed_this_pass == 0) {
                break;
            }
        }

        stats_.non_manifold_vertices_removed = total_removed;
        stats_.non_manifold_passes_executed = pass + 1;

        if (options_.verbose) {
            std::cout << "  Total removed: " << total_removed << " vertices in "
                      << stats_.non_manifold_passes_executed << " pass(es)\n\n";
        }

        // Debug: dump intermediate mesh
        if (options_.debug) {
            // Collect garbage before saving to ensure valid indices
            if (mesh_.has_garbage()) {
                mesh_.collect_garbage();
            }
            std::string debug_file = "debug_02_after_nonmanifold.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
                if (options_.verbose) {
                    std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
                }
            } else {
                std::cout << "  [DEBUG] Warning: Failed to save " << debug_file << "\n\n";
            }
        }
    }

    // Step 3: Remove isolated vertices (cleanup)
    if (options_.remove_isolated) {
        if (options_.verbose) {
            std::cout << "[3/4] Removing isolated vertices...\n";
        }
        stats_.isolated_vertices_removed = remove_isolated_vertices();
        if (options_.verbose) {
            std::cout << "  Removed: " << stats_.isolated_vertices_removed
                      << " isolated vertices\n\n";
        }

        // Debug: dump intermediate mesh
        if (options_.debug) {
            // Collect garbage before saving to ensure valid indices
            if (mesh_.has_garbage()) {
                mesh_.collect_garbage();
            }
            std::string debug_file = "debug_03_after_isolated.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
                if (options_.verbose) {
                    std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
                }
            } else {
                std::cout << "  [DEBUG] Warning: Failed to save " << debug_file << "\n\n";
            }
        }
    }

    // Step 4: Keep only largest connected component
    if (options_.keep_largest_component) {
        if (options_.verbose) {
            std::cout << "[4/4] Keeping only largest connected component...\n";
        }
        stats_.small_components_removed = keep_only_largest_connected_component();
        if (options_.verbose) {
            std::cout << "  Found: " << stats_.connected_components_found << " component(s)\n";
            std::cout << "  Removed: " << stats_.small_components_removed << " small component(s)\n\n";
        }

        // Debug: dump intermediate mesh
        if (options_.debug) {
            // Collect garbage before saving to ensure valid indices
            if (mesh_.has_garbage()) {
                mesh_.collect_garbage();
            }
            std::string debug_file = "debug_04_after_components.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh_, CGAL::parameters::use_binary_mode(true))) {
                if (options_.verbose) {
                    std::cout << "  [DEBUG] Saved: " << debug_file << "\n\n";
                }
            } else {
                std::cout << "  [DEBUG] Warning: Failed to save " << debug_file << "\n\n";
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
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
        std::cout << "  Time: " << std::fixed << std::setprecision(2)
                  << stats_.total_time_ms << " ms\n";
        std::cout << "==========================\n\n";
    }

    return stats_;
}

size_t MeshPreprocessor::remove_duplicate_vertices() {
    size_t initial_vertices = mesh_.number_of_vertices();

    if (options_.debug) {
        std::cout << "  [DEBUG] Starting duplicate removal:\n";
        std::cout << "  [DEBUG]   Initial vertices: " << initial_vertices << "\n";
        std::cout << "  [DEBUG]   Initial faces: " << mesh_.number_of_faces() << "\n";
    }

    // PROPER METHOD: Convert mesh to polygon soup, clean it, rebuild mesh
    // This is the ONLY SAFE way to remove ALL duplicate vertices (boundary AND interior)

    // Step 1: Extract polygon soup from mesh
    std::vector<Point_3> points;
    std::vector<std::vector<std::size_t>> polygons;

    // Extract vertices
    std::map<vertex_descriptor, std::size_t> vertex_index_map;
    std::size_t index = 0;
    for (auto v : mesh_.vertices()) {
        vertex_index_map[v] = index++;
        points.push_back(mesh_.point(v));
    }

    // Extract faces
    for (auto f : mesh_.faces()) {
        std::vector<std::size_t> polygon;
        for (auto v : CGAL::vertices_around_face(CGAL::halfedge(f, mesh_), mesh_)) {
            polygon.push_back(vertex_index_map[v]);
        }
        polygons.push_back(polygon);
    }

    if (options_.debug) {
        std::cout << "  [DEBUG] Extracted polygon soup:\n";
        std::cout << "  [DEBUG]   Points: " << points.size() << "\n";
        std::cout << "  [DEBUG]   Polygons: " << polygons.size() << "\n";
    }

    // Step 2: Merge duplicate points in polygon soup
    // IMPORTANT: This will create non-manifold vertices in the resulting mesh,
    // which will be deleted in the next preprocessing step (remove_non_manifold_vertices)
    PMP::merge_duplicate_points_in_polygon_soup(points, polygons);

    // COUNT HERE: After merge, track how many duplicates were removed
    size_t vertices_after_merge = points.size();

    if (options_.debug) {
        std::cout << "  [DEBUG] After merge_duplicate_points_in_polygon_soup:\n";
        std::cout << "  [DEBUG]   Points: " << vertices_after_merge << "\n";
        std::cout << "  [DEBUG]   Polygons: " << polygons.size() << "\n";
        std::cout << "  [DEBUG]   Duplicates merged: " << (initial_vertices - vertices_after_merge) << "\n";
    }

    // Step 3: Merge duplicate polygons
    size_t before_polygon_merge = polygons.size();
    PMP::merge_duplicate_polygons_in_polygon_soup(points, polygons);

    if (options_.debug) {
        std::cout << "  [DEBUG] After merge_duplicate_polygons_in_polygon_soup:\n";
        std::cout << "  [DEBUG]   Duplicate polygons removed: " << (before_polygon_merge - polygons.size()) << "\n";
    }

    // Step 4: CRITICAL - Remove degenerate polygons manually
    // merge_duplicate_points can create degenerate faces (e.g., [v0, v42, v42])
    // We MUST remove these, but we DON'T use repair_polygon_soup() because
    // it would split non-manifold vertices back to original state!
    size_t before_cleanup = polygons.size();
    auto it = std::remove_if(polygons.begin(), polygons.end(),
        [](const std::vector<std::size_t>& poly) {
            // Remove polygons with < 3 vertices
            if (poly.size() < 3) return true;

            // Remove polygons with < 3 UNIQUE vertices (degenerates)
            std::set<std::size_t> unique_verts(poly.begin(), poly.end());
            return unique_verts.size() < 3;
        });
    polygons.erase(it, polygons.end());

    if (options_.debug) {
        std::cout << "  [DEBUG] After manual degenerate removal:\n";
        std::cout << "  [DEBUG]   Degenerate faces removed: " << (before_cleanup - polygons.size()) << "\n";
        std::cout << "  [DEBUG]   Polygons remaining: " << polygons.size() << "\n";
    }

    // Step 5: DO NOT orient polygons here!
    // orient_polygon_soup() duplicates points to make the soup manifold,
    // which would UNDO our duplicate removal. We want non-manifold vertices
    // to be DELETED in the next step, not split back to original state.
    // Orientation will be handled later if needed.

    if (options_.debug) {
        std::cout << "  [DEBUG] Skipping orient_polygon_soup (would duplicate points)\n";
        std::cout << "  [DEBUG]   Points: " << points.size() << "\n";
        std::cout << "  [DEBUG]   Polygons: " << polygons.size() << "\n";
    }

    // Step 6: Validate soup before conversion (DEBUG SAFETY CHECK)
    if (options_.verbose || options_.debug) {
        bool is_valid = PMP::is_polygon_soup_a_polygon_mesh(polygons);
        if (!is_valid) {
            std::cout << "  [ERROR] Polygon soup validation failed!\n";
            std::cout << "  The soup may have non-manifold edges or invalid topology.\n";
            std::cout << "  Attempting to continue anyway...\n";
            // Don't abort - we'll try to build the mesh and let non-manifold removal fix it
        } else if (options_.debug) {
            std::cout << "  [DEBUG] Polygon soup validation: PASSED\n";
        }
    }

    // Step 7: Rebuild mesh from polygon soup
    // NOTE: This WILL create non-manifold vertices (expected behavior)
    // They will be removed in the next preprocessing step
    mesh_.clear();
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh_);

    if (options_.debug) {
        std::cout << "  [DEBUG] After polygon_soup_to_polygon_mesh:\n";
        std::cout << "  [DEBUG]   Final vertices: " << mesh_.number_of_vertices() << "\n";
        std::cout << "  [DEBUG]   Final faces: " << mesh_.number_of_faces() << "\n";
    }

    // Return count of duplicates merged (before repair, not after final cleanup)
    return initial_vertices > vertices_after_merge ?
           (initial_vertices - vertices_after_merge) : 0;
}

size_t MeshPreprocessor::remove_non_manifold_vertices() {
    // Detect all non-manifold vertices
    std::vector<halfedge_descriptor> nm_halfedges;
    PMP::non_manifold_vertices(mesh_, std::back_inserter(nm_halfedges));

    if (nm_halfedges.empty()) {
        return 0;
    }

    // Collect unique non-manifold vertices
    std::set<vertex_descriptor> nm_vertices;
    for (auto h : nm_halfedges) {
        nm_vertices.insert(CGAL::target(h, mesh_));
    }

    // Collect all faces incident to non-manifold vertices
    std::set<face_descriptor> faces_to_remove;
    for (auto v : nm_vertices) {
        for (auto h : CGAL::halfedges_around_target(v, mesh_)) {
            if (!mesh_.is_border(h)) {
                face_descriptor f = mesh_.face(h);
                if (f != boost::graph_traits<Mesh>::null_face()) {
                    faces_to_remove.insert(f);
                }
            }
        }
    }

    if (faces_to_remove.empty()) {
        return 0;
    }

    // Create a face selection property map
    std::unordered_map<face_descriptor, bool> face_selection_map;
    for (auto f : mesh_.faces()) {
        face_selection_map[f] = (faces_to_remove.count(f) > 0);
    }
    auto is_selected = boost::make_assoc_property_map(face_selection_map);

    // CRITICAL: Expand the face selection to avoid creating new non-manifold vertices
    // This is the proper CGAL way to handle non-manifold removal
    CGAL::expand_face_selection_for_removal(faces_to_remove, mesh_, is_selected);

    // Collect expanded selection
    std::vector<face_descriptor> expanded_faces;
    for (auto f : mesh_.faces()) {
        if (face_selection_map[f]) {
            expanded_faces.push_back(f);
        }
    }

    // Remove the faces using CGAL's proper method
    for (auto f : expanded_faces) {
        if (!mesh_.is_removed(f)) {
            CGAL::Euler::remove_face(CGAL::halfedge(f, mesh_), mesh_);
        }
    }

    // IMPORTANT: After removing faces, vertices/edges become isolated but are NOT automatically removed
    // CGAL::Euler::remove_face() may remove some isolated vertices automatically,
    // but we need to clean up any remaining isolated vertices

    // Remove isolated vertices created by face removal
    // NOTE: PMP::remove_isolated_vertices() only removes vertices with NO halfedges
    // Vertices with border halfedges are NOT considered isolated by CGAL
    size_t removed_count = PMP::remove_isolated_vertices(mesh_);

    return removed_count;
}

size_t MeshPreprocessor::remove_isolated_vertices() {
    return PMP::remove_isolated_vertices(mesh_);
}

size_t MeshPreprocessor::keep_only_largest_connected_component() {
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
    std::size_t largest_component_id = 0;
    std::size_t largest_component_size = 0;
    for (const auto& pair : component_sizes) {
        if (pair.second > largest_component_size) {
            largest_component_id = pair.first;
            largest_component_size = pair.second;
        }
    }

    if (options_.verbose) {
        std::cout << "  Largest component ID: " << largest_component_id
                  << " with " << largest_component_size << " faces\n";
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

void MeshPreprocessor::print_report() const {
    std::cout << "\n=== Preprocessing Report ===\n";
    std::cout << "Duplicate vertices merged: " << stats_.duplicates_merged << "\n";
    std::cout << "Non-manifold vertices (initial): " << stats_.non_manifold_vertices_found << "\n";
    std::cout << "Non-manifold vertices removed: " << stats_.non_manifold_vertices_removed;
    if (stats_.non_manifold_passes_executed > 0) {
        std::cout << " (in " << stats_.non_manifold_passes_executed << " pass(es))";
    }
    std::cout << "\n";
    std::cout << "Isolated vertices removed: " << stats_.isolated_vertices_removed << "\n";
    std::cout << "Connected components found: " << stats_.connected_components_found << "\n";
    std::cout << "Small components removed: " << stats_.small_components_removed << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2)
              << stats_.total_time_ms << " ms\n";
    std::cout << "============================\n\n";
}

} // namespace MeshRepair
