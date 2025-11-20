// Engine mode entry point (IPC mode for Blender addon)

#include "include/config.h"
#include "include/debug_path.h"
#include "engine/command_handler.h"
#include "engine/socket_stream.h"
#include "include/help_printer.h"
#include <iostream>
#include <cstring>
#include <memory>

#ifdef _WIN32
#    include <io.h>
#    include <fcntl.h>
#endif

using namespace MeshRepair;
using namespace MeshRepair::Engine;

int
engine_main(int argc, char** argv)
{
    bool socket_mode = false;
    // Check for engine-specific options
    int verbosity = 1;  // Default: info level (stats)
    int socket_port = 0;  // 0 = pipe mode, >0 = socket mode
    std::string temp_dir;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0) {
            // Check if next arg is a number
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                verbosity = std::atoi(argv[i + 1]);
                ++i;  // Skip the level argument
                if (verbosity < 0 || verbosity > 4) {
                    std::cerr << "ERROR: Verbosity level must be 0-4\n";
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
                    std::cerr << "ERROR: Invalid port number: " << argv[i] << "\n";
                    std::cerr << "Port must be between 1 and 65535\n";
                    return 1;
                }
            } else {
                std::cerr << "ERROR: --socket requires a port number\n";
                std::cerr << "Usage: meshrepair --socket PORT\n";
                return 1;
            }
        } else if ((std::strcmp(argv[i], "--temp-dir") == 0 || std::strcmp(argv[i], "--temp") == 0) && i + 1 < argc) {
            temp_dir = argv[i + 1];
            ++i;
        } else if (std::strcmp(argv[i], "--temp-dir") == 0 || std::strcmp(argv[i], "--temp") == 0) {
            std::cerr << "ERROR: --temp-dir requires a path argument\n";
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
    bool debug      = (verbosity >= 4);  // PLY file dumps only at level 4

    socket_mode = (socket_port > 0);

    if (!temp_dir.empty()) {
        MeshRepair::DebugPath::set_base_directory(temp_dir);
    }

    // Socket mode
    if (socket_mode) {
        // Initialize sockets (Windows only)
        if (!SocketServer::init_sockets()) {
            std::cerr << "ERROR: Failed to initialize socket library\n";
            return 1;
        }

        std::cerr << "MeshRepair v" << Config::VERSION << " - Engine Mode (Socket)\n";
        std::cerr << "Starting socket server on port " << socket_port << "...\n";

        try {
            // Create socket server
            SocketServer server;
            if (!server.listen(socket_port)) {
                std::cerr << "ERROR: Failed to start socket server on port " << socket_port << "\n";
                std::cerr << "Make sure the port is not already in use.\n";
                SocketServer::cleanup_sockets();
                return 1;
            }

            std::cerr << "Server listening on port " << socket_port << "\n";
            std::cerr << "Press Ctrl+C to stop the server\n\n";

            // Keep accepting connections until manually stopped
            while (true) {
                std::cerr << "Waiting for addon connection...\n";

                // Accept client connection
                socket_t client_socket = server.accept_client();
                if (client_socket == INVALID_SOCKET) {
                    std::cerr << "ERROR: Failed to accept client connection\n";
                    continue;  // Try again
                }

                std::cerr << "Client connected!\n";
                if (verbose) {
                    std::cerr << "Verbose mode enabled\n";
                    std::cerr << "Protocol: Binary-framed JSON messages\n";
                    std::cerr << "\n";
                }
                if (show_stats) {
                    std::cerr << "Stats mode enabled\n";
                }

                // Create socket streams
                SocketIStream input_stream(client_socket);
                SocketOStream output_stream(client_socket);

                // Create command handler (socket mode = true, pass verbose and stats flags)
                CommandHandler handler(output_stream, verbose, show_stats, true);

                // Run message loop (blocks until client disconnects)
                int exit_code = handler.run_message_loop(input_stream);

                if (verbose) {
                    std::cerr << "Session ended (exit code: " << exit_code << ")\n\n";
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
            std::cerr << "FATAL ERROR in socket mode: " << ex.what() << "\n";
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
    if (verbose && socket_mode) {
        std::cerr << "MeshRepair v" << Config::VERSION << " - Engine Mode (Pipe)\n";
        std::cerr << "Starting IPC engine...\n";
        std::cerr << "Protocol: Binary-framed JSON messages\n";
        std::cerr << "Input: stdin (binary) | Output: stdout (binary) | Logs: stderr\n";
        std::cerr << "\n";
        std::cerr << "Batch mode: Engine will process all commands from stdin until EOF.\n";
        std::cerr << "This pattern avoids Windows pipe EOF issues by sending all commands\n";
        std::cerr << "upfront, closing stdin to signal end of input.\n";
        std::cerr << "\n";
        std::cerr.flush();  // Flush stderr before first stdout write
    }
    if (show_stats && socket_mode) {
        std::cerr << "Stats mode enabled\n";
        std::cerr.flush();  // Flush stderr before first stdout write
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
        // Create command handler (pipe mode = false, pass verbose and stats flags)
        CommandHandler handler(std::cout, verbose, show_stats, false);

        // Run message loop
        int exit_code = handler.run_message_loop(std::cin);

        if (verbose && socket_mode) {
            std::cerr << "Engine shutdown complete (exit code: " << exit_code << ")\n";
        }

        return exit_code;
    } catch (const std::exception& ex) {
        if (socket_mode) {
            std::cerr << "FATAL ERROR in pipe mode: " << ex.what() << "\n";
        }
        return 1;
    }
}
