#include "mesh_loader.h"
#include "hole_detector.h"
#include "hole_filler.h"
#include "mesh_validator.h"
#include "progress_reporter.h"
#include "config.h"

#include <iostream>
#include <string>
#include <cstring>

using namespace MeshRepair;

void print_usage(const char* program_name) {
    std::cout << "\n"
              << "MeshHoleFiller v" << Config::VERSION << "\n"
              << "Mesh hole filling tool using CGAL (Liepa 2003 with Laplacian fairing)\n\n"
              << "Usage: " << program_name << " <input> <output> [options]\n\n"
              << "Arguments:\n"
              << "  input                  Input mesh file (.obj, .ply, .off)\n"
              << "  output                 Output mesh file (.obj, .ply, .off)\n\n"
              << "Options:\n"
              << "  --continuity <0|1|2>   Fairing continuity (default: 1)\n"
              << "                         0 = C⁰, 1 = C¹, 2 = C²\n"
              << "  --max-boundary <n>     Max hole boundary vertices (default: 1000)\n"
              << "  --max-diameter <r>     Max hole diameter ratio (default: 0.1)\n"
              << "  --no-2d-cdt            Disable 2D constrained Delaunay\n"
              << "  --no-3d-delaunay       Disable 3D Delaunay fallback\n"
              << "  --skip-cubic           Skip cubic search (faster but less robust)\n"
              << "  --no-refine            Disable mesh refinement\n"
              << "  --verbose              Verbose output\n"
              << "  --quiet                Minimal output\n"
              << "  --stats                Show detailed statistics\n"
              << "  --validate             Validate mesh before/after\n"
              << "  --ascii-ply            Save PLY in ASCII format (default: binary)\n"
              << "  --help                 Show this help message\n\n"
              << "Examples:\n"
              << "  " << program_name << " input.obj output.obj\n"
              << "  " << program_name << " mesh.ply repaired.ply --verbose --stats\n"
              << "  " << program_name << " model.obj fixed.obj --continuity 2 --max-boundary 500\n\n";
}

struct CommandLineArgs {
    std::string input_file;
    std::string output_file;
    FillingOptions filling_options;
    bool show_stats = false;
    bool validate = false;
    bool quiet = false;
    bool ascii_ply = false;  // Use ASCII PLY instead of binary

    bool parse(int argc, char** argv) {
        if (argc < 3) {
            return false;
        }

        input_file = argv[1];
        output_file = argv[2];

        // Check for help
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
                return false;
            }
        }

        // Parse options
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--continuity" && i + 1 < argc) {
                filling_options.fairing_continuity = std::stoi(argv[++i]);
                if (filling_options.fairing_continuity > 2) {
                    std::cerr << "Error: Continuity must be 0, 1, or 2\n";
                    return false;
                }
            }
            else if (arg == "--max-boundary" && i + 1 < argc) {
                filling_options.max_hole_boundary_vertices = std::stoul(argv[++i]);
            }
            else if (arg == "--max-diameter" && i + 1 < argc) {
                filling_options.max_hole_diameter_ratio = std::stod(argv[++i]);
            }
            else if (arg == "--no-2d-cdt") {
                filling_options.use_2d_cdt = false;
            }
            else if (arg == "--no-3d-delaunay") {
                filling_options.use_3d_delaunay = false;
            }
            else if (arg == "--skip-cubic") {
                filling_options.skip_cubic_search = true;
            }
            else if (arg == "--no-refine") {
                filling_options.refine = false;
            }
            else if (arg == "--verbose") {
                filling_options.verbose = true;
            }
            else if (arg == "--quiet") {
                quiet = true;
                filling_options.show_progress = false;
                filling_options.verbose = false;
            }
            else if (arg == "--stats") {
                show_stats = true;
            }
            else if (arg == "--validate") {
                validate = true;
            }
            else if (arg == "--ascii-ply") {
                ascii_ply = true;
            }
            else {
                std::cerr << "Unknown option: " << arg << "\n";
                return false;
            }
        }

        return true;
    }
};

int main(int argc, char** argv) {
    // Parse command-line arguments
    CommandLineArgs args;
    if (!args.parse(argc, argv)) {
        print_usage(argv[0]);
        return (argc < 3) ? 1 : 0;  // Return 0 for --help, 1 for parse error
    }

    if (!args.quiet) {
        std::cout << "=== MeshHoleFiller v" << Config::VERSION << " ===\n\n";
    }

    // Step 1: Load mesh
    if (!args.quiet) {
        std::cout << "Loading mesh from: " << args.input_file << "\n";
    }

    auto mesh_opt = MeshLoader::load(args.input_file);
    if (!mesh_opt) {
        std::cerr << "Error: " << MeshLoader::get_last_error() << "\n";
        return 1;
    }

    Mesh mesh = std::move(mesh_opt.value());

    // Validate input mesh if requested
    if (args.validate) {
        if (!args.quiet) {
            std::cout << "\n=== Input Mesh Validation ===\n";
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            std::cerr << "Warning: Input mesh failed validity checks\n";
        }

        if (!MeshValidator::is_triangle_mesh(mesh)) {
            std::cerr << "Error: Mesh must be a triangle mesh\n";
            return 1;
        }
    }

    // Step 2: Detect holes
    if (!args.quiet) {
        std::cout << "\nDetecting holes...\n";
    }

    HoleDetector detector(mesh);
    auto holes = detector.detect_all_holes();

    if (holes.empty()) {
        if (!args.quiet) {
            std::cout << "No holes found. Mesh is already closed.\n";
            std::cout << "Output file will be identical to input.\n";
        }

        // Save anyway in case of format conversion
        if (!MeshLoader::save(mesh, args.output_file)) {
            std::cerr << "Error: " << MeshLoader::get_last_error() << "\n";
            return 1;
        }

        return 0;
    }

    // Step 3: Fill holes
    HoleFiller filler(mesh, args.filling_options);
    MeshStatistics stats = filler.fill_all_holes(holes);

    // Step 4: Validate output mesh if requested
    if (args.validate) {
        if (!args.quiet) {
            std::cout << "\n=== Output Mesh Validation ===\n";
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            std::cerr << "Warning: Output mesh failed validity checks\n";
        }
    }

    // Step 5: Save result
    if (!args.quiet) {
        std::cout << "\nSaving result to: " << args.output_file;
        // Check if output is PLY format (C++17 compatible)
        size_t len = args.output_file.length();
        if (len >= 4) {
            std::string ext = args.output_file.substr(len - 4);
            if (ext == ".ply" || ext == ".PLY") {
                std::cout << " (" << (args.ascii_ply ? "ASCII" : "binary") << " PLY)";
            }
        }
        std::cout << "\n";
    }

    // Binary PLY by default, ASCII if requested
    bool use_binary_ply = !args.ascii_ply;
    if (!MeshLoader::save(mesh, args.output_file, MeshLoader::Format::AUTO, use_binary_ply)) {
        std::cerr << "Error: " << MeshLoader::get_last_error() << "\n";
        return 1;
    }

    // Step 6: Print detailed statistics if requested
    if (args.show_stats) {
        std::cout << "\n=== Detailed Statistics ===\n";
        std::cout << "Original mesh:\n";
        std::cout << "  Vertices: " << stats.original_vertices << "\n";
        std::cout << "  Faces: " << stats.original_faces << "\n";

        std::cout << "\nFinal mesh:\n";
        std::cout << "  Vertices: " << stats.final_vertices
                  << " (+" << stats.total_vertices_added() << ")\n";
        std::cout << "  Faces: " << stats.final_faces
                  << " (+" << stats.total_faces_added() << ")\n";

        std::cout << "\nHole processing:\n";
        std::cout << "  Detected: " << stats.num_holes_detected << "\n";
        std::cout << "  Filled: " << stats.num_holes_filled << "\n";
        std::cout << "  Failed: " << stats.num_holes_failed << "\n";
        std::cout << "  Skipped: " << stats.num_holes_skipped << "\n";

        std::cout << "\nTiming:\n";
        std::cout << "  Total: " << stats.total_time_ms << " ms\n";

        if (args.filling_options.verbose && !stats.hole_details.empty()) {
            std::cout << "\nPer-hole details:\n";
            for (size_t i = 0; i < stats.hole_details.size(); ++i) {
                const auto& h = stats.hole_details[i];
                std::cout << "  Hole " << (i + 1) << ": ";
                if (h.filled_successfully) {
                    std::cout << "OK - " << h.num_faces_added << " faces, "
                              << h.num_vertices_added << " vertices, "
                              << h.fill_time_ms << " ms";
                    if (!h.fairing_succeeded) {
                        std::cout << " [fairing failed]";
                    }
                } else {
                    std::cout << "FAILED";
                }
                std::cout << "\n";
            }
        }
        std::cout << "===========================\n";
    }

    if (!args.quiet) {
        std::cout << "\nDone! Successfully processed mesh.\n";
    }

    return 0;
}
