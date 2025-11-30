#pragma once

#include "types.h"
#include "config.h"
#include <atomic>
#include <set>
#include <vector>

namespace MeshRepair {

struct HoleInfo {
    halfedge_descriptor boundary_halfedge;
    std::vector<vertex_descriptor> boundary_vertices;
    size_t boundary_size      = 0;
    double estimated_diameter = 0.0;
    double estimated_area     = 0.0;
};

struct HoleDetectorCtx {
    const Mesh* mesh = nullptr;
    bool verbose     = false;
};

int
detect_all_holes_ctx(const HoleDetectorCtx& ctx, std::vector<HoleInfo>& out_holes);
int
detect_all_holes_c(const Mesh& mesh, bool verbose, std::vector<HoleInfo>& out_holes);
bool
is_border_halfedge(const Mesh& mesh, halfedge_descriptor h);
HoleInfo
analyze_hole(const Mesh& mesh, halfedge_descriptor border_h);
size_t
count_border_edges(const Mesh& mesh);

struct FillingOptions {
    unsigned int fairing_continuity   = Config::DEFAULT_FAIRING_CONTINUITY;
    size_t max_hole_boundary_vertices = Config::DEFAULT_MAX_HOLE_BOUNDARY;
    double max_hole_diameter_ratio    = Config::DEFAULT_MAX_HOLE_DIAMETER_RATIO;
    bool use_2d_cdt                   = Config::DEFAULT_USE_2D_CDT;
    bool use_3d_delaunay              = Config::DEFAULT_USE_3D_DELAUNAY;
    bool skip_cubic_search            = Config::DEFAULT_SKIP_CUBIC;
    bool refine                       = Config::DEFAULT_REFINE;
    std::set<uint32_t> selection_boundary_vertices;
    bool guard_selection_boundary       = true;
    double reference_bbox_diagonal      = 0.0;
    bool keep_largest_component         = true;
    size_t min_partition_boundary_edges = Config::DEFAULT_MIN_PARTITION_EDGES;
    bool verbose                        = false;
    bool show_progress                  = true;
    bool holes_only                     = false;
};

struct HoleFillerCtx {
    Mesh* mesh                     = nullptr;
    FillingOptions options         = FillingOptions {};
    std::atomic<bool>* cancel_flag = nullptr;
};

HoleStatistics
fill_hole_ctx(HoleFillerCtx* ctx, const HoleInfo& hole);
MeshStatistics
fill_all_holes_ctx(HoleFillerCtx* ctx, const std::vector<HoleInfo>& holes);
int
fill_holes_c(Mesh& mesh, const FillingOptions& options, const std::vector<HoleInfo>& holes, MeshStatistics* out_stats);

}  // namespace MeshRepair
