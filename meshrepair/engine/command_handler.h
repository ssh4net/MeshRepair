#pragma once

#include "engine_wrapper.h"
#include "protocol.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <mutex>

namespace MeshRepair {
namespace Engine {

    // Command handler class
    // Routes commands to appropriate engine methods and builds responses
    class CommandHandler {
    public:
        CommandHandler(std::ostream& output_stream, bool verbose = false, bool show_stats = false, bool socket_mode = false);
        ~CommandHandler();

        // Main command processing loop
        // Reads commands from input_stream, processes them, writes responses to output_stream
        // Returns: exit code (0 = clean shutdown, 1 = error)
        int run_message_loop(std::istream& input_stream);

        // Process a single command
        // Returns: response JSON
        nlohmann::json process_command(const nlohmann::json& cmd);

    private:
        // Command handlers
        nlohmann::json handle_init(const nlohmann::json& params);
        nlohmann::json handle_load_mesh(const nlohmann::json& params);
        nlohmann::json handle_preprocess(const nlohmann::json& params);
        nlohmann::json handle_detect_holes(const nlohmann::json& params);
        nlohmann::json handle_fill_holes(const nlohmann::json& params);
        nlohmann::json handle_save_mesh(const nlohmann::json& params);
        nlohmann::json handle_get_info(const nlohmann::json& params);
        nlohmann::json handle_shutdown(const nlohmann::json& params);

        // Helper: Parse FillingOptions from JSON
        FillingOptions parse_filling_options(const nlohmann::json& params);

        // Helper: Parse PreprocessingOptions from JSON
        PreprocessingOptions parse_preprocessing_options(const nlohmann::json& params);

        // Engine wrapper
        EngineWrapper engine_;

        // Output stream for responses
        std::ostream& output_stream_;

        // Verbose mode
        bool verbose_;

        // Stats mode
        bool show_stats_;

        // Socket mode (persistent server)
        bool socket_mode_;

        // Shutdown flag
        bool shutdown_requested_;

        // Mutex for stdout writes (prevents protocol corruption on Windows)
        std::mutex write_mutex_;

        // Callbacks for engine
        void on_progress(double progress, const std::string& status);
        void on_log(const std::string& level, const std::string& message);
        bool on_cancel_check();
    };

}  // namespace Engine
}  // namespace MeshRepair
