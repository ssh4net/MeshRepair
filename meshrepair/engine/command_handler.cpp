#include "command_handler.h"
#include "mesh_binary.h"
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <thread>

namespace MeshRepair {
namespace Engine {

    CommandHandler::CommandHandler(std::ostream& output_stream, bool verbose, bool show_stats, bool socket_mode)
        : engine_()
        , output_stream_(output_stream)
        , verbose_(verbose)
        , show_stats_(show_stats)
        , socket_mode_(socket_mode)
        , shutdown_requested_(false)
    {
        // Set up engine callbacks
        engine_.set_progress_callback(
            [this](double progress, const std::string& status) { this->on_progress(progress, status); });

        engine_.set_log_callback(
            [this](const std::string& level, const std::string& message) { this->on_log(level, message); });

        engine_.set_cancel_check_callback([this]() { return this->on_cancel_check(); });
    }

    CommandHandler::~CommandHandler() {}

    int CommandHandler::run_message_loop(std::istream& input_stream)
    {
        // In pipe mode: NO stderr output at all (Windows pipe bug - stderr contaminates stdout)
        // In socket mode: stderr is safe
        if (verbose_ && socket_mode_) {
            std::cerr << "[Engine] Starting message loop\n";
        }

        int commands_processed = 0;

        try {
            while (!shutdown_requested_) {
                // Read command
                MessageType msg_type;
                nlohmann::json cmd;

                try {
                    cmd = read_message(input_stream, &msg_type);
                } catch (const std::exception& ex) {
                    // Check if this is EOF (stdin closed)
                    std::string error_msg = ex.what();
                    if (error_msg.find("connection closed") != std::string::npos ||
                        error_msg.find("I/O error") != std::string::npos) {
                        // In pipe mode (batch), stdin closure is expected after all commands are sent
                        // In socket mode, this means client disconnected
                        if (verbose_ && socket_mode_) {
                            std::cerr << "[Engine] Client disconnected\n";
                        }
                        // Pipe mode: silent (no stderr writes)
                        break;  // Exit loop gracefully
                    }
                    // Other errors are fatal
                    throw;
                }

                // Pipe mode: NO stderr output (Windows bug - contaminates stdout)
                // Socket mode: verbose logging OK
                if (verbose_ && socket_mode_) {
                    // Log command summary (don't dump full JSON - may contain large binary data)
                    std::string cmd_name = cmd.value("command", "unknown");
                    std::cerr << "[Engine] Received command: " << cmd_name;

                    // Log size if binary mesh data present
                    if (cmd.contains("params")) {
                        auto params = cmd["params"];
                        if (params.contains("mesh_data_binary")) {
                            std::string binary_data = params["mesh_data_binary"].get<std::string>();
                            std::cerr << ", binary_size=" << binary_data.size() << " bytes";
                        }
                    }

                    std::cerr << "\n";
                }

                // Validate message type
                if (msg_type != MessageType::COMMAND) {
                    nlohmann::json error_resp = create_error_response("Expected COMMAND message type",
                                                                      "protocol_error");
                    // FIX #1: Serialize stdout writes with mutex (prevents race conditions)
                    {
                        std::lock_guard<std::mutex> lock(write_mutex_);
                        write_message(output_stream_, error_resp, MessageType::RESPONSE);
                    }
                    continue;
                }

                // Process command
                nlohmann::json response;
                try {
                    response = process_command(cmd);
                } catch (const std::exception& ex) {
                    // Log full exception details (socket mode only - pipe mode: silent)
                    if (socket_mode_) {
                        std::string cmd_name = cmd.value("command", "unknown");
                        std::cerr << "[Engine] EXCEPTION in command '" << cmd_name << "': " << ex.what() << "\n";
                    }
                    std::string cmd_name = cmd.value("command", "unknown");
                    response = create_error_response(ex.what(), "command_error");
                }

                // Send response
                // FIX #1: Serialize stdout writes with mutex (prevents race conditions)
                {
                    std::lock_guard<std::mutex> lock(write_mutex_);
                    write_message(output_stream_, response, MessageType::RESPONSE);
                }

                // Increment command counter
                commands_processed++;

                // Pipe mode: NO stderr output (Windows bug - contaminates stdout)
                // Socket mode: verbose logging OK
                if (verbose_ && socket_mode_) {
                    // Log response summary (don't dump full JSON - may contain large binary data)
                    std::string resp_type = response.value("type", "unknown");
                    std::string msg = response.value("message", "");

                    std::cerr << "[Engine] Sent response: type=" << resp_type;
                    if (!msg.empty()) {
                        std::cerr << ", message=\"" << msg << "\"";
                    }

                    // Log size if binary mesh data present
                    if (response.contains("mesh_data_binary")) {
                        std::string binary_data = response["mesh_data_binary"].get<std::string>();
                        std::cerr << ", binary_size=" << binary_data.size() << " bytes";
                    }

                    std::cerr << "\n";
                }
            }

            // In pipe mode (batch), DO NOT exit immediately
            // Windows forcefully closes pipes on process exit, potentially losing buffered data
            // Instead, flush stdout and wait indefinitely for Python to terminate the process
            // This keeps the pipe open until Python has read all responses
            if (!socket_mode_) {
                // Pipe mode: NO stderr output (Windows bug)
                // Just flush and wait silently

                // Force flush all buffered data to pipe
                output_stream_.flush();
                std::cout.flush();

                // Wait indefinitely for process termination
                // Python will kill the process once it has read all responses from stdout
                // This prevents Windows from closing stdout pipe prematurely
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }

            if (verbose_ && socket_mode_) {
                std::cerr << "[Engine] Clean shutdown\n";
            }
            return 0;
        } catch (const std::exception& ex) {
            // Pipe mode: silent (no stderr) | Socket mode: log error
            if (socket_mode_) {
                std::cerr << "[Engine] FATAL ERROR: " << ex.what() << "\n";
            }
            return 1;
        }
    }

    nlohmann::json CommandHandler::process_command(const nlohmann::json& cmd)
    {
        // Validate basic structure
        if (!cmd.is_object() || !cmd.contains("command")) {
            return create_error_response("Invalid command: missing 'command' field", "invalid_command");
        }

        if (!cmd["command"].is_string()) {
            return create_error_response("Invalid command: 'command' must be a string", "invalid_command");
        }

        std::string command   = cmd["command"].get<std::string>();
        nlohmann::json params = cmd.value("params", nlohmann::json::object());

        // Route to appropriate handler
        if (command == "init") {
            return handle_init(params);
        } else if (command == "load_mesh") {
            return handle_load_mesh(params);
        } else if (command == "preprocess") {
            return handle_preprocess(params);
        } else if (command == "detect_holes") {
            return handle_detect_holes(params);
        } else if (command == "fill_holes") {
            return handle_fill_holes(params);
        } else if (command == "save_mesh") {
            return handle_save_mesh(params);
        } else if (command == "get_info") {
            return handle_get_info(params);
        } else if (command == "shutdown") {
            return handle_shutdown(params);
        } else {
            return create_error_response("Unknown command: " + command, "unknown_command");
        }
    }

    nlohmann::json CommandHandler::handle_init(const nlohmann::json& params)
    {
        nlohmann::json init_params = params;

        // Pipe mode must never emit stdout logging from internal subsystems.
        // Force verbose/debug flags off to prevent std::cout prints (e.g. ThreadManager).
        if (!socket_mode_) {
            init_params["verbose"] = false;
            init_params["debug"]   = false;
        }

        engine_.initialize(init_params);

        nlohmann::json resp = create_success_response("Engine initialized");
        resp["version"]     = Config::VERSION;
        resp["version_major"] = Config::VERSION_MAJOR;
        resp["version_minor"] = Config::VERSION_MINOR;
        resp["version_patch"] = Config::VERSION_PATCH;
        resp["build_date"]  = Config::BUILD_DATE;
        resp["build_time"]  = Config::BUILD_TIME;
        return resp;
    }

    nlohmann::json CommandHandler::handle_load_mesh(const nlohmann::json& params)
    {
        auto start_time = std::chrono::high_resolution_clock::now();

        // Check if binary mesh data is provided (base64-encoded)
        if (params.contains("mesh_data_binary")) {
            // Load mesh from binary data (pipe mode - binary format)
            std::string base64_data = params["mesh_data_binary"].get<std::string>();

            // Pipe mode: NO stderr (Windows bug)
            if ((verbose_ || show_stats_) && socket_mode_) {
                std::cerr << "[Engine] Loading mesh from binary data...\n";
                std::cerr << "[Engine]   Base64 size: " << base64_data.size() << " bytes\n";
            }

            try {
                auto decode_start = std::chrono::high_resolution_clock::now();

                // Decode base64 to binary
                std::vector<uint8_t> binary_data = base64_decode(base64_data);

                auto decode_end = std::chrono::high_resolution_clock::now();
                auto decode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(decode_end - decode_start).count();

                if (show_stats_ && socket_mode_) {
                    std::cerr << "[Engine]   Binary size: " << binary_data.size() << " bytes\n";
                    std::cerr << "[Engine]   Base64 decode time: " << decode_ms << " ms\n";
                }

                auto deserialize_start = std::chrono::high_resolution_clock::now();

                // Deserialize binary mesh
                Mesh mesh = deserialize_mesh_binary(binary_data);

                auto deserialize_end = std::chrono::high_resolution_clock::now();
                auto deserialize_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deserialize_end - deserialize_start).count();

                if ((verbose_ || show_stats_) && socket_mode_) {
                    std::cerr << "[Engine]   Vertices: " << mesh.number_of_vertices() << "\n";
                    std::cerr << "[Engine]   Faces: " << mesh.number_of_faces() << "\n";
                }

                if (show_stats_ && socket_mode_) {
                    std::cerr << "[Engine]   Deserialization time: " << deserialize_ms << " ms\n";
                }

                // Load mesh into engine
                engine_.set_mesh(std::move(mesh));

                auto end_time = std::chrono::high_resolution_clock::now();
                auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

                if (show_stats_ && socket_mode_) {
                    std::cerr << "[Engine]   Total load time: " << total_ms << " ms\n";
                }

                // Build response
                nlohmann::json resp = create_success_response("Mesh loaded from binary data");
                resp["mesh_info"]   = engine_.get_mesh_info();
                if (show_stats_ && socket_mode_) {
                    resp["load_time_ms"] = total_ms;
                    resp["decode_time_ms"] = decode_ms;
                    resp["deserialize_time_ms"] = deserialize_ms;
                }
                return resp;
            } catch (const std::exception& ex) {
                return create_error_response(std::string("Failed to load binary mesh: ") + ex.what(),
                                            "invalid_params");
            }
        }
        // Check if mesh data is provided directly (pipe mode - JSON format)
        else if (params.contains("mesh_data")) {
            // Load mesh from data (pipe mode)
            const auto& mesh_data = params["mesh_data"];

            if (!mesh_data.contains("vertices") || !mesh_data.contains("faces")) {
                return create_error_response("mesh_data must contain 'vertices' and 'faces'", "invalid_params");
            }

            // Parse vertices array
            std::vector<std::array<double, 3>> vertices;
            for (const auto& v : mesh_data["vertices"]) {
                if (!v.is_array() || v.size() != 3) {
                    return create_error_response("Each vertex must be an array of 3 numbers", "invalid_params");
                }
                vertices.push_back({v[0].get<double>(), v[1].get<double>(), v[2].get<double>()});
            }

            // Parse faces array
            std::vector<std::array<int, 3>> faces;
            for (const auto& f : mesh_data["faces"]) {
                if (!f.is_array() || f.size() != 3) {
                    return create_error_response("Each face must be an array of 3 indices (triangles only)",
                                                "invalid_params");
                }
                faces.push_back({f[0].get<int>(), f[1].get<int>(), f[2].get<int>()});
            }

            // Load mesh from data
            engine_.load_mesh_from_data(vertices, faces);

            // Build response
            nlohmann::json resp = create_success_response("Mesh loaded from data");
            resp["mesh_info"]   = engine_.get_mesh_info();
            return resp;
        }
        else if (params.contains("file_path")) {
            // Load mesh from file (legacy mode)
            std::string file_path = params["file_path"].get<std::string>();
            bool force_cgal       = params.value("force_cgal_loader", false);

            // Load mesh
            engine_.load_mesh(file_path, force_cgal);

            // Build response
            nlohmann::json resp = create_success_response("Mesh loaded from file");
            resp["mesh_info"]   = engine_.get_mesh_info();
            return resp;
        }
        else {
            return create_error_response("Missing required parameter: 'mesh_data_binary', 'mesh_data', or 'file_path'",
                                        "invalid_params");
        }
    }

    nlohmann::json CommandHandler::handle_preprocess(const nlohmann::json& params)
    {
        auto start_time = std::chrono::high_resolution_clock::now();

        PreprocessingOptions options = parse_preprocessing_options(params);

        if ((verbose_ || show_stats_) && socket_mode_) {
            std::cerr << "[Engine] Preprocessing mesh...\n";
        }

        engine_.preprocess_mesh(options);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        if (show_stats_ && socket_mode_) {
            std::cerr << "[Engine]   Preprocessing time: " << total_ms << " ms\n";
        }

        nlohmann::json resp = create_success_response("Preprocessing complete");
        resp["stats"]       = engine_.get_preprocessing_stats();
        resp["mesh_info"]   = engine_.get_mesh_info();
        if (show_stats_ && socket_mode_) {
            resp["preprocess_time_ms"] = total_ms;
        }
        return resp;
    }

    nlohmann::json CommandHandler::handle_detect_holes(const nlohmann::json& params)
    {
        auto start_time = std::chrono::high_resolution_clock::now();

        FillingOptions options = parse_filling_options(params);

        if ((verbose_ || show_stats_) && socket_mode_) {
            std::cerr << "[Engine] Detecting holes...\n";
        }

        engine_.detect_holes(options);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        if (show_stats_ && socket_mode_) {
            std::cerr << "[Engine]   Detection time: " << total_ms << " ms\n";
        }

        nlohmann::json resp = create_success_response("Hole detection complete");
        resp["stats"]       = engine_.get_hole_detection_stats();
        if (show_stats_ && socket_mode_) {
            resp["detect_time_ms"] = total_ms;
        }
        return resp;
    }

    nlohmann::json CommandHandler::handle_fill_holes(const nlohmann::json& params)
    {
        auto start_time = std::chrono::high_resolution_clock::now();

        FillingOptions options = parse_filling_options(params);
        bool use_partitioned   = params.value("use_partitioned", true);

        if ((verbose_ || show_stats_) && socket_mode_) {
            std::cerr << "[Engine] Filling holes (mode: " << (use_partitioned ? "partitioned" : "legacy") << ")...\n";
        }

        engine_.fill_holes(options, use_partitioned);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        if (show_stats_ && socket_mode_) {
            std::cerr << "[Engine]   Filling time: " << total_ms << " ms\n";
        }

        nlohmann::json resp = create_success_response("Hole filling complete");
        resp["stats"]       = engine_.get_hole_filling_stats();
        resp["mesh_info"]   = engine_.get_mesh_info();
        if (show_stats_ && socket_mode_) {
            resp["fill_time_ms"] = total_ms;
        }
        return resp;
    }

    nlohmann::json CommandHandler::handle_save_mesh(const nlohmann::json& params)
    {
        auto start_time = std::chrono::high_resolution_clock::now();

        // Check if binary data is requested (pipe mode - binary format)
        bool return_binary = params.value("return_binary", false);

        if (return_binary) {
            // Extract mesh and serialize to binary format
            try {
                const Mesh& mesh = engine_.get_mesh();

                if ((verbose_ || show_stats_) && socket_mode_) {
                    std::cerr << "[Engine] Serializing mesh to binary data...\n";
                    std::cerr << "[Engine]   Vertices: " << mesh.number_of_vertices() << "\n";
                    std::cerr << "[Engine]   Faces: " << mesh.number_of_faces() << "\n";
                }

                auto serialize_start = std::chrono::high_resolution_clock::now();

                // Serialize to binary
                std::vector<uint8_t> binary_data = serialize_mesh_binary(mesh);

                auto serialize_end = std::chrono::high_resolution_clock::now();
                auto serialize_ms = std::chrono::duration_cast<std::chrono::milliseconds>(serialize_end - serialize_start).count();

                if (show_stats_ && socket_mode_) {
                    std::cerr << "[Engine]   Binary size: " << binary_data.size() << " bytes\n";
                    std::cerr << "[Engine]   Serialization time: " << serialize_ms << " ms\n";
                }

                auto encode_start = std::chrono::high_resolution_clock::now();

                // Encode as base64
                std::string base64_data = base64_encode(binary_data);

                auto encode_end = std::chrono::high_resolution_clock::now();
                auto encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(encode_end - encode_start).count();

                auto end_time = std::chrono::high_resolution_clock::now();
                auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

                if (show_stats_ && socket_mode_) {
                    std::cerr << "[Engine]   Base64 size: " << base64_data.size() << " bytes\n";
                    std::cerr << "[Engine]   Base64 encode time: " << encode_ms << " ms\n";
                    std::cerr << "[Engine]   Total save time: " << total_ms << " ms\n";
                }

                nlohmann::json resp = create_success_response("Mesh data extracted (binary)");
                resp["mesh_data_binary"] = base64_data;
                if (show_stats_ && socket_mode_) {
                    resp["save_time_ms"] = total_ms;
                    resp["serialize_time_ms"] = serialize_ms;
                    resp["encode_time_ms"] = encode_ms;
                }
                return resp;
            } catch (const std::exception& ex) {
                return create_error_response(std::string("Failed to serialize binary mesh: ") + ex.what(),
                                            "serialization_error");
            }
        }
        // Check if file_path is provided (legacy mode) or return data (pipe mode - JSON format)
        else if (params.value("return_data", false)) {
            // Extract mesh data and return it (pipe mode - JSON format)
            nlohmann::json mesh_data = engine_.save_mesh_to_data();

            nlohmann::json resp = create_success_response("Mesh data extracted");
            resp["mesh_data"]   = mesh_data;
            return resp;
        }
        else if (params.contains("file_path")) {
            // Save mesh to file (legacy mode)
            std::string file_path = params["file_path"].get<std::string>();
            bool binary_ply       = params.value("binary_ply", true);

            engine_.save_mesh(file_path, binary_ply);

            nlohmann::json resp = create_success_response("Mesh saved to file");
            return resp;
        }
        else {
            return create_error_response(
                "Missing required parameter: 'file_path', 'return_data', or 'return_binary'", "invalid_params");
        }
    }

    nlohmann::json CommandHandler::handle_get_info(const nlohmann::json& params)
    {
        (void)params;  // Unused parameter
        nlohmann::json resp = create_success_response();
        resp["state"]       = static_cast<int>(engine_.get_state());
        resp["has_mesh"]    = engine_.has_mesh();

        if (engine_.has_mesh()) {
            resp["mesh_info"] = engine_.get_mesh_info();
        }

        return resp;
    }

    nlohmann::json CommandHandler::handle_shutdown(const nlohmann::json& params)
    {
        (void)params;  // Unused parameter
        if (socket_mode_) {
            // In socket mode: cleanup/reset but keep running
            engine_.clear_mesh();

            if (verbose_ && socket_mode_) {
                std::cerr << "[Engine] Cleanup requested (socket mode - engine remains running)\n";
            }

            return create_success_response("Engine state reset (socket mode)");
        } else {
            // In pipe mode: actual shutdown
            shutdown_requested_ = true;

            if (verbose_ && socket_mode_) {
                std::cerr << "[Engine] Shutdown requested (pipe mode)\n";
            }

            return create_success_response("Shutdown requested");
        }
    }

    FillingOptions CommandHandler::parse_filling_options(const nlohmann::json& params)
    {
        FillingOptions options;

        // Parse all filling options from params
        options.fairing_continuity         = params.value("continuity", 1);
        options.max_hole_boundary_vertices = params.value("max_boundary", 1000u);
        options.max_hole_diameter_ratio    = params.value("max_diameter", 0.1);
        options.use_2d_cdt                 = params.value("use_2d_cdt", true);
        options.use_3d_delaunay            = params.value("use_3d_delaunay", true);
        options.skip_cubic_search          = params.value("skip_cubic", false);
        options.refine                     = params.value("refine", true);
        options.verbose                    = params.value("verbose", false);
        options.show_progress              = params.value("show_progress", false);

        return options;
    }

    PreprocessingOptions CommandHandler::parse_preprocessing_options(const nlohmann::json& params)
    {
        PreprocessingOptions options;

        // Parse all preprocessing options from params
        options.remove_duplicates      = params.value("remove_duplicates", true);
        options.remove_non_manifold    = params.value("remove_non_manifold", true);
        options.remove_3_face_fans     = params.value("remove_3_face_fans", true);
        options.remove_isolated        = params.value("remove_isolated", true);
        options.keep_largest_component = params.value("keep_largest_component", true);
        options.non_manifold_passes    = params.value("non_manifold_passes", 10u);
        options.verbose                = params.value("verbose", false);
        options.debug                  = params.value("debug", false);

        return options;
    }

    void CommandHandler::on_progress(double progress, const std::string& status)
    {
        // DISABLED: Event messages cause protocol corruption on Windows
        // TODO: Fix Windows pipe handling for interleaved stdout/stderr
        //
        // nlohmann::json event = create_progress_event(progress, status);
        // try {
        //     write_message(output_stream_, event, MessageType::EVENT);
        // } catch (const std::exception& ex) {
        //     if (verbose_ && socket_mode_) {
        //         std::cerr << "[Engine] Failed to send progress event: " << ex.what() << "\n";
        //     }
        // }

        // In pipe mode: NO stderr output at all
        // Windows bug: stderr writes from callback threads can contaminate stdout bytes
        // In socket mode: stderr is safe (no Windows pipe issues)
        if (!socket_mode_) {
            return;  // Silent in pipe mode
        }

        // Socket mode: log to stderr
        if (verbose_ && socket_mode_) {
            std::cerr << "[Engine] Progress: " << (progress * 100.0) << "% - " << status << "\n";
        }
    }

    void CommandHandler::on_log(const std::string& level, const std::string& message)
    {
        // DISABLED: Event messages cause protocol corruption on Windows
        // TODO: Fix Windows pipe handling for interleaved stdout/stderr
        //
        // nlohmann::json event = create_log_event(level, message);
        // try {
        //     write_message(output_stream_, event, MessageType::EVENT);
        // } catch (const std::exception& ex) {
        //     if (verbose_ && socket_mode_) {
        //         std::cerr << "[Engine] Failed to send log event: " << ex.what() << "\n";
        //     }
        // }

        // In pipe mode: NO stderr output at all
        // Windows bug: stderr writes from callback threads can contaminate stdout bytes
        // In socket mode: stderr is safe (no Windows pipe issues)
        if (!socket_mode_) {
            return;  // Silent in pipe mode
        }

        // Socket mode: log to stderr
        if (verbose_ && socket_mode_) {
            std::cerr << "[Engine:" << level << "] " << message << "\n";
        }
    }

    bool CommandHandler::on_cancel_check()
    {
        // TODO: Implement cancellation support
        // For now, never cancel
        return false;
    }

}  // namespace Engine
}  // namespace MeshRepair
