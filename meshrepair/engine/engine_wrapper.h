#pragma once

#include "../include/types.h"
#include "../include/mesh_loader.h"
#include "../include/hole_ops.h"
#include "../include/mesh_preprocessor.h"
#include "../include/worker_pool.h"
#include "../include/pipeline_ops.h"

#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <functional>
#include <fstream>

namespace MeshRepair {
namespace Engine {

    // Callback types for progress reporting and logging (C-style function pointers)
    using ProgressCallback    = void (*)(double progress, const std::string& status, void* user);
    using LogCallback         = void (*)(const std::string& level, const std::string& message, void* user);
    using CancelCheckCallback = bool (*)(void* user);  // Returns true if operation should be cancelled

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
        void set_progress_callback(ProgressCallback callback, void* user = nullptr)
        {
            progress_callback_ = callback;
            progress_user_     = user;
        }
        void set_log_callback(LogCallback callback, void* user = nullptr)
        {
            log_callback_ = callback;
            log_user_     = user;
        }
        void set_cancel_check_callback(CancelCheckCallback callback, void* user = nullptr)
        {
            cancel_check_callback_ = callback;
            cancel_user_           = user;
        }

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
        bool has_mesh() const { return has_mesh_ || has_soup_; }
        bool has_holes_detected() const { return holes_detected_; }

        // Direct mesh access (for binary serialization)
        const Mesh& get_mesh() const;
        void set_mesh(Mesh&& mesh);
        void set_soup(PolygonSoup&& soup);

        // Selection boundary info (for edit mode selection support)
        void set_boundary_vertex_indices(const std::vector<uint32_t>& indices);
        void set_reference_bbox_diagonal(double diagonal);
        const std::vector<uint32_t>& get_boundary_vertex_indices() const { return boundary_vertex_indices_; }
        double get_reference_bbox_diagonal() const { return reference_bbox_diagonal_; }
        bool has_selection_boundary() const { return !boundary_vertex_indices_.empty(); }

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
        bool has_soup_ = false;
        bool has_mesh_ = false;
        PolygonSoup soup_storage_;  // Soup-based workflow (optimized)
        Mesh mesh_storage_;         // Mesh created after preprocessing or on-demand
        bool holes_detected_;

        // Statistics
        PreprocessingStats preprocess_stats_;
        MeshStatistics hole_stats_;

        // Threading
        std::unique_ptr<ThreadManager> thread_manager_;
        ThreadingConfig thread_config_;

        // Callbacks
        ProgressCallback progress_callback_;
        void* progress_user_ = nullptr;
        LogCallback log_callback_;
        void* log_user_ = nullptr;
        CancelCheckCallback cancel_check_callback_;
        void* cancel_user_ = nullptr;

        // Debug mode
        bool debug_mode_;

        // File logging
        std::ofstream log_file_;
        std::string log_file_path_;

        // Selection boundary info (for edit mode selection support)
        std::vector<uint32_t> boundary_vertex_indices_;   // Vertex indices on selection boundary
        std::vector<Point_3> boundary_vertex_positions_;  // Positions captured at load time
        double reference_bbox_diagonal_;                  // Full object bbox diagonal (0 = use mesh bbox)

        // Helper methods
        void log(const std::string& level, const std::string& message);
        void report_progress(double progress, const std::string& status);
        bool should_cancel();
        void ensure_state(EngineState expected_state, const std::string& operation);
        void ensure_mesh_exists();  // Convert soup→mesh if needed (for operations requiring mesh)
        void dump_debug_mesh(const std::string& prefix, const std::string& description);
        void capture_boundary_positions();
        void remap_boundary_indices_after_preprocess();
    };

}  // namespace Engine
}  // namespace MeshRepair
