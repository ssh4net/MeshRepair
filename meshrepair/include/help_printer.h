#pragma once

#include "config.h"
#include "logger.h"
#include <sstream>

namespace MeshRepair {

inline void
print_help(const char* program_name)
{
    std::ostringstream help;
    help << "\n"
         << "MeshRepair v" << Config::VERSION << "\n"
         << "Built: " << Config::BUILD_DATE << " " << Config::BUILD_TIME << " (" << MESHREPAIR_BUILD_CONFIG << ")\n"
         << "Mesh hole filling tool (Liepa 2003 with Laplacian fairing)\n\n"
         << "Usage:\n"
         << "  CLI mode:    " << program_name << " <input> <output> [options]\n"
         << "  Engine mode: " << program_name << " --engine [engine-options]\n"
         << "\n"
         << "CLI Mode:\n"
         << "  Traditional command-line mesh repair tool.\n"
         << "  Provide input/output mesh files and optional processing flags.\n"
         << "\n"
         << "Engine Mode:\n"
         << "  IPC engine for Blender and other integrations (stdin/stdout or socket).\n"
         << "  Communicates via binary-framed JSON messages.\n"
         << "\n"
         << "General Options:\n"
         << "  -h, --help             Show this message\n"
         << "  -v, --verbose <0-4>    Verbosity (default: 1)\n"
         << "  --validate             Validate mesh before/after processing\n"
         << "  --ascii-ply            Save PLY in ASCII (default: binary)\n"
         << "  --temp-dir <path>      Directory for debug PLY dumps\n"
         << "\n"
         << "CLI Arguments:\n"
         << "  input                  Input mesh file (.obj, .ply, .off)\n"
         << "  output                 Output mesh file (.obj, .ply, .off)\n"
         << "\n"
         << "CLI Options:\n"
         << "  --continuity <0|1|2>   Fairing continuity (default: 1)\n"
         << "  --max-boundary <n>     Max hole boundary vertices (default: 1000)\n"
         << "  --max-diameter <r>     Max hole diameter ratio (default: 0.1)\n"
         << "  --no-2d-cdt            Disable 2D constrained Delaunay\n"
         << "  --no-3d-delaunay       Disable 3D Delaunay fallback\n"
         << "  --skip-cubic           Skip cubic search (faster, less robust)\n"
         << "  --no-refine            Disable mesh refinement\n"
         << "  --holes_only           Output only reconstructed (new) polygons\n"
         << "  --per-hole-info        Print per-hole timing/details in stats output\n"
         << "  --min-edges <n>        Minimum boundary edges per partition before parallelizing (default: 100)\n"
         << "  --threads <n>          Worker threads (default: hw_cores/2, 0 = auto)\n"
         << "  --queue-size <n>       Pipeline queue size (legacy mode only, default: 10)\n"
         << "  --no-partition         Use legacy pipeline instead of partitioned mode\n"
         << "  --cgal-loader          Force CGAL OBJ loader (default: RapidOBJ if available)\n"
         << "\n"
         << "Preprocessing (enabled by default):\n"
         << "  --no-preprocess        Disable all preprocessing steps\n"
         << "  --no-remove-duplicates Skip duplicate vertex removal\n"
         << "  --no-remove-non-manifold Skip non-manifold vertex removal\n"
         << "  --no-remove-3facefan   Skip 3-face fan collapsing\n"
         << "  --remove-long-edges <r> Remove polygons with edges longer than r * mesh bbox diagonal (disabled by default)\n"
         << "  --no-remove-isolated   Skip isolated vertex removal\n"
         << "  --no-remove-small      Keep all components (no pruning)\n"
         << "  --non-manifold-passes <n> Number of non-manifold passes (default: 2)\n"
         << "\n"
         << "Engine Options:\n"
         << "  --engine               Start IPC engine (pipe mode default)\n"
         << "  --socket <port>        Run engine in TCP socket mode on <port>\n"
         << "  -v, --verbose <0-4>    Engine verbosity (same scale as CLI)\n"
         << "  --temp-dir <path>      Directory for engine debug/trace output\n"
         << "\n"
         << "Examples:\n"
         << "  " << program_name << " model.obj repaired.obj\n"
         << "  " << program_name << " mesh.ply output.ply --continuity 2 --max-boundary 500\n"
         << "  " << program_name << " input.obj output.obj --no-preprocess\n"
         << "  " << program_name << " --engine --socket 9876 -v 3\n"
         << "\n";

    logInfo(LogCategory::Cli, help.str());
}

}  // namespace MeshRepair
