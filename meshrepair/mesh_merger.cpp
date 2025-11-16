#include "mesh_merger.h"
#include "polygon_soup_repair.h"
#include <CGAL/Polygon_mesh_processing/polygon_mesh_to_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <iostream>

namespace MeshRepair {

namespace PMP = CGAL::Polygon_mesh_processing;

Mesh
MeshMerger::merge_submeshes(const Mesh& original_mesh, const std::vector<Submesh>& submeshes, bool verbose)
{
    if (verbose) {
        std::cout << "[Merger] Merging " << submeshes.size() << " submesh(es) "
                  << "back into original mesh...\n";
    }

    // Step 1: Convert original mesh and all submeshes to polygon soups
    std::vector<PolygonSoup> soups;
    soups.reserve(submeshes.size() + 1);

    // Add original mesh first
    soups.push_back(to_soup(original_mesh));

    size_t total_points   = soups.back().points.size();
    size_t total_polygons = soups.back().polygons.size();

    // Add all filled submeshes
    for (const auto& submesh : submeshes) {
        soups.push_back(to_soup(submesh.mesh));
        total_points += soups.back().points.size();
        total_polygons += soups.back().polygons.size();
    }

    if (verbose) {
        std::cout << "[Merger] Converted to soups: " << total_points << " points, " << total_polygons
                  << " polygons total\n";
        std::cout << "[Merger]   Original mesh: " << soups[0].points.size() << " points, " << soups[0].polygons.size()
                  << " polygons\n";
        std::cout << "[Merger]   Filled submeshes: " << (total_points - soups[0].points.size()) << " points, "
                  << (total_polygons - soups[0].polygons.size()) << " polygons\n";
    }

    // Step 2: Combine all soups into one
    PolygonSoup combined;
    combined.points.reserve(total_points);
    combined.polygons.reserve(total_polygons);

    for (const auto& soup : soups) {
        size_t point_offset = combined.points.size();

        // Add points (efficient bulk insert)
        combined.points.insert(combined.points.end(), soup.points.begin(), soup.points.end());

        // Add polygons with adjusted indices
        // Pre-allocate space for all polygons from this soup
        size_t poly_start = combined.polygons.size();
        combined.polygons.resize(poly_start + soup.polygons.size());

        // Use indexed access to enable potential parallelization
        for (size_t p = 0; p < soup.polygons.size(); ++p) {
            const auto& polygon = soup.polygons[p];
            std::vector<size_t> adjusted_polygon(polygon.size());

            // Adjust indices
            for (size_t v = 0; v < polygon.size(); ++v) {
                adjusted_polygon[v] = polygon[v] + point_offset;
            }

            combined.polygons[poly_start + p] = std::move(adjusted_polygon);
        }
    }

    if (verbose) {
        std::cout << "[Merger] Combined soup: " << combined.points.size() << " points, " << combined.polygons.size()
                  << " polygons\n";
    }

    // Step 3: Repair soup (removes duplicate vertices at boundaries!)
    PMP::repair_polygon_soup(combined.points, combined.polygons);

    if (verbose) {
        std::cout << "[Merger] After repair: " << combined.points.size() << " points "
                  << "(duplicates merged)\n";
    }

    // Step 4: Remove non-manifold polygons (MUST be done before orient!)
    size_t removed_non_manifold = PolygonSoupRepair::remove_non_manifold_polygons(combined.polygons);

    if (removed_non_manifold > 0) {
        if (verbose) {
            std::cout << "[Merger] Removed " << removed_non_manifold << " non-manifold polygon(s)\n";
        }
    }

    // Step 5: Orient soup (required for mesh conversion)
    bool oriented = PMP::orient_polygon_soup(combined.points, combined.polygons);

    if (!oriented && verbose) {
        std::cout << "[Merger] Warning: Some points were duplicated during orientation\n";
    }

    // Step 6: Convert back to mesh
    Mesh result = soup_to_mesh(combined);

    if (verbose) {
        std::cout << "[Merger] Final mesh: " << result.number_of_vertices() << " vertices, " << result.number_of_faces()
                  << " faces\n";
    }

    return result;
}

MeshMerger::PolygonSoup
MeshMerger::to_soup(const Mesh& mesh)
{
    PolygonSoup soup;

    PMP::polygon_mesh_to_polygon_soup(mesh, soup.points, soup.polygons);

    return soup;
}

Mesh
MeshMerger::soup_to_mesh(const PolygonSoup& soup)
{
    Mesh mesh;

    PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, mesh);

    return mesh;
}

}  // namespace MeshRepair
