#include "engine_wrapper.h"
#include "../include/config.h"
#include <CGAL/IO/PLY.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace MeshRepair {
namespace Engine {

    EngineWrapper::EngineWrapper()
        : state_(EngineState::UNINITIALIZED)
        , mesh_(std::nullopt)
        , holes_detected_(false)
        , thread_manager_(nullptr)
        , progress_callback_(nullptr)
        , log_callback_(nullptr)
        , cancel_check_callback_(nullptr)
        , debug_mode_(false)
    {
    }

    EngineWrapper::~EngineWrapper()
    {
        // Cleanup
    }

    void EngineWrapper::initialize(const nlohmann::json& config)
    {
        log("info", "Initializing engine v" + std::string(Config::VERSION));

        // Parse configuration
        thread_config_.num_threads = config.value("threads", 0u);  // 0 = auto
        thread_config_.queue_size  = config.value("queue_size", 10u);
        thread_config_.verbose     = config.value("verbose", false);
        debug_mode_                = config.value("debug", false);

        // Create thread manager
        thread_manager_ = std::make_unique<ThreadManager>(thread_config_);

        state_ = EngineState::READY;
        log("info", "Engine initialized with " + std::to_string(thread_manager_->get_total_threads()) + " threads");

        if (debug_mode_) {
            log("info", "Debug mode enabled - intermediate meshes will be dumped as PLY files");
        }
    }

    void EngineWrapper::load_mesh(const std::string& file_path, bool force_cgal_loader)
    {
        ensure_state(EngineState::READY, "load_mesh");
        state_ = EngineState::MESH_LOADED;  // Optimistic state change

        log("info", "Loading mesh from: " + file_path);
        report_progress(0.0, "Loading mesh");

        auto mesh_opt = MeshLoader::load(file_path, MeshLoader::Format::AUTO, force_cgal_loader);
        if (!mesh_opt) {
            state_ = EngineState::ERROR;
            throw std::runtime_error("Failed to load mesh: " + MeshLoader::get_last_error());
        }

        mesh_           = std::move(mesh_opt.value());
        holes_detected_ = false;

        log("info", "Mesh loaded successfully");
        log("info", "  Vertices: " + std::to_string(mesh_->number_of_vertices()));
        log("info", "  Faces: " + std::to_string(mesh_->number_of_faces()));
        log("info", "  Edges: " + std::to_string(mesh_->number_of_edges()));

        // Debug: dump original loaded mesh
        dump_debug_mesh("debug_00_original_loaded", "Original loaded mesh");

        report_progress(1.0, "Mesh loaded");
    }

    void EngineWrapper::load_mesh_from_data(const std::vector<std::array<double, 3>>& vertices,
                                             const std::vector<std::array<int, 3>>& faces)
    {
        ensure_state(EngineState::READY, "load_mesh_from_data");
        state_ = EngineState::MESH_LOADED;  // Optimistic state change

        log("info", "Loading mesh from data");
        report_progress(0.0, "Loading mesh");

        try {
            // Create new mesh
            Mesh new_mesh;

            // Add vertices
            std::vector<Mesh::Vertex_index> vertex_indices;
            vertex_indices.reserve(vertices.size());
            for (const auto& v : vertices) {
                Point_3 p(v[0], v[1], v[2]);
                vertex_indices.push_back(new_mesh.add_vertex(p));
            }

            // Add faces
            for (const auto& f : faces) {
                if (f[0] < 0 || f[0] >= static_cast<int>(vertex_indices.size()) ||
                    f[1] < 0 || f[1] >= static_cast<int>(vertex_indices.size()) ||
                    f[2] < 0 || f[2] >= static_cast<int>(vertex_indices.size())) {
                    state_ = EngineState::ERROR;
                    throw std::runtime_error("Invalid face index in mesh data");
                }

                std::vector<Mesh::Vertex_index> face_verts = {
                    vertex_indices[f[0]],
                    vertex_indices[f[1]],
                    vertex_indices[f[2]]
                };
                new_mesh.add_face(face_verts);
            }

            mesh_           = std::move(new_mesh);
            holes_detected_ = false;

            log("info", "Mesh loaded successfully from data");
            log("info", "  Vertices: " + std::to_string(mesh_->number_of_vertices()));
            log("info", "  Faces: " + std::to_string(mesh_->number_of_faces()));
            log("info", "  Edges: " + std::to_string(mesh_->number_of_edges()));

            // Debug: dump original loaded mesh
            dump_debug_mesh("debug_00_original_loaded", "Original loaded mesh");

            report_progress(1.0, "Mesh loaded");
        }
        catch (const std::exception& ex) {
            state_ = EngineState::ERROR;
            throw std::runtime_error(std::string("Failed to load mesh from data: ") + ex.what());
        }
    }

    void EngineWrapper::preprocess_mesh(const PreprocessingOptions& options)
    {
        ensure_state(EngineState::MESH_LOADED, "preprocess_mesh");
        state_ = EngineState::PREPROCESSING;

        log("info", "Starting mesh preprocessing");
        report_progress(0.0, "Preprocessing");

        MeshPreprocessor preprocessor(*mesh_, options);
        preprocess_stats_ = preprocessor.preprocess();

        log("info", "Preprocessing complete");
        log("info", "  Duplicates merged: " + std::to_string(preprocess_stats_.duplicates_merged));
        log("info", "  Non-manifold removed: " + std::to_string(preprocess_stats_.non_manifold_vertices_removed));
        log("info", "  Isolated removed: " + std::to_string(preprocess_stats_.isolated_vertices_removed));
        log("info", "  Small components removed: " + std::to_string(preprocess_stats_.small_components_removed));

        // Debug: dump preprocessed mesh (after internal preprocessor dumps 01-05)
        dump_debug_mesh("debug_09_after_preprocessing", "After preprocessing complete");

        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Preprocessing complete");
    }

    void EngineWrapper::detect_holes(const FillingOptions& options)
    {
        ensure_state(EngineState::MESH_LOADED, "detect_holes");
        state_ = EngineState::DETECTING_HOLES;

        log("info", "Detecting holes");
        report_progress(0.0, "Detecting holes");

        HoleDetector detector(*mesh_);
        auto holes = detector.detect_all_holes();

        // Filter holes by options
        size_t filtered_count = 0;
        for (const auto& hole : holes) {
            if (hole.boundary_size <= options.max_hole_boundary_vertices) {
                filtered_count++;
            }
        }

        // Store basic stats (full stats come from fill_holes)
        hole_stats_.num_holes_detected = filtered_count;
        holes_detected_                = true;

        log("info", "Hole detection complete");
        log("info", "  Total holes found: " + std::to_string(holes.size()));
        log("info", "  Holes within size limit: " + std::to_string(filtered_count));

        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Hole detection complete");
    }

    void EngineWrapper::fill_holes(const FillingOptions& options, bool use_partitioned)
    {
        ensure_state(EngineState::MESH_LOADED, "fill_holes");
        state_ = EngineState::FILLING_HOLES;

        log("info", "Filling holes (mode: " + std::string(use_partitioned ? "partitioned" : "legacy") + ")");
        report_progress(0.0, "Filling holes");

        // Record original stats
        hole_stats_.original_vertices = mesh_->number_of_vertices();
        hole_stats_.original_faces    = mesh_->number_of_faces();

        if (use_partitioned) {
            // Use partitioned parallel filling (default, faster)
            ParallelHoleFillerPipeline processor(*mesh_, *thread_manager_, options);
            hole_stats_ = processor.process_partitioned(options.verbose, debug_mode_);
        } else {
            // Use legacy pipeline (fallback)
            PipelineProcessor processor(*mesh_, *thread_manager_, options);
            if (thread_manager_->get_total_threads() > 1) {
                hole_stats_ = processor.process_pipeline(options.verbose);
            } else {
                hole_stats_ = processor.process_batch(options.verbose);
            }
        }

        // Record final stats
        hole_stats_.final_vertices = mesh_->number_of_vertices();
        hole_stats_.final_faces    = mesh_->number_of_faces();

        log("info", "Hole filling complete");
        log("info", "  Holes filled: " + std::to_string(hole_stats_.num_holes_filled));
        log("info", "  Holes failed: " + std::to_string(hole_stats_.num_holes_failed));
        log("info", "  Holes skipped: " + std::to_string(hole_stats_.num_holes_skipped));
        log("info", "  Vertices added: " + std::to_string(hole_stats_.total_vertices_added()));
        log("info", "  Faces added: " + std::to_string(hole_stats_.total_faces_added()));
        log("info", "  Time: " + std::to_string(hole_stats_.total_time_ms) + " ms");

        // Debug: dump filled mesh (after internal hole filler dumps 06-08)
        // Note: This is redundant with debug_08_final_merged but kept for consistency
        dump_debug_mesh("debug_10_after_hole_filling", "After hole filling complete");

        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Hole filling complete");
    }

    void EngineWrapper::save_mesh(const std::string& file_path, bool binary_ply)
    {
        ensure_state(EngineState::MESH_LOADED, "save_mesh");
        state_ = EngineState::SAVING;

        log("info", "Saving mesh to: " + file_path);
        report_progress(0.0, "Saving mesh");

        if (!MeshLoader::save(*mesh_, file_path, MeshLoader::Format::AUTO, binary_ply)) {
            state_ = EngineState::ERROR;
            throw std::runtime_error("Failed to save mesh: " + MeshLoader::get_last_error());
        }

        log("info", "Mesh saved successfully");
        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Mesh saved");
    }

    nlohmann::json EngineWrapper::save_mesh_to_data()
    {
        ensure_state(EngineState::MESH_LOADED, "save_mesh_to_data");
        state_ = EngineState::SAVING;

        log("info", "Extracting mesh data");
        report_progress(0.0, "Extracting mesh");

        try {
            nlohmann::json mesh_data;

            // Create vertex index map
            std::map<Mesh::Vertex_index, int> vertex_map;
            int vertex_idx = 0;

            // Extract vertices
            nlohmann::json vertices = nlohmann::json::array();
            for (auto v : mesh_->vertices()) {
                const Point_3& p = mesh_->point(v);
                vertices.push_back({p.x(), p.y(), p.z()});
                vertex_map[v] = vertex_idx++;
            }

            // Extract faces
            nlohmann::json faces = nlohmann::json::array();
            for (auto f : mesh_->faces()) {
                std::vector<int> face_indices;
                for (auto v : vertices_around_face(mesh_->halfedge(f), *mesh_)) {
                    face_indices.push_back(vertex_map[v]);
                }
                faces.push_back(face_indices);
            }

            mesh_data["vertices"] = vertices;
            mesh_data["faces"]    = faces;

            log("info", "Mesh data extracted successfully");
            log("info", "  Vertices: " + std::to_string(vertices.size()));
            log("info", "  Faces: " + std::to_string(faces.size()));

            state_ = EngineState::MESH_LOADED;
            report_progress(1.0, "Mesh data extracted");

            return mesh_data;
        }
        catch (const std::exception& ex) {
            state_ = EngineState::ERROR;
            throw std::runtime_error(std::string("Failed to extract mesh data: ") + ex.what());
        }
    }

    nlohmann::json EngineWrapper::get_mesh_info() const
    {
        if (!mesh_) {
            return nlohmann::json::object();
        }

        nlohmann::json info;
        info["vertices"] = mesh_->number_of_vertices();
        info["faces"]    = mesh_->number_of_faces();
        info["edges"]    = mesh_->number_of_edges();
        return info;
    }

    nlohmann::json EngineWrapper::get_preprocessing_stats() const
    {
        nlohmann::json stats;
        stats["duplicates_merged"]             = preprocess_stats_.duplicates_merged;
        stats["non_manifold_vertices_removed"] = preprocess_stats_.non_manifold_vertices_removed;
        stats["isolated_vertices_removed"]     = preprocess_stats_.isolated_vertices_removed;
        stats["small_components_removed"]      = preprocess_stats_.small_components_removed;
        stats["connected_components_found"]    = preprocess_stats_.connected_components_found;
        stats["total_time_ms"]                 = preprocess_stats_.total_time_ms;
        return stats;
    }

    nlohmann::json EngineWrapper::get_hole_detection_stats() const
    {
        nlohmann::json stats;
        stats["holes_detected"] = hole_stats_.num_holes_detected;
        return stats;
    }

    nlohmann::json EngineWrapper::get_hole_filling_stats() const
    {
        nlohmann::json stats;
        stats["holes_detected"]    = hole_stats_.num_holes_detected;
        stats["holes_filled"]      = hole_stats_.num_holes_filled;
        stats["holes_failed"]      = hole_stats_.num_holes_failed;
        stats["holes_skipped"]     = hole_stats_.num_holes_skipped;
        stats["original_vertices"] = hole_stats_.original_vertices;
        stats["original_faces"]    = hole_stats_.original_faces;
        stats["final_vertices"]    = hole_stats_.final_vertices;
        stats["final_faces"]       = hole_stats_.final_faces;
        stats["vertices_added"]    = hole_stats_.total_vertices_added();
        stats["faces_added"]       = hole_stats_.total_faces_added();
        stats["total_time_ms"]     = hole_stats_.total_time_ms;
        return stats;
    }

    const Mesh& EngineWrapper::get_mesh() const
    {
        if (!mesh_.has_value()) {
            throw std::runtime_error("No mesh loaded");
        }
        return mesh_.value();
    }

    void EngineWrapper::set_mesh(Mesh&& mesh)
    {
        mesh_           = std::move(mesh);
        holes_detected_ = false;
        state_          = EngineState::MESH_LOADED;

        log("info", "Mesh set successfully");
        log("info", "  Vertices: " + std::to_string(mesh_->number_of_vertices()));
        log("info", "  Faces: " + std::to_string(mesh_->number_of_faces()));
        log("info", "  Edges: " + std::to_string(mesh_->number_of_edges()));
    }

    void EngineWrapper::clear_mesh()
    {
        mesh_             = std::nullopt;
        holes_detected_   = false;
        preprocess_stats_ = PreprocessingStats();
        hole_stats_       = MeshStatistics();
        if (state_ != EngineState::UNINITIALIZED) {
            state_ = EngineState::READY;
        }
    }

    void EngineWrapper::reset()
    {
        clear_mesh();
        thread_manager_.reset();
        state_ = EngineState::UNINITIALIZED;
    }

    void EngineWrapper::log(const std::string& level, const std::string& message)
    {
        if (log_callback_) {
            log_callback_(level, message);
        }
    }

    void EngineWrapper::report_progress(double progress, const std::string& status)
    {
        if (progress_callback_) {
            progress_callback_(progress, status);
        }
    }

    bool EngineWrapper::should_cancel()
    {
        if (cancel_check_callback_) {
            return cancel_check_callback_();
        }
        return false;
    }

    void EngineWrapper::ensure_state(EngineState expected_state, const std::string& operation)
    {
        if (state_ != expected_state) {
            std::ostringstream oss;
            oss << "Invalid state for " << operation << ": expected state " << static_cast<int>(expected_state)
                << ", current state " << static_cast<int>(state_);
            throw std::runtime_error(oss.str());
        }
    }

    void EngineWrapper::dump_debug_mesh(const std::string& prefix, const std::string& description)
    {
        if (!debug_mode_ || !mesh_.has_value()) {
            return;
        }

        try {
            std::string filename = prefix + ".ply";

            if (CGAL::IO::write_PLY(filename, mesh_.value(), CGAL::parameters::use_binary_mode(true))) {
                log("info", "[DEBUG] Saved: " + filename);
                log("info", "[DEBUG]   " + description);
                log("info", "[DEBUG]   Vertices: " + std::to_string(mesh_->number_of_vertices()));
                log("info", "[DEBUG]   Faces: " + std::to_string(mesh_->number_of_faces()));
            } else {
                log("warning", "[DEBUG] Failed to save: " + filename);
            }
        } catch (const std::exception& ex) {
            log("warning", "[DEBUG] Exception while saving debug mesh: " + std::string(ex.what()));
        }
    }

}  // namespace Engine
}  // namespace MeshRepair
