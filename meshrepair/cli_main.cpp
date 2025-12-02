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
#include "logger.h"

#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/IO/PLY.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstring>
#include <chrono>

using namespace MeshRepair;

namespace PMP = CGAL::Polygon_mesh_processing;

struct CommandLineArgs {
    std::string input_file;
    std::string output_file;
    FillingOptions filling_options;
    int verbosity      = 1;  // 0=quiet, 1=info(stats), 2=verbose, 3=debug, 4=trace(PLY dumps)
    bool validate      = false;
    bool ascii_ply     = false;  // Use ASCII PLY instead of binary
    bool per_hole_info = false;  // Print per-hole timing/details in stats output

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
                    logError(LogCategory::Cli, "Error: Continuity must be 0, 1, or 2");
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
            } else if (arg == "--per-hole-info") {
                per_hole_info = true;
            } else if (arg == "--verbose" || arg == "-v") {
                // Check if next arg is a number
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    verbosity = std::stoi(argv[++i]);
                    if (verbosity < 0 || verbosity > 4) {
                        logError(LogCategory::Cli, "Error: Verbosity level must be 0-4");
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
                    logError(LogCategory::Cli, "Error: Non-manifold passes must be at least 1");
                    return false;
                }
            } else if (arg == "--threads" && i + 1 < argc) {
                num_threads = std::stoul(argv[++i]);
            } else if (arg == "--queue-size" && i + 1 < argc) {
                queue_size = std::stoul(argv[++i]);
                if (queue_size == 0) {
                    logError(LogCategory::Cli, "Error: Queue size must be at least 1");
                    return false;
                }
            } else if (arg == "--no-partition") {
                use_partitioned = false;
            } else if (arg == "--min-edges" && i + 1 < argc) {
                filling_options.min_partition_boundary_edges = std::stoul(argv[++i]);
            } else if (arg == "--cgal-loader") {
                force_cgal_loader = true;
            } else {
                logError(LogCategory::Cli, "Unknown option: " + arg);
                return false;
            }
        }

        return true;
    }
};

int
cli_main(int argc, char** argv)
{
    LoggerConfig log_cfg;
    log_cfg.useStderr = false;
    initLogger(log_cfg);

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

    log_cfg.minLevel = logLevelFromVerbosity(args.verbosity);
    setLogLevel(log_cfg.minLevel);

    args.filling_options.verbose                = verbose;
    args.filling_options.show_progress          = (args.verbosity > 0);
    args.filling_options.keep_largest_component = args.preprocess_keep_largest_component;

    if (args.verbosity > 0) {
        std::ostringstream banner;
        banner << "=== MeshHoleFiller v" << Config::VERSION << " ===\n";
        banner << MESHREPAIR_BUILD_CONFIG << " Build: " << Config::BUILD_DATE << " " << Config::BUILD_TIME;
        logInfo(LogCategory::Empty, banner.str());
    }

    // Step 1: Load mesh as polygon soup (optimized - avoids mesh construction)
    if (verbose) {
        logInfo(LogCategory::Cli, "Loading mesh from: " + args.input_file);
    }

    PolygonSoup soup;
    Mesh mesh;  // Will be populated during preprocessing or immediately if no preprocessing
    if (mesh_loader_load_soup(args.input_file.c_str(), MeshLoader::Format::AUTO, args.force_cgal_loader, &soup) != 0) {
        logError(LogCategory::Cli, std::string("Error: ") + mesh_loader_last_error());
        return 1;
    }

    if (verbose) {
        std::ostringstream load_report;
        load_report << "Loaded polygon soup from: " << args.input_file << "\n"
                    << "  Points: " << soup.points.size() << "\n"
                    << "  Polygons: " << soup.polygons.size() << "\n"
                    << "  Load time: " << std::fixed << std::setprecision(2) << soup.load_time_ms << " ms";
        logInfo(LogCategory::Cli, load_report.str());
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
                    std::ostringstream debug_log;
                    debug_log << "  [DEBUG] Saved original loaded soup: " << debug_file << "\n"
                              << "  [DEBUG]   Points: " << soup.points.size() << "\n"
                              << "  [DEBUG]   Polygons: " << soup.polygons.size();
                    logDebug(LogCategory::Cli, debug_log.str());
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
            std::ostringstream prep_report;
            prep_report << "=== Preprocessing Report ===\n"
                        << "Duplicate vertices merged: " << prep_stats.duplicates_merged << "\n"
                        << "Non-manifold polygons removed: " << prep_stats.non_manifold_vertices_removed << "\n"
                        << "3-face fans collapsed: " << prep_stats.face_fans_collapsed << "\n"
                        << "Isolated vertices removed: " << prep_stats.isolated_vertices_removed << "\n"
                        << "Connected components found: " << prep_stats.connected_components_found << "\n"
                        << "Small components removed: " << prep_stats.small_components_removed << "\n"
                        << "Timing breakdown:\n"
                        << "  Soup cleanup: " << std::fixed << std::setprecision(2) << prep_stats.soup_cleanup_time_ms
                        << " ms\n"
                        << "  Soup->Mesh conversion: " << prep_stats.soup_to_mesh_time_ms << " ms\n"
                        << "  Mesh cleanup: " << prep_stats.mesh_cleanup_time_ms << " ms\n"
                        << "  Total: " << prep_stats.total_time_ms << " ms\n"
                        << "============================\n";
            logInfo(LogCategory::Cli, prep_report.str());
        }
    } else {
        // No preprocessing - just convert soup to mesh directly
        auto convert_start = std::chrono::high_resolution_clock::now();
        PMP::polygon_soup_to_polygon_mesh(soup.points, soup.polygons, mesh);
        auto convert_end = std::chrono::high_resolution_clock::now();

        double convert_time_ms = std::chrono::duration<double, std::milli>(convert_end - convert_start).count();

        if (args.verbosity > 0 && args.filling_options.verbose) {
            std::ostringstream convert_report;
            convert_report << "Converted soup to mesh (no preprocessing)\n"
                           << "  Vertices: " << mesh.number_of_vertices() << "\n"
                           << "  Faces: " << mesh.number_of_faces() << "\n"
                           << "  Conversion time: " << std::fixed << std::setprecision(2) << convert_time_ms << " ms";
            logInfo(LogCategory::Cli, convert_report.str());
        }
    }

    // Validate mesh if requested (after conversion to mesh)
    if (args.validate) {
        if (args.verbosity > 0) {
            logInfo(LogCategory::Cli, "=== Input Mesh Validation ===");
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            logWarn(LogCategory::Cli, "Warning: Input mesh failed validity checks");
        }

        if (!MeshValidator::is_triangle_mesh(mesh)) {
            logError(LogCategory::Cli, "Error: Mesh must be a triangle mesh");
            return 1;
        }
    }

    // Step 2 & 3: Detect and fill holes (with threading)
    MeshStatistics stats;

    if (args.filling_options.holes_only && !args.use_partitioned) {
        if (args.verbosity > 0) {
            logWarn(LogCategory::Cli,
                    "Warning: --holes_only is supported only in partitioned mode; flag will be ignored.");
        }
        args.filling_options.holes_only = false;
    }

    if (args.use_partitioned) {
        // Use partitioned parallel filling (default mode)
        if (args.verbosity > 0 && args.filling_options.verbose) {
            logInfo(LogCategory::Cli, "=== Partitioned Parallel Filling (Default) ===");
        }

        ParallelPipelineCtx ctx;
        ctx.mesh       = &mesh;
        ctx.thread_mgr = &thread_manager;
        ctx.options    = args.filling_options;

        stats = parallel_fill_partitioned(&ctx, args.filling_options.verbose, debug);
    } else {
        // Use legacy pipeline processing
        if (args.verbosity > 0 && args.filling_options.verbose) {
            logInfo(LogCategory::Cli, "=== Legacy Pipeline Mode ===");
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
            logInfo(LogCategory::Cli, "No holes found. Mesh is already closed.");
        }

        // Save anyway in case of format conversion
        if (mesh_loader_save(mesh, args.output_file.c_str(), MeshLoader::Format::AUTO, true) != 0) {
            logError(LogCategory::Cli, std::string("Error: ") + mesh_loader_last_error());
            return 1;
        }

        return 0;
    }

    // Step 4: Validate output mesh if requested
    if (args.validate) {
        if (args.verbosity > 0) {
            logInfo(LogCategory::Cli, "=== Output Mesh Validation ===");
        }
        MeshValidator::print_statistics(mesh, true);

        if (!MeshValidator::is_valid(mesh)) {
            logWarn(LogCategory::Cli, "Warning: Output mesh failed validity checks");
        }
    }

    // Step 5: Save result
    if (args.verbosity > 0 && args.filling_options.verbose) {
        std::ostringstream save_msg;
        save_msg << "Saving result to: " << args.output_file;
        // Check if output is PLY format (C++17 compatible)
        size_t len = args.output_file.length();
        if (len >= 4) {
            std::string ext = args.output_file.substr(len - 4);
            if (ext == ".ply" || ext == ".PLY") {
                save_msg << " (" << (args.ascii_ply ? "ASCII" : "binary") << " PLY)";
            }
        }
        logInfo(LogCategory::Cli, save_msg.str());
    }

    // Binary PLY by default, ASCII if requested
    auto save_start_time = std::chrono::high_resolution_clock::now();
    bool use_binary_ply  = !args.ascii_ply;
    if (mesh_loader_save(mesh, args.output_file.c_str(), MeshLoader::Format::AUTO, use_binary_ply) != 0) {
        logError(LogCategory::Cli, std::string("Error: ") + mesh_loader_last_error());
        return 1;
    }
    auto save_end_time  = std::chrono::high_resolution_clock::now();
    double save_time_ms = std::chrono::duration<double, std::milli>(save_end_time - save_start_time).count();

    // Step 6: Print detailed statistics if requested
    if (show_stats) {
        std::ostringstream stats_report;
        stats_report << "=== Detailed Statistics ===\n";
        stats_report << "Original mesh:\n";
        stats_report << "  Vertices: " << stats.original_vertices << "\n";
        stats_report << "  Faces: " << stats.original_faces << "\n";

        stats_report << "Final mesh:\n";
        if (args.filling_options.holes_only) {
            stats_report << "  [holes_only] Output contains only reconstructed faces (base mesh faces omitted)\n";
        }
        stats_report << "  Vertices: " << stats.final_vertices << " (+" << mesh_stats_total_vertices_added(stats)
                     << ")\n";
        stats_report << "  Faces: " << stats.final_faces << " (+" << mesh_stats_total_faces_added(stats) << ")\n";

        stats_report << "Hole processing:\n";
        stats_report << "  Detected: " << stats.num_holes_detected << "\n";
        stats_report << "  Filled: " << stats.num_holes_filled << "\n";
        stats_report << "  Failed: " << stats.num_holes_failed << "\n";
        stats_report << "  Skipped: " << stats.num_holes_skipped << "\n";

        stats_report << "Timing breakdown:\n";
        stats_report << "  File load: " << std::fixed << std::setprecision(2) << soup.load_time_ms << " ms\n";
        if (args.enable_preprocessing) {
            stats_report << "  Preprocessing:\n";
            stats_report << "    Soup cleanup: " << prep_stats.soup_cleanup_time_ms << " ms\n";
            stats_report << "    Soup->Mesh conversion: " << prep_stats.soup_to_mesh_time_ms << " ms\n";
            stats_report << "    Mesh cleanup: " << prep_stats.mesh_cleanup_time_ms << " ms\n";
            stats_report << "    Subtotal: " << prep_stats.total_time_ms << " ms\n";
        }
        stats_report << "  Hole filling: " << stats.total_time_ms << " ms\n";
        if (stats.merge_validation_passes > 0 || stats.merge_validation_removed > 0) {
            stats_report << "    Merge validation removed " << stats.merge_validation_removed
                         << " (oob=" << stats.merge_validation_out_of_bounds
                         << ", invalid=" << stats.merge_validation_invalid_cycle
                         << ", edges=" << stats.merge_validation_edge_orientation
                         << ", non_manifold=" << stats.merge_validation_non_manifold
                         << ", passes=" << stats.merge_validation_passes << ")\n";
        }
        stats_report << "  File save: " << save_time_ms << " ms\n";

        auto program_end_time = std::chrono::high_resolution_clock::now();
        double total_program_time_ms
            = std::chrono::duration<double, std::milli>(program_end_time - program_start_time).count();
        // set std::ostringstream precision to 3 decimal places for seconds
        stats_report.precision(4);
        stats_report << "  Total program time: " << total_program_time_ms /1000.0 << " s\n";
        stats_report.precision(2);

        logInfo(LogCategory::Cli, stats_report.str());

        if (args.per_hole_info && !stats.hole_details.empty()) {
            std::ostringstream per_hole_report;
            per_hole_report << "Per-hole details:\n";
            for (size_t i = 0; i < stats.hole_details.size(); ++i) {
                const auto& h = stats.hole_details[i];
                per_hole_report << "  Hole " << (i + 1) << ": ";
                if (h.filled_successfully) {
                    per_hole_report << "OK - " << h.num_faces_added << " faces, " << h.num_vertices_added
                                    << " vertices, " << h.fill_time_ms << " ms";
                    if (!h.fairing_succeeded) {
                        per_hole_report << " [fairing failed]";
                    }
                } else {
                    per_hole_report << "FAILED";
                }
                per_hole_report << "\n";
            }
            logInfo(LogCategory::Cli, per_hole_report.str());
        }

        logInfo(LogCategory::Cli, "===========================");
    }

    if (args.verbosity > 0) {
        logInfo(LogCategory::Cli, "Done! Successfully processed mesh.");
    }

    return 0;
}
