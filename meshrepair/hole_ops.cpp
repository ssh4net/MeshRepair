#include "hole_ops.h"
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/bounding_box.h>
#include <chrono>
#include <string>
#include <unordered_set>
#include "include/logger.h"

namespace PMP = CGAL::Polygon_mesh_processing;

namespace MeshRepair {

int
detect_all_holes_ctx(const HoleDetectorCtx& ctx, std::vector<HoleInfo>& out_holes)
{
    out_holes.clear();
    if (!ctx.mesh) {
        return -1;
    }

    const Mesh& mesh = *ctx.mesh;
    std::unordered_set<halfedge_descriptor> processed;

    for (auto h : mesh.halfedges()) {
        if (mesh.is_border(h) && processed.find(h) == processed.end()) {
            HoleInfo info = analyze_hole(mesh, h);
            out_holes.push_back(info);

            auto h_current = h;
            do {
                processed.insert(h_current);
                h_current = mesh.next(h_current);
            } while (h_current != h);
        }
    }

    if (ctx.verbose) {
        if (!out_holes.empty()) {
            logDetail(LogCategory::Fill, "Detected " + std::to_string(out_holes.size()) + " hole(s)");
        } else {
            logDetail(LogCategory::Fill, "No holes detected. Mesh is closed.");
        }
    }

    return 0;
}

bool
is_border_halfedge(const Mesh& mesh, halfedge_descriptor h)
{
    return mesh.is_border(h);
}

HoleInfo
analyze_hole(const Mesh& mesh, halfedge_descriptor border_h)
{
    HoleInfo info;
    info.boundary_halfedge = border_h;

    auto h = border_h;
    do {
        info.boundary_vertices.push_back(mesh.target(h));
        h = mesh.next(h);
    } while (h != border_h);

    info.boundary_size = info.boundary_vertices.size();

    std::vector<Point_3> boundary_points;
    boundary_points.reserve(info.boundary_vertices.size());

    for (auto v : info.boundary_vertices) {
        boundary_points.push_back(mesh.point(v));
    }

    auto bbox               = CGAL::bounding_box(boundary_points.begin(), boundary_points.end());
    auto diag_squared       = CGAL::squared_distance(bbox.min(), bbox.max());
    info.estimated_diameter = std::sqrt(CGAL::to_double(diag_squared));

    double radius       = info.estimated_diameter / 2.0;
    info.estimated_area = 3.14159 * radius * radius;

    return info;
}

size_t
count_border_edges(const Mesh& mesh)
{
    size_t count = 0;
    for (auto h : mesh.halfedges()) {
        if (mesh.is_border(h)) {
            ++count;
        }
    }
    return count;
}

int
detect_all_holes_c(const Mesh& mesh, bool verbose, std::vector<HoleInfo>& out_holes)
{
    HoleDetectorCtx ctx { &mesh, verbose };
    return detect_all_holes_ctx(ctx, out_holes);
}

namespace {
    double compute_mesh_bbox_diagonal(const Mesh& mesh)
    {
        if (mesh.number_of_vertices() == 0) {
            return 0.0;
        }

        std::vector<Point_3> all_points;
        all_points.reserve(mesh.number_of_vertices());
        for (auto v : mesh.vertices()) {
            all_points.push_back(mesh.point(v));
        }

        auto bbox         = CGAL::bounding_box(all_points.begin(), all_points.end());
        auto diag_squared = CGAL::squared_distance(bbox.min(), bbox.max());
        return std::sqrt(CGAL::to_double(diag_squared));
    }

    bool should_skip_hole(const HoleFillerCtx& ctx, const HoleInfo& hole)
    {
        const auto& options = ctx.options;

        if (hole.boundary_size > options.max_hole_boundary_vertices) {
            return true;
        }

        if (options.guard_selection_boundary && !options.selection_boundary_vertices.empty()) {
            bool all_boundary = true;
            for (const auto& v : hole.boundary_vertices) {
                uint32_t v_idx = static_cast<uint32_t>(v.idx());
                if (options.selection_boundary_vertices.find(v_idx) == options.selection_boundary_vertices.end()) {
                    all_boundary = false;
                    break;
                }
            }
            if (all_boundary) {
                if (options.verbose) {
                    logDetail(LogCategory::Fill, "Skipping hole (selection boundary): all "
                                                     + std::to_string(hole.boundary_size)
                                                     + " vertices are on selection boundary");
                }
                return true;
            }
        }

        double ref_diagonal = options.reference_bbox_diagonal > 0.0 ? options.reference_bbox_diagonal
                                                                    : compute_mesh_bbox_diagonal(*ctx.mesh);
        if (ref_diagonal > 0.0 && hole.estimated_diameter > ref_diagonal * options.max_hole_diameter_ratio) {
            return true;
        }

        return false;
    }
}  // namespace

HoleStatistics
fill_hole_ctx(HoleFillerCtx* ctx, const HoleInfo& hole)
{
    HoleStatistics stats;
    if (!ctx || !ctx->mesh) {
        return stats;
    }

    Mesh& mesh                  = *ctx->mesh;
    const auto& options         = ctx->options;
    stats.num_boundary_vertices = hole.boundary_size;
    stats.hole_area             = hole.estimated_area;
    stats.hole_diameter         = hole.estimated_diameter;

    auto start_time = std::chrono::high_resolution_clock::now();

    if (should_skip_hole(*ctx, hole)) {
        if (options.verbose) {
            logDetail(LogCategory::Fill,
                      "Skipping hole: " + std::to_string(hole.boundary_size) + " boundary vertices (guard/limits)");
        }
        stats.filled_successfully = false;
        return stats;
    }

    std::vector<face_descriptor> patch_faces;
    std::vector<vertex_descriptor> patch_vertices;

    try {
        auto result = PMP::triangulate_refine_and_fair_hole(
            mesh, hole.boundary_halfedge,
            CGAL::parameters::face_output_iterator(std::back_inserter(patch_faces))
                .vertex_output_iterator(std::back_inserter(patch_vertices))
                .use_2d_constrained_delaunay_triangulation(options.use_2d_cdt)
                .use_delaunay_triangulation(options.use_3d_delaunay)
                .do_not_use_cubic_algorithm(options.skip_cubic_search)
                .fairing_continuity(options.fairing_continuity));

        bool triangulation_success = std::get<0>(result);
        bool fairing_success       = triangulation_success;

        if (triangulation_success) {
            stats.num_faces_added     = patch_faces.size();
            stats.num_vertices_added  = patch_vertices.size();
            stats.filled_successfully = true;
            stats.fairing_succeeded   = fairing_success;

            if (options.verbose) {
                std::string msg = "Filled: " + std::to_string(stats.num_faces_added) + " faces, "
                                  + std::to_string(stats.num_vertices_added) + " vertices added";
                if (!fairing_success) {
                    msg += " [FAIRING FAILED]";
                }
                logDetail(LogCategory::Fill, msg);
            }
        } else {
            stats.filled_successfully = false;
            stats.fairing_succeeded   = false;
            stats.error_message       = "CGAL triangulation failed (possibly degenerate or self-intersecting boundary)";

            if (options.verbose) {
                logDetail(LogCategory::Fill, "Failed to triangulate hole (boundary may be degenerate)");
            }
        }
    } catch (const std::exception& e) {
        stats.filled_successfully = false;
        stats.fairing_succeeded   = false;
        stats.error_message       = e.what();

        if (options.verbose) {
            logDetail(LogCategory::Fill, std::string("Exception during hole filling: ") + e.what());
        }
    }

    auto end_time      = std::chrono::high_resolution_clock::now();
    stats.fill_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return stats;
}

MeshStatistics
fill_all_holes_ctx(HoleFillerCtx* ctx, const std::vector<HoleInfo>& holes)
{
    MeshStatistics mesh_stats;
    if (!ctx || !ctx->mesh) {
        return mesh_stats;
    }

    Mesh& mesh          = *ctx->mesh;
    const auto& options = ctx->options;

    mesh_stats.original_vertices  = mesh.number_of_vertices();
    mesh_stats.original_faces     = mesh.number_of_faces();
    mesh_stats.num_holes_detected = holes.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    if (holes.empty()) {
        if (options.verbose) {
            logDetail(LogCategory::Fill, "No holes to fill.");
        }
        mesh_stats.final_vertices = mesh_stats.original_vertices;
        mesh_stats.final_faces    = mesh_stats.original_faces;
        return mesh_stats;
    }

    if (options.verbose) {
        logDetail(LogCategory::Fill, "Filling " + std::to_string(holes.size()) + " hole(s)...");
    }

    for (size_t i = 0; i < holes.size(); ++i) {
        if (options.verbose) {
            logDetail(LogCategory::Fill, "Hole " + std::to_string(i + 1) + "/" + std::to_string(holes.size()) + " ("
                                             + std::to_string(holes[i].boundary_size) + " boundary vertices)");
        }

        HoleStatistics hole_stats = fill_hole_ctx(ctx, holes[i]);
        mesh_stats.hole_details.push_back(hole_stats);

        if (hole_stats.filled_successfully) {
            mesh_stats.num_holes_filled++;
        } else {
            if (should_skip_hole(*ctx, holes[i])) {
                mesh_stats.num_holes_skipped++;
            } else {
                mesh_stats.num_holes_failed++;
            }
        }
    }

    auto end_time            = std::chrono::high_resolution_clock::now();
    mesh_stats.fill_time_ms  = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    mesh_stats.total_time_ms = mesh_stats.fill_time_ms;

    mesh_stats.final_vertices = mesh.number_of_vertices();
    mesh_stats.final_faces    = mesh.number_of_faces();

    if (options.verbose) {
        logDetail(LogCategory::Fill, "=== Hole Filling Summary ===");
        logDetail(LogCategory::Fill, "Filled successfully: " + std::to_string(mesh_stats.num_holes_filled));
        logDetail(LogCategory::Fill, "Failed: " + std::to_string(mesh_stats.num_holes_failed));
        logDetail(LogCategory::Fill, "Skipped (too large): " + std::to_string(mesh_stats.num_holes_skipped));
        logDetail(LogCategory::Fill, "Faces added: " + std::to_string(mesh_stats_total_faces_added(mesh_stats)));
        logDetail(LogCategory::Fill, "Vertices added: " + std::to_string(mesh_stats_total_vertices_added(mesh_stats)));
        logDetail(LogCategory::Fill, "Total time: " + std::to_string(mesh_stats.total_time_ms) + " ms");
    }

    return mesh_stats;
}

int
fill_holes_c(Mesh& mesh, const FillingOptions& options, const std::vector<HoleInfo>& holes, MeshStatistics* out_stats)
{
    HoleFillerCtx ctx;
    ctx.mesh    = &mesh;
    ctx.options = options;

    MeshStatistics stats = fill_all_holes_ctx(&ctx, holes);
    if (out_stats) {
        *out_stats = stats;
    }
    return 0;
}

}  // namespace MeshRepair
