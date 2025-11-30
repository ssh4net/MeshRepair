#include "mesh_loader.h"
#include "hole_ops.h"
#include "mesh_validator.h"
#include "progress_reporter.h"
#include "mesh_preprocessor.h"
#include "worker_pool.h"
#include "pipeline_ops.h"
#include "config.h"
#include "debug_path.h"
#include "help_printer.h"

#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/IO/PLY.h>
#include <iostream>
#include <string>
#include <cstring>
#include <chrono>

using namespace MeshRepair;

namespace PMP = CGAL::Polygon_mesh_processing;

struct CommandLineArgs {
    std::string input_file;
    std::string output_file;
    FillingOptions filling_options;
    int verbosity  = 1;  // 0=quiet, 1=info(stats), 2=verbose, 3=debug, 4=trace(PLY dumps)
    bool validate  = false;
    bool ascii_ply = false;  // Use ASCII PLY instead of binary

    // Preprocessing options
    bool enable_preprocessing              = true;
    bool preprocess_remove_duplicates      = true;
    bool preprocess_remove_non_manifold    = true;
    bool preprocess_remove_3_face_fans     = true;
    bool preprocess_remove_isolated        = true;
    bool preprocess_keep_largest_component = true;
    size_t non_manifold_passes             = 10;

    // Threading options
    size_t num_threads   = 0;     // 0 = auto (hw_cores / 2)
    size_t queue_size    = 10;    // Pipeline queue size
    bool use_partitioned = true;  // Use partitioned parallel filling (default)

    // Loader options
    bool force_cgal_loader = false;  // Force CGAL OBJ loader instead of RapidOBJ

    // Debug/temp options
    std::string temp_dir;

    bool parse(int argc, char** argv)
    {
        if (argc < 3) {
            return false;
        }

        input_file  = argv[1];
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
            } else if (arg == "--max-boundary" && i + 1 < argc) {
                filling_options.max_hole_boundary_vertices = std::stoul(argv[++i]);
            } else if (arg == "--max-diameter" && i + 1 < argc) {
                filling_options.max_hole_diameter_ratio = std::stod(argv[++i]);
            } else if (arg == "--no-2d-cdt") {
                filling_options.use_2d_cdt = false;
            } else if (arg == "--no-3d-delaunay") {
                filling_options.use_3d_delaunay = false;
            } else if (arg == "--skip-cubic") {
                filling_options.skip_cubic_search = true;
            } else if (arg == "--no-refine") {
                filling_options.refine = false;
            } else if (arg == "--holes_only") {
                filling_options.holes_only = true;
            } else if (arg == "--verbose" || arg == "-v") {
                // Check if next arg is a number
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    verbosity = std::stoi(argv[++i]);
                    if (verbosity < 0 || verbosity > 4) {
                        std::cout << "Error: Verbosity level must be 0-4\n";
                        return false;
                    }
                } else {
                    verbosity = 2;  // Default to verbose if no level specified
                }
            } else if (arg == "--validate") {
                validate = true;
            } else if (arg == "--ascii-ply") {
                ascii_ply = true;
            } else if ((arg == "--temp-dir" || arg == "--temp") && i + 1 < argc) {
                temp_dir = argv[++i];
            } else if (arg == "--no-preprocess") {
                enable_preprocessing = false;
            } else if (arg == "--preprocess") {
                enable_preprocessing = true;  // Deprecated, preprocessing is the default
            } else if (arg == "--no-remove-duplicates") {
                preprocess_remove_duplicates = false;
            } else if (arg == "--no-remove-non-manifold") {
                preprocess_remove_non_manifold = false;
            } else if (arg == "--no-remove-3facefan") {
                preprocess_remove_3_face_fans = false;
            } else if (arg == "--no-remove-isolated") {
                preprocess_remove_isolated = false;
            } else if (arg == "--no-remove-small") {
                preprocess_keep_largest_component = false;
            } else if (arg == "--non-manifold-passes" && i + 1 < argc) {
                non_manifold_passes = std::stoul(argv[++i]);
                if (non_manifold_passes == 0) {
                    std::cout << "Error: Non-manifold passes must be at least 1\n";
                    return false;
                }
            } else if (arg == "--threads" && i + 1 < argc) {
                num_threads = std::stoul(argv[++i]);
            } else if (arg == "--queue-size" && i + 1 < argc) {
                queue_size = std::stoul(argv[++i]);
                if (queue_size == 0) {
                    std::cout << "Error: Queue size must be at least 1\n";
                    return false;
                }
            } else if (arg == "--no-partition") {
                use_partitioned = false;
            } else if (arg == "--min-edges" && i + 1 < argc) {
                filling_options.min_partition_boundary_edges = std::stoul(argv[++i]);
            } else if (arg == "--cgal-loader") {
                force_cgal_loader = true;
            } else {
                std::cout << "Unknown option: " << arg << "\n";
                return false;
            }
        }

        return true;
    }
};

int
cli_main(int argc, char** argv)
{
    // Parse command-line arguments
    CommandLineArgs args;
    if (!args.parse(argc, argv)) {
        print_help(argv[0]);
        return 1;
    }

    if (!args.temp_dir.empty()) {
        DebugPath::set_base_directory(args.temp_dir);
    }

    // Start total timing
    auto program_start_time = std::chrono::high_resolution_clock::now();

    // Set flags based on verbosity level
    // 0 = quiet, 1 = info (stats), 2 = verbose, 3 = debug, 4 = trace (PLY dumps)
    bool show_stats = (args.verbosity >= 1);
    bool verbose    = (args.verbosity >= 2);
    bool debug      = (args.verbosity >= 4);  // PLY dumps only at level 4

    args.filling_options.verbose       = verbose;
    args.filling_options.show_progress = (args.verbosity > 0);
    args.filling_options.keep_largest_component = args.preprocess_keep_largest_component;

    if (args.verbosity > 0) {
        std::cout << "=== MeshHoleFiller v" << Config::VERSION << " ===\n";
        std::cout << " Bult: " << Config::BUILD_DATE << " " << Config::BUILD_TIME << "\n\n";
    }

    // Step 1: Load mesh as polygon soup (optimized - avoids mesh construction)
    if (verbose) {
        std::cout << "Loading mesh from: " << args.input_file << "\n";
    }

    PolygonSoup soup;
    Mesh mesh;  // Will be populated during preprocessing or immediately if no preprocessing
    if (mesh_loader_load_soup(args.input_file.c_str(), MeshLoader::Format::AUTO, args.force_cgal_loader, &soup) != 0) {
        std::cout << "Error: " << mesh_loader_last_error() << "\n";
        return 1;
    }

    if (verbose) {
        std::cout << "Loaded polygon soup from: " << args.input_file << "\n";
        std::cout << "  Points: " << soup.points.size() << "\n";
        std::cout << "  Polygons: " << soup.polygons.size() << "\n";
        std::cout << "  Load time: " << std::fixed << std::setprecision(2) << soup.load_time_ms << " ms\n\n";
    }

    // Configure threading
    ThreadingConfig thread_config;
    thread_config.num_threads = args.num_threads;
    thread_config.queue_size  = args.queue_size;
    thread_config.verbose     = verbose;

    ThreadManager thread_manager;
    thread_manager_init(thread_manager, thread_config);

    // Step 1.5: Preprocess soup and convert to mesh (OPTIMIZED - single conversion!)
    PreprocessingStats prep_stats;

    if (args.enable_preprocessing) {
        // Debug: Save original loaded soup before any preprocessing
        if (debug) {
            std::string debug_file = DebugPath::step_file("original_loaded");
            // Convert soup to temporary mesh for debug output
            Mesh debug_mesh;
            PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, debug_mesh);
            if (CGAL::IO::write_PLY(debug_file, debug_mesh, CGAL::parameters::use_binary_mode(true))) {
                if (args.verbosity > 0) {
                    std::cout << "  [DEBUG] Saved original loaded soup: " << debug_file << "\n";
                    std::cout << "  [DEBUG]   Points: " << soup.points.size() << "\n";
                    std::cout << "  [DEBUG]   Polygons: " << soup.polygons.size() << "\n\n";
                }
            }
        }

        PreprocessingOptions prep_opts;
        prep_opts.remove_duplicates      = args.preprocess_remove_duplicates;
        prep_opts.remove_non_manifold    = args.preprocess_remove_non_manifold;
        prep_opts.remove_3_face_fans     = args.preprocess_remove_3_face_fans;
        prep_opts.remove_isolated        = args.preprocess_remove_isolated;
        prep_opts.keep_largest_component = args.preprocess_keep_largest_component;
        prep_opts.non_manifold_passes    = args.non_manifold_passes;
        prep_opts.verbose                = verbose;
        prep_opts.debug                  = debug;

        // Preprocess soup directly (no mesh->soup extraction!)
        prep_stats = MeshPreprocessor::preprocess_soup(soup, mesh, prep_opts);

        if (show_stats && verbose) {
            std::cout << "\n=== Preprocessing Report ===\n";
            std::cout << "Duplicate vertices merged: " << prep_stats.duplicates_merged << "\n";
            std::cout << "Non-manifold polygons removed: " << prep_stats.non_manifold_vertices_removed << "\n";
            std::cout << "3-face fans collapsed: " << prep_stats.face_fans_collapsed << "\n";
            std::cout << "Isolated vertices removed: " << prep_stats.isolated_vertices_removed << "\n";
            std::cout << "Connected components found: " << prep_stats.connected_components_found << "\n";
            std::cout << "Small components removed: " << prep_stats.small_components_removed << "\n";
            std::cout << "Timing breakdown:\n";
            std::cout << "  Soup cleanup: " << std::fixed << std::setprecision(2) << prep_stats.soup_cleanup_time_ms
                      << " ms\n";
            std::cout << "  Soup->Mesh conversion: " << prep_stats.soup_to_mesh_time_ms << " ms\n";
            std::cout << "  Mesh cleanup: " << prep_stats.mesh_cleanup_time_ms << " ms\n";
            std::cout << "  Total: " << prep_stats.total_time_ms << " ms\n";
            std::cout << "============================\n\n";
        }
    } else {
        // No preprocessing - just convert soup to mesh directly
        auto convert_start = std::chrono::high_resolution_clock::now();
        PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, mesh);
        auto convert_end = std::chrono::high_resolution_clock::now();

        double convert_time_ms = std::chrono::duration<double, std::milli>(convert_end - convert_start).count();

        if (args.verbosity > 0 && args.filling_options.verbose) {
            std::cout << "Converted soup to mesh (no preprocessing)\n";
            std::cout << "  Vertices: " << mesh.number_of_vertices() << "\n";
            std::cout << "  Faces: " << mesh.number_of_faces() << "\n";
            std::cout << "  Conversion time: " << std::fixed << std::setprecision(2) << convert_time_ms << " ms\n\n";
        }
    }

    // Validate mesh if requested (after conversion to mesh)
    if (args.validate) {
        if (args.verbosity > 0) {
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

    // Step 2 & 3: Detect and fill holes (with threading)
    MeshStatistics stats;

    if (args.filling_options.holes_only && !args.use_partitioned) {
        if (args.verbosity > 0) {
            std::cout << "Warning: --holes_only is supported only in partitioned mode; flag will be ignored.\n";
        }
        args.filling_options.holes_only = false;
    }

    if (args.use_partitioned) {
        // Use partitioned parallel filling (default mode)
        if (args.verbosity > 0 && args.filling_options.verbose) {
            std::cout << "\n=== Partitioned Parallel Filling (Default) ===\n";
        }

        ParallelPipelineCtx ctx;
        ctx.mesh       = &mesh;
        ctx.thread_mgr = &thread_manager;
        ctx.options    = args.filling_options;

        stats = parallel_fill_partitioned(&ctx, args.filling_options.verbose, debug);
    } else {
        // Use legacy pipeline processing
        if (args.verbosity > 0 && args.filling_options.verbose) {
            std::cout << "\n=== Legacy Pipeline Mode ===\n";
        }

        PipelineContext ctx;
        ctx.mesh       = &mesh;
        ctx.thread_mgr = &thread_manager;
        ctx.options    = args.filling_options;

        if (thread_manager.config.num_threads > 1) {
            stats = pipeline_process_pipeline(&ctx, args.filling_options.verbose);
        } else {
            stats = pipeline_process_batch(&ctx, args.filling_options.verbose);
        }
    }

    // Check if no holes were found
    if (stats.num_holes_detected == 0) {
        if (args.verbosity > 0) {
            std::cout << "No holes found. Mesh is already closed.\n";
        }

        // Save anyway in case of format conversion
        if (mesh_loader_save(mesh, args.output_file.c_str(), MeshLoader::Format::AUTO, true) != 0) {
            std::cout << "Error: " << mesh_loader_last_error() << "\n";
            return 1;
        }

        return 0;
    }

    // Step 4: Validate output mesh if requested
    if (args.validate) {
        if (args.verbosity > 0) {
            std::cout << "\n=== Output Mesh Validation ===\n";
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            std::cout << "Warning: Output mesh failed validity checks\n";
        }
    }

    // Step 5: Save result
    if (args.verbosity > 0 && args.filling_options.verbose) {
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
    auto save_start_time = std::chrono::high_resolution_clock::now();
    bool use_binary_ply  = !args.ascii_ply;
    if (mesh_loader_save(mesh, args.output_file.c_str(), MeshLoader::Format::AUTO, use_binary_ply) != 0) {
        std::cout << "Error: " << mesh_loader_last_error() << "\n";
        return 1;
    }
    auto save_end_time  = std::chrono::high_resolution_clock::now();
    double save_time_ms = std::chrono::duration<double, std::milli>(save_end_time - save_start_time).count();

    // Step 6: Print detailed statistics if requested
    if (show_stats) {
        std::cout << "\n=== Detailed Statistics ===\n";
        std::cout << "Original mesh:\n";
        std::cout << "  Vertices: " << stats.original_vertices << "\n";
        std::cout << "  Faces: " << stats.original_faces << "\n";

        std::cout << "\nFinal mesh:\n";
        if (args.filling_options.holes_only) {
            std::cout << "  [holes_only] Output contains only reconstructed faces (base mesh faces omitted)\n";
        }
        std::cout << "  Vertices: " << stats.final_vertices << " (+" << mesh_stats_total_vertices_added(stats) << ")\n";
        std::cout << "  Faces: " << stats.final_faces << " (+" << mesh_stats_total_faces_added(stats) << ")\n";

        std::cout << "\nHole processing:\n";
        std::cout << "  Detected: " << stats.num_holes_detected << "\n";
        std::cout << "  Filled: " << stats.num_holes_filled << "\n";
        std::cout << "  Failed: " << stats.num_holes_failed << "\n";
        std::cout << "  Skipped: " << stats.num_holes_skipped << "\n";

        std::cout << "\nTiming breakdown:\n";
        std::cout << "  File load: " << std::fixed << std::setprecision(2) << soup.load_time_ms << " ms\n";
        if (args.enable_preprocessing) {
            std::cout << "  Preprocessing:\n";
            std::cout << "    Soup cleanup: " << prep_stats.soup_cleanup_time_ms << " ms\n";
            std::cout << "    Soup->Mesh conversion: " << prep_stats.soup_to_mesh_time_ms << " ms\n";
            std::cout << "    Mesh cleanup: " << prep_stats.mesh_cleanup_time_ms << " ms\n";
            std::cout << "    Subtotal: " << prep_stats.total_time_ms << " ms\n";
        }
        std::cout << "  Hole filling: " << stats.total_time_ms << " ms\n";
        std::cout << "  File save: " << save_time_ms << " ms\n";

        auto program_end_time = std::chrono::high_resolution_clock::now();
        double total_program_time_ms
            = std::chrono::duration<double, std::milli>(program_end_time - program_start_time).count();
        std::cout << "  Total program time: " << total_program_time_ms << " ms\n";

        if (args.filling_options.verbose && !stats.hole_details.empty()) {
            std::cout << "\nPer-hole details:\n";
            for (size_t i = 0; i < stats.hole_details.size(); ++i) {
                const auto& h = stats.hole_details[i];
                std::cout << "  Hole " << (i + 1) << ": ";
                if (h.filled_successfully) {
                    std::cout << "OK - " << h.num_faces_added << " faces, " << h.num_vertices_added << " vertices, "
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

    if (args.verbosity > 0) {
        std::cout << "\nDone! Successfully processed mesh.\n";
    }

    return 0;
}
