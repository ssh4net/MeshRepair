#pragma once

#include "../include/types.h"
#include "../include/mesh_loader.h"
#include "../include/hole_detector.h"
#include "../include/hole_filler.h"
#include "../include/mesh_preprocessor.h"
#include "../include/thread_manager.h"
#include "../include/parallel_hole_filler.h"
#include "../include/pipeline_processor.h"

#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <fstream>

namespace MeshRepair {
namespace Engine {

    // Callback types for progress reporting and logging
    using ProgressCallback    = std::function<void(double progress, const std::string& status)>;
    using LogCallback         = std::function<void(const std::string& level, const std::string& message)>;
    using CancelCheckCallback = std::function<bool()>;  // Returns true if operation should be cancelled

    // Engine state
    enum class EngineState {
        UNINITIALIZED,
        READY,
        MESH_LOADED,
        PREPROCESSING,
        DETECTING_HOLES,
        FILLING_HOLES,
        SAVING,
        ERROR
    };

    // Main engine wrapper class
    // Manages mesh state and operations for IPC communication
    class EngineWrapper {
    public:
        EngineWrapper();
        ~EngineWrapper();

        // Initialization
        void initialize(const nlohmann::json& config);
        bool is_initialized() const { return state_ != EngineState::UNINITIALIZED; }
        EngineState get_state() const { return state_; }

        // Callbacks
        void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
        void set_log_callback(LogCallback callback) { log_callback_ = callback; }
        void set_cancel_check_callback(CancelCheckCallback callback) { cancel_check_callback_ = callback; }

        // Mesh operations
        void load_mesh(const std::string& file_path, bool force_cgal_loader = false);
        void load_mesh_from_data(const std::vector<std::array<double, 3>>& vertices,
                                 const std::vector<std::array<int, 3>>& faces);
        void preprocess_mesh(const PreprocessingOptions& options);
        void detect_holes(const FillingOptions& options);
        void fill_holes(const FillingOptions& options, bool use_partitioned = true);
        void save_mesh(const std::string& file_path, bool binary_ply = true);
        nlohmann::json save_mesh_to_data();

        // Query state
        bool has_mesh() const { return mesh_.has_value() || soup_.has_value(); }
        bool has_holes_detected() const { return holes_detected_; }

        // Direct mesh access (for binary serialization)
        const Mesh& get_mesh() const;
        void set_mesh(Mesh&& mesh);

        // Get mesh statistics (for responses)
        nlohmann::json get_mesh_info() const;
        nlohmann::json get_preprocessing_stats() const;
        nlohmann::json get_hole_detection_stats() const;
        nlohmann::json get_hole_filling_stats() const;

        // Clear/reset
        void clear_mesh();
        void reset();

    private:
        // State
        EngineState state_;
        std::optional<PolygonSoup> soup_;  // Soup-based workflow (optimized)
        std::optional<Mesh> mesh_;         // Mesh created after preprocessing or on-demand
        bool holes_detected_;

        // Statistics
        PreprocessingStats preprocess_stats_;
        MeshStatistics hole_stats_;

        // Threading
        std::unique_ptr<ThreadManager> thread_manager_;
        ThreadingConfig thread_config_;

        // Callbacks
        ProgressCallback progress_callback_;
        LogCallback log_callback_;
        CancelCheckCallback cancel_check_callback_;

        // Debug mode
        bool debug_mode_;

        // File logging
        std::ofstream log_file_;
        std::string log_file_path_;

        // Helper methods
        void log(const std::string& level, const std::string& message);
        void report_progress(double progress, const std::string& status);
        bool should_cancel();
        void ensure_state(EngineState expected_state, const std::string& operation);
        void ensure_mesh_exists();  // Convert soup→mesh if needed (for operations requiring mesh)
        void dump_debug_mesh(const std::string& prefix, const std::string& description);
    };

}  // namespace Engine
}  // namespace MeshRepair
