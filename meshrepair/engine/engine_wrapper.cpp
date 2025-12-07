#include "engine_wrapper.h"
#include "../include/config.h"
#include "../include/debug_path.h"
#include "../include/c_api.h"
#include <CGAL/IO/PLY.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/boost/graph/helpers.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <exception>

namespace MeshRepair {
namespace Engine {

    namespace PMP = CGAL::Polygon_mesh_processing;

    EngineWrapper::EngineWrapper()
        : state_(EngineState::UNINITIALIZED)
        , holes_detected_(false)
        , progress_callback_(nullptr)
        , log_callback_(nullptr)
        , cancel_check_callback_(nullptr)
        , debug_mode_(false)
        , reference_bbox_diagonal_(0.0)
    {
        has_soup_ = false;
        has_mesh_ = false;
    }

    EngineWrapper::~EngineWrapper()
    {
        if (log_file_.is_open()) {
            log_file_ << "=== Engine shutdown ===" << std::endl;
            log_file_.close();
        }
    }

    void EngineWrapper::initialize(const nlohmann::json& config)
    {
        if (config.contains("log_file_path")) {
            log_file_path_ = config.value("log_file_path", std::string {});
            log_file_.open(log_file_path_, std::ios::out | std::ios::trunc);
            if (log_file_.is_open()) {
                log_file_ << "=== MeshRepair Engine Log ===" << std::endl;
                log_file_ << "Engine version: " << Config::VERSION << std::endl;
                log_file_ << "Log started at: " << std::time(nullptr) << std::endl;
                log_file_ << "=============================\n" << std::endl;
                log_file_.flush();
            }
        }

        log("info", "Initializing engine v" + std::string(Config::VERSION));

        thread_config_.num_threads = config.value("threads", 0u);  // 0 = auto
        thread_config_.queue_size  = config.value("queue_size", 10u);
        thread_config_.verbose     = config.value("verbose", false);
        debug_mode_                = config.value("debug", false);

        if (config.contains("temp_dir")) {
            std::string temp_dir = config.value("temp_dir", std::string {});
            MeshRepair::DebugPath::set_base_directory(temp_dir);
            if (!temp_dir.empty()) {
                log("info", "Debug output directory: " + MeshRepair::DebugPath::get_base_directory());
            }
        }

        thread_manager_ = std::make_unique<ThreadManager>();
        thread_manager_init(*thread_manager_, thread_config_);

        state_ = EngineState::READY;
        log("info", "Engine initialized with " + std::to_string(thread_manager_->config.num_threads) + " thread(s)");

        if (debug_mode_) {
            log("info", "Debug mode enabled - intermediate meshes will be dumped as PLY files");
        }

        if (!log_file_path_.empty()) {
            log("info", "File logging enabled: " + log_file_path_);
        }
    }

    void EngineWrapper::load_mesh(const std::string& file_path, bool force_cgal_loader)
    {
        ensure_state(EngineState::READY, "load_mesh");
        state_ = EngineState::MESH_LOADED;

        log("info", "Loading mesh from: " + file_path);
        report_progress(0.0, "Loading mesh");

        PolygonSoup new_soup;
        int status = mesh_loader_load_soup(file_path.c_str(), MeshLoader::Format::AUTO, force_cgal_loader, &new_soup);
        if (status != 0) {
            state_ = EngineState::ERROR;
            throw std::runtime_error("Failed to load mesh: " + std::string(mesh_loader_last_error()));
        }

        soup_storage_     = std::move(new_soup);
        has_soup_         = true;
        has_mesh_         = false;
        holes_detected_   = false;
        preprocess_stats_ = PreprocessingStats();
        hole_stats_       = MeshStatistics();

        log("info", "Mesh loaded successfully as polygon soup");
        log("info", "  Points: " + std::to_string(soup_storage_.points.size()));
        log("info", "  Polygons: " + std::to_string(soup_storage_.polygons.size()));
        log("info", "  Load time: " + std::to_string(soup_storage_.load_time_ms) + " ms");

        if (debug_mode_) {
            Mesh debug_mesh;
            PMP::polygon_soup_to_polygon_mesh(soup_storage_.points, soup_storage_.polygons, debug_mesh);
            std::string filename = DebugPath::step_file("original_loaded");
            if (CGAL::IO::write_PLY(filename, debug_mesh, CGAL::parameters::use_binary_mode(true))) {
                log("info", "[DEBUG] Saved original soup: " + filename);
            }
        }

        report_progress(1.0, "Mesh loaded");
    }

    void EngineWrapper::load_mesh_from_data(const std::vector<std::array<double, 3>>& vertices,
                                            const std::vector<std::array<int, 3>>& faces)
    {
        ensure_state(EngineState::READY, "load_mesh_from_data");
        state_ = EngineState::MESH_LOADED;

        log("info", "Loading mesh from data (polygon soup)");
        report_progress(0.0, "Loading mesh");

        try {
            PolygonSoup new_soup;
            new_soup.points.reserve(vertices.size());
            new_soup.polygons.reserve(faces.size());

            for (const auto& v : vertices) {
                new_soup.points.emplace_back(v[0], v[1], v[2]);
            }

            for (const auto& f : faces) {
                if (f[0] < 0 || f[0] >= static_cast<int>(vertices.size()) || f[1] < 0
                    || f[1] >= static_cast<int>(vertices.size()) || f[2] < 0
                    || f[2] >= static_cast<int>(vertices.size())) {
                    state_ = EngineState::ERROR;
                    throw std::runtime_error("Invalid face index in mesh data");
                }

                std::vector<std::size_t> polygon = {
                    static_cast<std::size_t>(f[0]),
                    static_cast<std::size_t>(f[1]),
                    static_cast<std::size_t>(f[2]),
                };
                new_soup.polygons.push_back(std::move(polygon));
            }

            new_soup.load_time_ms = 0.0;

            soup_storage_     = std::move(new_soup);
            has_soup_         = true;
            has_mesh_         = false;
            holes_detected_   = false;
            preprocess_stats_ = PreprocessingStats();
            hole_stats_       = MeshStatistics();
            boundary_vertex_indices_.clear();
            boundary_vertex_positions_.clear();

            if (debug_mode_) {
                Mesh debug_mesh;
                PMP::polygon_soup_to_polygon_mesh(soup_storage_.points, soup_storage_.polygons, debug_mesh);
                std::string filename = DebugPath::step_file("original_loaded");
                CGAL::IO::write_PLY(filename, debug_mesh, CGAL::parameters::use_binary_mode(true));
            }

            log("info", "Mesh loaded successfully from data as polygon soup");
            log("info", "  Points: " + std::to_string(soup_storage_.points.size()));
            log("info", "  Polygons: " + std::to_string(soup_storage_.polygons.size()));

            // (already dumped above in debug mode)

            report_progress(1.0, "Mesh loaded");
        } catch (const std::exception& ex) {
            state_ = EngineState::ERROR;
            throw std::runtime_error(std::string("Failed to load mesh from data: ") + ex.what());
        }
    }

    void EngineWrapper::preprocess_mesh(const PreprocessingOptions& options)
    {
        ensure_state(EngineState::MESH_LOADED, "preprocess_mesh");
        state_ = EngineState::PREPROCESSING;

        log("info", "Starting mesh preprocessing (soup-based)");
        report_progress(0.0, "Preprocessing");

        PreprocessingOptions opts = options;
        if (debug_mode_) {
            opts.debug   = true;
            opts.verbose = true;
        }

        if (has_soup_) {
            Mesh output_mesh;
            preprocess_stats_ = MeshPreprocessor::preprocess_soup(soup_storage_, output_mesh, opts);
            mesh_storage_     = std::move(output_mesh);
            has_mesh_         = true;
            has_soup_         = false;
        } else if (has_mesh_) {
            // Convert mesh back to soup, then run soup-based preprocessing
            PolygonSoup soup;
            soup.points.reserve(mesh_storage_.number_of_vertices());
            soup.polygons.reserve(mesh_storage_.number_of_faces());

            std::map<vertex_descriptor, std::size_t> vmap;
            std::size_t idx = 0;
            for (auto v : mesh_storage_.vertices()) {
                vmap[v] = idx++;
                soup.points.push_back(mesh_storage_.point(v));
            }

            for (auto f : mesh_storage_.faces()) {
                std::vector<std::size_t> poly;
                for (auto v : CGAL::vertices_around_face(CGAL::halfedge(f, mesh_storage_), mesh_storage_)) {
                    poly.push_back(vmap[v]);
                }
                soup.polygons.push_back(std::move(poly));
            }
            soup.load_time_ms = 0.0;

            Mesh output_mesh;
            preprocess_stats_ = MeshPreprocessor::preprocess_soup(soup, output_mesh, opts);
            mesh_storage_     = std::move(output_mesh);
            has_mesh_         = true;
            has_soup_         = false;
        } else {
            state_ = EngineState::ERROR;
            throw std::runtime_error("No mesh or soup loaded for preprocessing");
        }

        log("info", "Preprocessing complete");
        log("info", "  Duplicates merged: " + std::to_string(preprocess_stats_.duplicates_merged));
        log("info", "  Non-manifold removed: " + std::to_string(preprocess_stats_.non_manifold_vertices_removed));
        log("info", "  3-face fans collapsed: " + std::to_string(preprocess_stats_.face_fans_collapsed));
        log("info", "  Isolated removed: " + std::to_string(preprocess_stats_.isolated_vertices_removed));
        log("info", "  Small components removed: " + std::to_string(preprocess_stats_.small_components_removed));
        log("info", "  Timing - Soup: " + std::to_string(preprocess_stats_.soup_cleanup_time_ms) + " ms, "
                        + "Conversion: " + std::to_string(preprocess_stats_.soup_to_mesh_time_ms) + " ms, "
                        + "Mesh: " + std::to_string(preprocess_stats_.mesh_cleanup_time_ms) + " ms");

        remap_boundary_indices_after_preprocess();

        dump_debug_mesh("after_preprocessing", "After preprocessing complete");

        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Preprocessing complete");
    }

    void EngineWrapper::detect_holes(const FillingOptions& options)
    {
        ensure_state(EngineState::MESH_LOADED, "detect_holes");
        ensure_mesh_exists();
        state_ = EngineState::DETECTING_HOLES;

        log("info", "Detecting holes");
        report_progress(0.0, "Detecting holes");

        FillingOptions detect_options = options;
        if (debug_mode_) {
            detect_options.verbose = true;
        }

        HoleDetectorCtx detect_ctx { &mesh_storage_, detect_options.verbose };
        std::vector<HoleInfo> holes;
        detect_all_holes_ctx(detect_ctx, holes);

        size_t filtered_count = 0;
        for (const auto& hole : holes) {
            if (hole.boundary_size <= detect_options.max_hole_boundary_vertices) {
                filtered_count++;
            }
        }

        hole_stats_                    = MeshStatistics();
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
        ensure_mesh_exists();
        state_ = EngineState::FILLING_HOLES;

        log("info", "Filling holes (mode: " + std::string(use_partitioned ? "partitioned" : "legacy") + ")");
        report_progress(0.0, "Filling holes");

        FillingOptions fill_options = options;
        if (debug_mode_) {
            fill_options.verbose = true;
        }
        if (fill_options.holes_only && !use_partitioned) {
            log("info", "holes_only is supported only in partitioned mode; ignoring for legacy pipeline");
            fill_options.holes_only = false;
        }
        if (fill_options.verbose) {
            std::ostringstream oss;
            oss << "Fill options: continuity=" << fill_options.fairing_continuity
                << " refine=" << (fill_options.refine ? "true" : "false")
                << " use_2d_cdt=" << (fill_options.use_2d_cdt ? "true" : "false")
                << " use_3d_delaunay=" << (fill_options.use_3d_delaunay ? "true" : "false")
                << " skip_cubic=" << (fill_options.skip_cubic_search ? "true" : "false")
                << " max_boundary=" << fill_options.max_hole_boundary_vertices
                << " max_diam_ratio=" << fill_options.max_hole_diameter_ratio
                << " selection_boundary_sz=" << fill_options.selection_boundary_vertices.size()
                << " guard_selection_boundary=" << (fill_options.guard_selection_boundary ? "true" : "false")
                << " holes_only=" << (fill_options.holes_only ? "true" : "false");
            if (fill_options.reference_bbox_diagonal > 0.0) {
                oss << " ref_bbox_diag=" << fill_options.reference_bbox_diagonal;
            }
            log("info", oss.str());
        }

        if (!boundary_vertex_indices_.empty()) {
            fill_options.selection_boundary_vertices = std::set<uint32_t>(boundary_vertex_indices_.begin(),
                                                                          boundary_vertex_indices_.end());
            if (fill_options.guard_selection_boundary) {
                log("info", "  Selection guard enabled: " + std::to_string(boundary_vertex_indices_.size())
                                + " boundary vertices will be used to protect selection border");
            } else {
                log("info", "  Selection guard disabled: boundary vertex data provided ("
                                + std::to_string(boundary_vertex_indices_.size())
                                + " vertices) but will not skip border holes");
            }
        }

        if (reference_bbox_diagonal_ > 0.0) {
            fill_options.reference_bbox_diagonal = reference_bbox_diagonal_;
            log("info", "  Using reference bbox diagonal: " + std::to_string(reference_bbox_diagonal_));
        }

        hole_stats_                   = MeshStatistics();
        hole_stats_.original_vertices = mesh_storage_.number_of_vertices();
        hole_stats_.original_faces    = mesh_storage_.number_of_faces();

        if (!thread_manager_) {
            state_ = EngineState::ERROR;
            throw std::runtime_error("Thread manager not initialized");
        }

        if (use_partitioned) {
            ParallelPipelineCtx ctx;
            ctx.mesh       = &mesh_storage_;
            ctx.thread_mgr = thread_manager_.get();
            ctx.options    = fill_options;
            hole_stats_    = parallel_fill_partitioned(&ctx, fill_options.verbose, debug_mode_);
        } else {
            PipelineContext pipe_ctx;
            pipe_ctx.mesh       = &mesh_storage_;
            pipe_ctx.thread_mgr = thread_manager_.get();
            pipe_ctx.options    = fill_options;

            if (thread_manager_->config.num_threads > 1) {
                hole_stats_ = pipeline_process_pipeline(&pipe_ctx, fill_options.verbose);
            } else {
                hole_stats_ = pipeline_process_batch(&pipe_ctx, fill_options.verbose);
            }
        }

        hole_stats_.final_vertices = mesh_storage_.number_of_vertices();
        hole_stats_.final_faces    = mesh_storage_.number_of_faces();

        log("info", "Hole filling complete");
        log("info", "  Holes filled: " + std::to_string(hole_stats_.num_holes_filled));
        log("info", "  Holes failed: " + std::to_string(hole_stats_.num_holes_failed));
        log("info", "  Holes skipped: " + std::to_string(hole_stats_.num_holes_skipped));
        log("info", "  Vertices added: " + std::to_string(mesh_stats_total_vertices_added(hole_stats_)));
        log("info", "  Faces added: " + std::to_string(mesh_stats_total_faces_added(hole_stats_)));
        log("info", "  Time: " + std::to_string(hole_stats_.total_time_ms) + " ms");

        dump_debug_mesh("after_hole_filling", "After hole filling complete");

        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Hole filling complete");
    }

    void EngineWrapper::save_mesh(const std::string& file_path, bool binary_ply)
    {
        ensure_state(EngineState::MESH_LOADED, "save_mesh");
        ensure_mesh_exists();
        state_ = EngineState::SAVING;

        log("info", "Saving mesh to: " + file_path);
        report_progress(0.0, "Saving mesh");

        int status = mesh_loader_save(mesh_storage_, file_path.c_str(), MeshLoader::Format::AUTO, binary_ply);
        if (status != 0) {
            state_ = EngineState::ERROR;
            throw std::runtime_error("Failed to save mesh: " + std::string(mesh_loader_last_error()));
        }

        log("info", "Mesh saved successfully");
        state_ = EngineState::MESH_LOADED;
        report_progress(1.0, "Mesh saved");
    }

    nlohmann::json EngineWrapper::save_mesh_to_data()
    {
        ensure_state(EngineState::MESH_LOADED, "save_mesh_to_data");
        ensure_mesh_exists();
        state_ = EngineState::SAVING;

        log("info", "Extracting mesh data");
        report_progress(0.0, "Extracting mesh");

        try {
            nlohmann::json mesh_data;

            std::map<Mesh::Vertex_index, int> vertex_map;
            int vertex_idx = 0;

            nlohmann::json vertices = nlohmann::json::array();
            for (auto v : mesh_storage_.vertices()) {
                const Point_3& p = mesh_storage_.point(v);
                vertices.push_back({ p.x(), p.y(), p.z() });
                vertex_map[v] = vertex_idx++;
            }

            nlohmann::json faces = nlohmann::json::array();
            for (auto f : mesh_storage_.faces()) {
                std::vector<int> face_indices;
                for (auto v : vertices_around_face(mesh_storage_.halfedge(f), mesh_storage_)) {
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
        } catch (const std::exception& ex) {
            state_ = EngineState::ERROR;
            throw std::runtime_error(std::string("Failed to extract mesh data: ") + ex.what());
        }
    }

    nlohmann::json EngineWrapper::get_mesh_info() const
    {
        if (has_mesh_) {
            nlohmann::json info;
            info["vertices"] = mesh_storage_.number_of_vertices();
            info["faces"]    = mesh_storage_.number_of_faces();
            info["edges"]    = mesh_storage_.number_of_edges();
            return info;
        }

        if (has_soup_) {
            nlohmann::json info;
            info["points"]   = soup_storage_.points.size();
            info["polygons"] = soup_storage_.polygons.size();
            info["is_soup"]  = true;
            return info;
        }

        return nlohmann::json::object();
    }

    nlohmann::json EngineWrapper::get_preprocessing_stats() const
    {
        nlohmann::json stats;
        stats["duplicates_merged"]             = preprocess_stats_.duplicates_merged;
        stats["non_manifold_vertices_removed"] = preprocess_stats_.non_manifold_vertices_removed;
        stats["long_edge_polygons_removed"]    = preprocess_stats_.long_edge_polygons_removed;
        stats["face_fans_collapsed"]           = preprocess_stats_.face_fans_collapsed;
        stats["isolated_vertices_removed"]     = preprocess_stats_.isolated_vertices_removed;
        stats["small_components_removed"]      = preprocess_stats_.small_components_removed;
        stats["connected_components_found"]    = preprocess_stats_.connected_components_found;
        stats["total_time_ms"]                 = preprocess_stats_.total_time_ms;
        stats["soup_cleanup_time_ms"]          = preprocess_stats_.soup_cleanup_time_ms;
        stats["long_edge_time_ms"]             = preprocess_stats_.long_edge_time_ms;
        stats["soup_to_mesh_time_ms"]          = preprocess_stats_.soup_to_mesh_time_ms;
        stats["mesh_cleanup_time_ms"]          = preprocess_stats_.mesh_cleanup_time_ms;
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
        stats["num_holes_detected"]   = hole_stats_.num_holes_detected;
        stats["num_holes_filled"]     = hole_stats_.num_holes_filled;
        stats["num_holes_failed"]     = hole_stats_.num_holes_failed;
        stats["num_holes_skipped"]    = hole_stats_.num_holes_skipped;
        stats["original_vertices"]    = hole_stats_.original_vertices;
        stats["original_faces"]       = hole_stats_.original_faces;
        stats["final_vertices"]       = hole_stats_.final_vertices;
        stats["final_faces"]          = hole_stats_.final_faces;
        stats["total_vertices_added"] = mesh_stats_total_vertices_added(hole_stats_);
        stats["total_faces_added"]    = mesh_stats_total_faces_added(hole_stats_);
        stats["total_time_ms"]        = hole_stats_.total_time_ms;
        return stats;
    }

    const Mesh& EngineWrapper::get_mesh() const
    {
        if (!has_mesh_) {
            throw std::runtime_error("No mesh loaded");
        }
        return mesh_storage_;
    }

    void EngineWrapper::set_mesh(Mesh&& mesh)
    {
        mesh_storage_     = std::move(mesh);
        has_mesh_         = true;
        has_soup_         = false;
        holes_detected_   = false;
        preprocess_stats_ = PreprocessingStats();
        hole_stats_       = MeshStatistics();
        state_            = EngineState::MESH_LOADED;

        log("info", "Mesh set successfully (from binary deserialization)");
        log("info", "  Vertices: " + std::to_string(mesh_storage_.number_of_vertices()));
        log("info", "  Faces: " + std::to_string(mesh_storage_.number_of_faces()));
        log("info", "  Edges: " + std::to_string(mesh_storage_.number_of_edges()));
    }

    void EngineWrapper::set_soup(PolygonSoup&& soup)
    {
        ensure_state(EngineState::READY, "set_soup");
        state_ = EngineState::MESH_LOADED;

        soup_storage_     = std::move(soup);
        has_soup_         = true;
        has_mesh_         = false;
        holes_detected_   = false;
        preprocess_stats_ = PreprocessingStats();
        hole_stats_       = MeshStatistics();
        boundary_vertex_indices_.clear();
        boundary_vertex_positions_.clear();

        log("info", "Mesh loaded successfully as polygon soup");
        log("info", "  Points: " + std::to_string(soup_storage_.points.size()));
        log("info", "  Polygons: " + std::to_string(soup_storage_.polygons.size()));

        if (debug_mode_) {
            Mesh debug_mesh;
            PMP::polygon_soup_to_polygon_mesh(soup_storage_.points, soup_storage_.polygons, debug_mesh);
            std::string filename = DebugPath::step_file("original_loaded");
            if (CGAL::IO::write_PLY(filename, debug_mesh, CGAL::parameters::use_binary_mode(true))) {
                log("info", "[DEBUG] Saved original soup: " + filename);
            }
        }
    }

    void EngineWrapper::set_boundary_vertex_indices(const std::vector<uint32_t>& indices)
    {
        boundary_vertex_indices_ = indices;
        capture_boundary_positions();
        if (!indices.empty()) {
            log("info", "Selection boundary vertices set: " + std::to_string(indices.size()) + " vertices marked");
        }
    }

    void EngineWrapper::set_reference_bbox_diagonal(double diagonal)
    {
        reference_bbox_diagonal_ = diagonal;
        if (diagonal > 0) {
            log("info", "Reference bbox diagonal set: " + std::to_string(diagonal));
        }
    }

    void EngineWrapper::clear_mesh()
    {
        has_soup_         = false;
        has_mesh_         = false;
        soup_storage_     = PolygonSoup();
        mesh_storage_     = Mesh();
        holes_detected_   = false;
        preprocess_stats_ = PreprocessingStats();
        hole_stats_       = MeshStatistics();
        boundary_vertex_indices_.clear();
        boundary_vertex_positions_.clear();
        reference_bbox_diagonal_ = 0.0;
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
            log_callback_(level, message, log_user_);
        }

        if (log_file_.is_open()) {
            auto now    = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::tm tm_buf {};
#ifdef _WIN32
            localtime_s(&tm_buf, &time_t);
#else
            localtime_r(&time_t, &tm_buf);
#endif

            char time_buf[64];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

            log_file_ << "[" << time_buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                      << "[" << level << "] " << message << std::endl;
            log_file_.flush();
        }
    }

    void EngineWrapper::report_progress(double progress, const std::string& status)
    {
        if (progress_callback_) {
            progress_callback_(progress, status, progress_user_);
        }
    }

    bool EngineWrapper::should_cancel()
    {
        if (cancel_check_callback_) {
            return cancel_check_callback_(cancel_user_);
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

    void EngineWrapper::ensure_mesh_exists()
    {
        if (has_mesh_) {
            return;
        }

        if (has_soup_) {
            log("info", "Converting polygon soup to mesh (on-demand)");
            Mesh new_mesh;
            PMP::polygon_soup_to_polygon_mesh(soup_storage_.points, soup_storage_.polygons, new_mesh);
            mesh_storage_ = std::move(new_mesh);
            has_mesh_     = true;
            has_soup_     = false;
            log("info", "  Vertices: " + std::to_string(mesh_storage_.number_of_vertices()));
            log("info", "  Faces: " + std::to_string(mesh_storage_.number_of_faces()));
            return;
        }

        throw std::runtime_error("No mesh or soup loaded");
    }

    void EngineWrapper::capture_boundary_positions()
    {
        boundary_vertex_positions_.clear();
        if (boundary_vertex_indices_.empty()) {
            return;
        }

        if (has_soup_) {
            const auto& pts = soup_storage_.points;
            for (auto idx : boundary_vertex_indices_) {
                if (idx < pts.size()) {
                    const auto& p = pts[idx];
                    boundary_vertex_positions_.emplace_back(p[0], p[1], p[2]);
                }
            }
        } else if (has_mesh_) {
            const auto vertex_count = mesh_storage_.number_of_vertices();
            for (auto idx : boundary_vertex_indices_) {
                if (idx < vertex_count) {
                    boundary_vertex_positions_.push_back(mesh_storage_.point(vertex_descriptor(static_cast<int>(idx))));
                }
            }
        }
    }

    namespace {
        struct QuantKey {
            long long x;
            long long y;
            long long z;
            bool operator==(const QuantKey& other) const { return x == other.x && y == other.y && z == other.z; }
        };

        struct QuantHash {
            std::size_t operator()(const QuantKey& k) const
            {
                std::size_t h1 = std::hash<long long> {}(k.x);
                std::size_t h2 = std::hash<long long> {}(k.y);
                std::size_t h3 = std::hash<long long> {}(k.z);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

        inline QuantKey quantize_point(const Point_3& p, double scale)
        {
            return QuantKey { static_cast<long long>(std::llround(CGAL::to_double(p.x()) * scale)),
                              static_cast<long long>(std::llround(CGAL::to_double(p.y()) * scale)),
                              static_cast<long long>(std::llround(CGAL::to_double(p.z()) * scale)) };
        }
    }  // namespace

    void EngineWrapper::remap_boundary_indices_after_preprocess()
    {
        if (boundary_vertex_positions_.empty() || mesh_storage_.is_empty()) {
            return;
        }

        const double scale = 1e6;  // ~1e-6 tolerance
        std::unordered_map<QuantKey, uint32_t, QuantHash> lookup;
        lookup.reserve(static_cast<size_t>(mesh_storage_.number_of_vertices()));

        for (auto v : mesh_storage_.vertices()) {
            auto key = quantize_point(mesh_storage_.point(v), scale);
            lookup.emplace(key, static_cast<uint32_t>(v));
        }

        std::vector<uint32_t> remapped;
        remapped.reserve(boundary_vertex_positions_.size());
        for (const auto& pos : boundary_vertex_positions_) {
            auto key = quantize_point(pos, scale);
            auto it  = lookup.find(key);
            if (it != lookup.end()) {
                remapped.push_back(it->second);
                continue;
            }

            // Fallback: nearest neighbor (linear scan; boundary set is small)
            double best       = std::numeric_limits<double>::max();
            uint32_t best_idx = std::numeric_limits<uint32_t>::max();
            for (auto v : mesh_storage_.vertices()) {
                auto diff   = mesh_storage_.point(v) - pos;
                double dist = CGAL::to_double(diff.squared_length());
                if (dist < best) {
                    best     = dist;
                    best_idx = static_cast<uint32_t>(v);
                }
            }
            if (best_idx != std::numeric_limits<uint32_t>::max()) {
                remapped.push_back(best_idx);
            }
        }

        if (!remapped.empty()) {
            boundary_vertex_indices_ = remapped;
            log("info", "Selection boundary remapped after preprocessing: "
                            + std::to_string(boundary_vertex_indices_.size()) + " vertices");
        }
    }

    void EngineWrapper::dump_debug_mesh(const std::string& prefix, const std::string& description)
    {
        if (!debug_mode_ || !has_mesh_) {
            return;
        }

        try {
            std::string filename = MeshRepair::DebugPath::step_file(prefix);

            if (CGAL::IO::write_PLY(filename, mesh_storage_, CGAL::parameters::use_binary_mode(true))) {
                log("info", "[DEBUG] Saved: " + filename);
                log("info", "[DEBUG]   " + description);
                log("info", "[DEBUG]   Vertices: " + std::to_string(mesh_storage_.number_of_vertices()));
                log("info", "[DEBUG]   Faces: " + std::to_string(mesh_storage_.number_of_faces()));
            } else {
                log("warning", "[DEBUG] Failed to save: " + filename);
            }
        } catch (const std::exception& ex) {
            log("warning", "[DEBUG] Exception while saving debug mesh: " + std::string(ex.what()));
        }
    }

}  // namespace Engine

}  // namespace MeshRepair

// C API bridge (formerly in c_api.cpp)
// Forward declarations of existing entry points
int
cli_main(int argc, char** argv);
int
engine_main(int argc, char** argv);

namespace {

void
reset_status(MRStatus* status)
{
    if (!status) {
        return;
    }
    status->code       = MR_STATUS_OK;
    status->exit_code  = 0;
    status->message[0] = '\0';
}

void
write_error(MRStatus* status, MRStatusCode code, int exit_code, const char* msg)
{
    if (!status) {
        return;
    }
    status->code      = code;
    status->exit_code = exit_code;
    if (msg) {
        std::snprintf(status->message, sizeof(status->message), "%s", msg);
    } else {
        status->message[0] = '\0';
    }
}

}  // namespace

extern "C" const char*
mr_version(void)
{
    return MeshRepair::Config::VERSION;
}

extern "C" int
mr_run_cli(int argc, char** argv, MRStatus* out_status)
{
    reset_status(out_status);

    if (argc < 0 || argv == nullptr) {
        write_error(out_status, MR_STATUS_INVALID_ARGUMENT, -1, "Invalid CLI arguments");
        return -1;
    }

    try {
        int code = cli_main(argc, argv);
        if (out_status) {
            out_status->exit_code = code;
            if (code != 0) {
                write_error(out_status, MR_STATUS_ERROR, code, "CLI returned non-zero exit code");
            }
        }
        return code;
    } catch (const std::exception& ex) {
        write_error(out_status, MR_STATUS_EXCEPTION, -1, ex.what());
        return -1;
    } catch (...) {
        write_error(out_status, MR_STATUS_EXCEPTION, -1, "Unknown exception in CLI");
        return -1;
    }
}

extern "C" int
mr_run_engine(int argc, char** argv, MRStatus* out_status)
{
    reset_status(out_status);

    if (argc < 0 || argv == nullptr) {
        write_error(out_status, MR_STATUS_INVALID_ARGUMENT, -1, "Invalid engine arguments");
        return -1;
    }

    try {
        int code = engine_main(argc, argv);
        if (out_status) {
            out_status->exit_code = code;
            if (code != 0) {
                write_error(out_status, MR_STATUS_ERROR, code, "Engine returned non-zero exit code");
            }
        }
        return code;
    } catch (const std::exception& ex) {
        write_error(out_status, MR_STATUS_EXCEPTION, -1, ex.what());
        return -1;
    } catch (...) {
        write_error(out_status, MR_STATUS_EXCEPTION, -1, "Unknown exception in engine");
        return -1;
    }
}
