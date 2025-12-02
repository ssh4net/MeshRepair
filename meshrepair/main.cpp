// Main entry point for meshrepair
// Routes to CLI mode (default) or engine mode (--engine flag)

#include <cstring>
#include <string>

#include "include/help_printer.h"
#include "include/logger.h"

// Forward declarations
int
cli_main(int argc, char** argv);
int
engine_main(int argc, char** argv);

int
main(int argc, char** argv)
{
    MeshRepair::LoggerConfig log_cfg;
    log_cfg.useStderr = false;
    MeshRepair::initLogger(log_cfg);

    // Check for --engine flag (can be anywhere in args)
    bool engine_mode    = false;
    bool help_requested = (argc <= 1);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--engine") == 0) {
            engine_mode = true;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            help_requested = true;
        }
    }

    if (help_requested) {
        MeshRepair::print_help(argv[0]);
        return 0;
    }

    try {
        if (engine_mode) {
            // IPC engine mode for addon integration
            return engine_main(argc, argv);
        } else {
            // Traditional CLI mode
            return cli_main(argc, argv);
        }
    } catch (const std::exception& ex) {
        MeshRepair::logError(MeshRepair::LogCategory::Cli, std::string("FATAL ERROR: ") + ex.what());
        return 1;
    }
}
