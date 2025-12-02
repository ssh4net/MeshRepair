// Engine mode entry point (IPC mode for Blender addon)

#include "include/config.h"
#include "include/debug_path.h"
#include "engine/engine_dispatch.h"
#include "engine/socket_stream.h"
#include "include/help_printer.h"
#include "include/logger.h"
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

#if defined(MESHREPAIR_USE_SPDLOG)
#    include <spdlog/spdlog.h>
#endif

#ifdef _WIN32
#    include <io.h>
#    include <fcntl.h>
#endif

using namespace MeshRepair;
using namespace MeshRepair::Engine;

int
engine_main(int argc, char** argv)
{
    LoggerConfig log_cfg;
    log_cfg.useStderr = true;
    initLogger(log_cfg);

    bool socket_mode = false;
    // Check for engine-specific options
    int verbosity   = 1;  // Default: info level (stats)
    int socket_port = 0;  // 0 = pipe mode, >0 = socket mode
    std::string temp_dir;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0) {
            // Check if next arg is a number
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                verbosity = std::atoi(argv[i + 1]);
                ++i;  // Skip the level argument
                if (verbosity < 0 || verbosity > 4) {
                    logError(LogCategory::Engine, "ERROR: Verbosity level must be 0-4");
                    return 1;
                }
            } else {
                verbosity = 2;  // Default to verbose if no level specified
            }
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            MeshRepair::print_help(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--socket") == 0) {
            // Next argument should be port number
            if (i + 1 < argc) {
                socket_port = std::atoi(argv[i + 1]);
                ++i;  // Skip port argument
                if (socket_port <= 0 || socket_port > 65535) {
                    logError(LogCategory::Engine, std::string("ERROR: Invalid port number: ") + argv[i]);
                    logError(LogCategory::Engine, "Port must be between 1 and 65535");
                    return 1;
                }
            } else {
                logError(LogCategory::Engine, "ERROR: --socket requires a port number");
                logInfo(LogCategory::Engine, "Usage: meshrepair --socket PORT");
                return 1;
            }
        } else if ((std::strcmp(argv[i], "--temp-dir") == 0 || std::strcmp(argv[i], "--temp") == 0) && i + 1 < argc) {
            temp_dir = argv[i + 1];
            ++i;
        } else if (std::strcmp(argv[i], "--temp-dir") == 0 || std::strcmp(argv[i], "--temp") == 0) {
            logError(LogCategory::Engine, "ERROR: --temp-dir requires a path argument");
            return 1;
        }
        // Skip --engine flag itself
        else if (std::strcmp(argv[i], "--engine") == 0) {
            continue;
        }
    }

    // Derive flags from verbosity level
    // 0 = quiet, 1 = info (stats), 2 = verbose, 3 = debug, 4 = trace (PLY dumps)
    bool show_stats = (verbosity >= 1);
    bool verbose    = (verbosity >= 2);

    log_cfg.minLevel = logLevelFromVerbosity(verbosity);
    setLogLevel(log_cfg.minLevel);

    socket_mode = (socket_port > 0);

    if (!temp_dir.empty()) {
        MeshRepair::DebugPath::set_base_directory(temp_dir);
    }

    // Socket mode
    if (socket_mode) {
        // Initialize sockets (Windows only)
        if (!SocketServer::init_sockets()) {
            logError(LogCategory::Engine, "ERROR: Failed to initialize socket library");
            return 1;
        }

        logInfo(LogCategory::Engine, "MeshRepair v" + std::string(Config::VERSION) + " - Engine Mode (Socket)");
        logInfo(LogCategory::Engine, "Starting socket server on port " + std::to_string(socket_port) + "...");

        try {
            // Create socket server
            SocketServer server;
            if (!server.listen(socket_port)) {
                logError(LogCategory::Engine,
                         "ERROR: Failed to start socket server on port " + std::to_string(socket_port));
                logError(LogCategory::Engine, "Make sure the port is not already in use.");
                SocketServer::cleanup_sockets();
                return 1;
            }

            logInfo(LogCategory::Engine, "Server listening on port " + std::to_string(socket_port));
            logInfo(LogCategory::Engine, "Press Ctrl+C to stop the server");

            // Keep accepting connections until manually stopped
            while (true) {
                logInfo(LogCategory::Engine, "Waiting for addon connection...");

                // Accept client connection
                socket_t client_socket = server.accept_client();
                if (client_socket == INVALID_SOCKET) {
                    logError(LogCategory::Engine, "ERROR: Failed to accept client connection");
                    continue;  // Try again
                }

                logInfo(LogCategory::Engine, "Client connected!");
                if (verbose) {
                    logInfo(LogCategory::Engine, "Verbose mode enabled");
                    logInfo(LogCategory::Engine, "Protocol: Binary-framed JSON messages");
                }
                if (show_stats) {
                    logInfo(LogCategory::Engine, "Stats mode enabled");
                }

                // Create socket streams
                SocketIStream input_stream(client_socket);
                SocketOStream output_stream(client_socket);

                EngineWrapper engine;

                // Run message loop using procedural dispatcher
                bool shutdown_requested = false;
                while (!shutdown_requested) {
                    MessageType msg_type;
                    nlohmann::json cmd;
                    try {
                        cmd = read_message(input_stream, &msg_type);
                    } catch (const std::exception& ex) {
                        std::string error_msg = ex.what();
                        if (error_msg.find("connection closed") != std::string::npos
                            || error_msg.find("I/O error") != std::string::npos) {
                            break;
                        }
                        throw;
                    }

                    if (msg_type != MessageType::COMMAND) {
                        nlohmann::json error_resp = create_error_response("Expected COMMAND message type",
                                                                          "protocol_error");
                        write_message(output_stream, error_resp, MessageType::RESPONSE);
                        continue;
                    }

                    nlohmann::json response = dispatch_command_procedural(engine, cmd, verbose, show_stats, true);
                    write_message(output_stream, response, MessageType::RESPONSE);
                    shutdown_requested = (cmd.value("command", "") == "shutdown");
                }

                if (verbose) {
                    logInfo(LogCategory::Engine, "Session ended");
                }

                // Close client socket
                closesocket(client_socket);

                // Loop back to accept next connection
            }

            // This code is unreachable (Ctrl+C stops the server)
            server.close();
            SocketServer::cleanup_sockets();

            return 0;
        } catch (const std::exception& ex) {
            logError(LogCategory::Engine, std::string("FATAL ERROR in socket mode: ") + ex.what());
            SocketServer::cleanup_sockets();
            return 1;
        }
    }

    // Set binary mode for stdin/stdout FIRST (before any stderr writes)
    // This prevents Windows from mixing stderr bytes into stdout
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);  // Also set stderr to binary to prevent mixing
#endif

    // Pipe mode (default)
    // Write startup messages to stderr AFTER binary mode is set
    if (verbose) {
        std::ostringstream banner;
        banner << "MeshRepair v" << Config::VERSION << "\n";
        banner << "Built on " << Config::BUILD_DATE << " at " << Config::BUILD_TIME << " (" << MESHREPAIR_BUILD_CONFIG
               << ")\n";
        banner << "Engine Mode (Pipe)\n\n";
        banner << "Starting IPC engine...\n";
        banner << "Protocol: Binary-framed JSON messages\n";
        banner << "Input: stdin (binary) | Output: stdout (binary) | Logs: stderr\n\n";
        banner << "Batch mode: Engine will process all commands from stdin until EOF.\n";
        banner << "This pattern avoids Windows pipe EOF issues by sending all commands\n";
        banner << "upfront, closing stdin to signal end of input.";
        logInfo(LogCategory::Engine, banner.str());
#if defined(MESHREPAIR_USE_SPDLOG)
        if (auto logger = spdlog::default_logger()) {
            logger->flush();
        }
#endif
    }
    if (show_stats) {
        logInfo(LogCategory::Engine, "Stats mode enabled");
#if defined(MESHREPAIR_USE_SPDLOG)
        if (auto logger = spdlog::default_logger()) {
            logger->flush();
        }
#endif
    }

    // NOTE: We do NOT untie cin/cout or disable sync_with_stdio because:
    // 1. On Windows, untying cin.tie(nullptr) causes cin to misbehave with binary pipes
    // 2. std::cin is pre-initialized in text mode before _setmode is called
    // 3. Changing FD mode with _setmode doesn't update cin's internal buffers
    // 4. Keep default C++ stream behavior to avoid Windows-specific bugs
    //
    // Alternative considered: Using raw file descriptors (read/write on FD 0/1)
    // but keeping std::cin/cout for now to minimize changes

    try {
        // Procedural command processing loop using dispatcher
        if (verbose) {
            logInfo(LogCategory::Engine, "[Engine] Using procedural dispatcher");
        }

        EngineWrapper engine;
        bool shutdown_requested = false;
        while (!shutdown_requested) {
            MessageType msg_type;
            nlohmann::json cmd;
            try {
                cmd = read_message(std::cin, &msg_type);
            } catch (const std::exception& ex) {
                std::string error_msg = ex.what();
                if (error_msg.find("connection closed") != std::string::npos
                    || error_msg.find("I/O error") != std::string::npos) {
                    break;  // graceful shutdown on EOF/close
                }
                throw;
            }

            if (msg_type != MessageType::COMMAND) {
                nlohmann::json error_resp = create_error_response("Expected COMMAND message type", "protocol_error");
                write_message(std::cout, error_resp, MessageType::RESPONSE);
                continue;
            }

            nlohmann::json response = dispatch_command_procedural(engine, cmd, verbose, show_stats, false);
            write_message(std::cout, response, MessageType::RESPONSE);
            shutdown_requested = (cmd.value("command", "") == "shutdown");
        }
        return 0;
    } catch (const std::exception& ex) {
        logError(LogCategory::Engine, std::string("FATAL ERROR in pipe mode: ") + ex.what());
        return 1;
    }
}
