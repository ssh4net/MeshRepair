#include "mesh_loader.h"
#include "hole_detector.h"
#include "hole_filler.h"
#include "mesh_validator.h"
#include "progress_reporter.h"
#include "mesh_preprocessor.h"
#include "thread_manager.h"
#include "pipeline_processor.h"
#include "parallel_hole_filler.h"
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
              << "                         0 = C0, 1 = C1, 2 = C2\n"
              << "  --max-boundary <n>     Max hole boundary vertices (default: 1000)\n"
              << "  --max-diameter <r>     Max hole diameter ratio (default: 0.1)\n"
              << "  --no-2d-cdt            Disable 2D constrained Delaunay\n"
              << "  --no-3d-delaunay       Disable 3D Delaunay fallback\n"
              << "  --skip-cubic           Skip cubic search (faster but less robust)\n"
              << "  --no-refine            Disable mesh refinement\n"
              << "  -v, --verbose          Verbose output (shows all hole details)\n"
              << "  --quiet                Minimal output\n"
              << "  --stats                Show detailed statistics\n"
              << "  --validate             Validate mesh before/after\n"
              << "  --ascii-ply            Save PLY in ASCII format (default: binary)\n"
              << "\n"
              << "Preprocessing:\n"
              << "  --preprocess           Enable all preprocessing steps\n"
              << "  --no-remove-duplicates Disable duplicate vertex removal\n"
              << "  --no-remove-non-manifold Disable non-manifold vertex removal\n"
              << "  --no-remove-isolated   Disable isolated vertex removal\n"
              << "  --no-remove-small      Disable small component removal\n"
              << "  --non-manifold-passes <n> Number of non-manifold removal passes (default: 2)\n"
              << "  --debug                Dump intermediate meshes as binary PLY\n"
              << "\n"
              << "Threading:\n"
              << "  --threads <n>          Number of worker threads (default: hw_cores/2, 0 = auto)\n"
              << "  --queue-size <n>       Pipeline queue size in holes (default: 10, legacy mode only)\n"
              << "  --no-partition         Disable partitioned parallel filling (use legacy mode)\n"
              << "\n"
              << "Loader:\n"
              << "  --cgal-loader          Force CGAL OBJ loader (default: RapidOBJ if available)\n"
              << "\n"
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

    // Preprocessing options
    bool enable_preprocessing = false;
    bool preprocess_remove_duplicates = true;
    bool preprocess_remove_non_manifold = true;
    bool preprocess_remove_isolated = true;
    bool preprocess_keep_largest_component = true;
    size_t non_manifold_passes = 2;
    bool debug = false;

    // Threading options
    size_t num_threads = 0;      // 0 = auto (hw_cores / 2)
    size_t queue_size = 10;      // Pipeline queue size
    bool use_partitioned = true; // Use partitioned parallel filling (default)

    // Loader options
    bool force_cgal_loader = false; // Force CGAL OBJ loader instead of RapidOBJ

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
                    std::cout << "Error: Continuity must be 0, 1, or 2\n";
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
            else if (arg == "--verbose" || arg == "-v") {
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
            else if (arg == "--preprocess") {
                enable_preprocessing = true;
            }
            else if (arg == "--no-remove-duplicates") {
                preprocess_remove_duplicates = false;
            }
            else if (arg == "--no-remove-non-manifold") {
                preprocess_remove_non_manifold = false;
            }
            else if (arg == "--no-remove-isolated") {
                preprocess_remove_isolated = false;
            }
            else if (arg == "--no-remove-small") {
                preprocess_keep_largest_component = false;
            }
            else if (arg == "--non-manifold-passes" && i + 1 < argc) {
                non_manifold_passes = std::stoul(argv[++i]);
                if (non_manifold_passes == 0) {
                    std::cout << "Error: Non-manifold passes must be at least 1\n";
                    return false;
                }
            }
            else if (arg == "--debug") {
                debug = true;
            }
            else if (arg == "--threads" && i + 1 < argc) {
                num_threads = std::stoul(argv[++i]);
            }
            else if (arg == "--queue-size" && i + 1 < argc) {
                queue_size = std::stoul(argv[++i]);
                if (queue_size == 0) {
                    std::cout << "Error: Queue size must be at least 1\n";
                    return false;
                }
            }
            else if (arg == "--no-partition") {
                use_partitioned = false;
            }
            else if (arg == "--cgal-loader") {
                force_cgal_loader = true;
            }
            else {
                std::cout << "Unknown option: " << arg << "\n";
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
    if (!args.quiet && args.filling_options.verbose) {
        std::cout << "Loading mesh from: " << args.input_file << "\n";
    }

    auto mesh_opt = MeshLoader::load(args.input_file, MeshLoader::Format::AUTO, args.force_cgal_loader);
    if (!mesh_opt) {
        std::cout << "Error: " << MeshLoader::get_last_error() << "\n";
        return 1;
    }

    Mesh mesh = std::move(mesh_opt.value());

    if (!args.quiet && args.filling_options.verbose) {
        std::cout << "Loaded mesh from: " << args.input_file << "\n";
        std::cout << "  Vertices: " << mesh.number_of_vertices() << "\n";
        std::cout << "  Faces: " << mesh.number_of_faces() << "\n";
        std::cout << "  Edges: " << mesh.number_of_edges() << "\n\n";
    }

    // Configure threading
    ThreadingConfig thread_config;
    thread_config.num_threads = args.num_threads;
    thread_config.queue_size = args.queue_size;
    thread_config.verbose = args.filling_options.verbose;

    ThreadManager thread_manager(thread_config);

    // Validate input mesh if requested
    if (args.validate) {
        if (!args.quiet) {
            std::cout << "\n=== Input Mesh Validation ===\n";
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            std::cout << "Warning: Input mesh failed validity checks\n";
        }

        if (!MeshValidator::is_triangle_mesh(mesh)) {
            std::cout << "Error: Mesh must be a triangle mesh\n";
            return 1;
        }
    }

    // Step 1.5: Preprocess mesh if requested
    if (args.enable_preprocessing) {
        // Debug: Save original loaded mesh before any preprocessing
        if (args.debug) {
            std::string debug_file = "debug_00_original_loaded.ply";
            if (CGAL::IO::write_PLY(debug_file, mesh, CGAL::parameters::use_binary_mode(true))) {
                if (!args.quiet) {
                    std::cout << "  [DEBUG] Saved original loaded mesh: " << debug_file << "\n";
                    std::cout << "  [DEBUG]   Vertices: " << mesh.number_of_vertices() << "\n";
                    std::cout << "  [DEBUG]   Faces: " << mesh.number_of_faces() << "\n\n";
                }
            }
        }

        PreprocessingOptions prep_opts;
        prep_opts.remove_duplicates = args.preprocess_remove_duplicates;
        prep_opts.remove_non_manifold = args.preprocess_remove_non_manifold;
        prep_opts.remove_isolated = args.preprocess_remove_isolated;
        prep_opts.keep_largest_component = args.preprocess_keep_largest_component;
        prep_opts.non_manifold_passes = args.non_manifold_passes;
        prep_opts.verbose = args.filling_options.verbose;
        prep_opts.debug = args.debug;

        MeshPreprocessor preprocessor(mesh, prep_opts);
        auto prep_stats = preprocessor.preprocess();

        if (args.show_stats) {
            preprocessor.print_report();
        }
    }

    // Step 2 & 3: Detect and fill holes (with threading)
    MeshStatistics stats;

    if (args.use_partitioned) {
        // Use partitioned parallel filling (default mode)
        if (!args.quiet && args.filling_options.verbose) {
            std::cout << "\n=== Partitioned Parallel Filling (Default) ===\n";
        }

        ParallelHoleFillerPipeline partitioned_processor(
            mesh, thread_manager, args.filling_options);

        stats = partitioned_processor.process_partitioned(args.filling_options.verbose, args.debug);
    }
    else {
        // Use legacy pipeline processing
        if (!args.quiet && args.filling_options.verbose) {
            std::cout << "\n=== Legacy Pipeline Mode ===\n";
        }

        PipelineProcessor processor(mesh, thread_manager, args.filling_options);

        if (thread_manager.get_total_threads() > 1) {
            // Use pipeline processing for parallel execution
            stats = processor.process_pipeline(args.filling_options.verbose);
        } else {
            // Single-threaded batch processing
            stats = processor.process_batch(args.filling_options.verbose);
        }
    }

    // Check if no holes were found
    if (stats.num_holes_detected == 0) {
        if (!args.quiet) {
            std::cout << "No holes found. Mesh is already closed.\n";
        }

        // Save anyway in case of format conversion
        if (!MeshLoader::save(mesh, args.output_file)) {
            std::cout << "Error: " << MeshLoader::get_last_error() << "\n";
            return 1;
        }

        return 0;
    }

    // Step 4: Validate output mesh if requested
    if (args.validate) {
        if (!args.quiet) {
            std::cout << "\n=== Output Mesh Validation ===\n";
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            std::cout << "Warning: Output mesh failed validity checks\n";
        }
    }

    // Step 5: Save result
    if (!args.quiet && args.filling_options.verbose) {
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
        std::cout << "Error: " << MeshLoader::get_last_error() << "\n";
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
