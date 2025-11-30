#include "engine_dispatch.h"
#include "mesh_binary.h"
#include <unordered_map>

namespace MeshRepair {
namespace Engine {

    namespace {
        using DispatchFn = nlohmann::json (*)(EngineWrapper&, const nlohmann::json&, bool, bool, bool);

        nlohmann::json dispatch_init(EngineWrapper& engine, const nlohmann::json& params, bool verbose, bool show_stats,
                                     bool socket_mode)
        {
            (void)verbose;
            (void)show_stats;
            (void)socket_mode;
            engine.initialize(params);
            nlohmann::json resp   = create_success_response("Engine initialized");
            resp["version"]       = Config::VERSION;
            resp["version_major"] = Config::VERSION_MAJOR;
            resp["version_minor"] = Config::VERSION_MINOR;
            resp["version_patch"] = Config::VERSION_PATCH;
            resp["build_date"]    = Config::BUILD_DATE;
            resp["build_time"]    = Config::BUILD_TIME;
            return resp;
        }

        nlohmann::json dispatch_load_mesh(EngineWrapper& engine, const nlohmann::json& params, bool verbose,
                                          bool show_stats, bool socket_mode)
        {
            (void)verbose;
            (void)show_stats;
            (void)socket_mode;
            std::string file_path      = params.value("file_path", std::string {});
            bool force_cgal            = params.value("force_cgal", false);
            std::string mesh_b64       = params.value("mesh_data_binary", std::string {});
            nlohmann::json mesh_json   = params.value("mesh_data", nlohmann::json::object());
            const auto vertex_count_in = params.value("vertex_count", 0);
            const auto face_count_in   = params.value("face_count", 0);
            const bool has_counts      = vertex_count_in > 0 && face_count_in > 0;

            try {
                if (!mesh_b64.empty()) {
                    if (mesh_b64.size() % 4 != 0) {
                        mesh_b64.append(4 - (mesh_b64.size() % 4), '=');
                    }
                    auto binary = base64_decode(mesh_b64);
                    if (has_counts) {
                        const uint32_t expected_bytes = 4 + static_cast<uint32_t>(vertex_count_in) * 12 + 4
                                                        + static_cast<uint32_t>(face_count_in) * 12;
                        if (binary.size() < expected_bytes) {
                            return create_error_response("Binary mesh data too small: got "
                                                             + std::to_string(binary.size()) + " expected "
                                                             + std::to_string(expected_bytes),
                                                         "invalid_mesh");
                        }
                    }
                    PolygonSoup soup = deserialize_mesh_binary_to_soup(binary, static_cast<uint32_t>(vertex_count_in),
                                                                       static_cast<uint32_t>(face_count_in));
                    engine.set_soup(std::move(soup));
                } else if (!mesh_json.empty() && mesh_json.contains("vertices") && mesh_json.contains("faces")) {
                    const auto& verts_json = mesh_json["vertices"];
                    const auto& faces_json = mesh_json["faces"];
                    std::vector<std::array<double, 3>> vertices;
                    std::vector<std::array<int, 3>> faces;
                    vertices.reserve(vertex_count_in > 0 ? static_cast<size_t>(vertex_count_in) : verts_json.size());
                    faces.reserve(face_count_in > 0 ? static_cast<size_t>(face_count_in) : faces_json.size());

                    for (const auto& v : verts_json) {
                        if (!v.is_array() || v.size() < 3)
                            continue;
                        vertices.push_back({ v[0].get<double>(), v[1].get<double>(), v[2].get<double>() });
                    }
                    for (const auto& f : faces_json) {
                        if (!f.is_array() || f.size() < 3)
                            continue;
                        faces.push_back({ f[0].get<int>(), f[1].get<int>(), f[2].get<int>() });
                    }
                    engine.load_mesh_from_data(vertices, faces);
                } else if (!file_path.empty()) {
                    engine.load_mesh(file_path, force_cgal);
                } else {
                    return create_error_response("Missing file_path or mesh data", "invalid_params");
                }
            } catch (const std::exception& ex) {
                return create_error_response(ex.what(), "exception");
            }

            // Optional selection metadata
            if (params.contains("boundary_vertex_indices")) {
                std::vector<uint32_t> boundary_indices;
                boundary_indices.reserve(params["boundary_vertex_indices"].size());
                for (const auto& idx : params["boundary_vertex_indices"]) {
                    boundary_indices.push_back(idx.get<uint32_t>());
                }
                engine.set_boundary_vertex_indices(boundary_indices);
            }

            if (params.contains("reference_bbox_diagonal")) {
                engine.set_reference_bbox_diagonal(params.value("reference_bbox_diagonal", 0.0));
            }

            nlohmann::json resp = create_success_response("Mesh loaded");
            resp["mesh_info"]   = engine.get_mesh_info();
            return resp;
        }

        nlohmann::json dispatch_preprocess(EngineWrapper& engine, const nlohmann::json& params, bool verbose,
                                           bool show_stats, bool socket_mode)
        {
            (void)verbose;
            (void)show_stats;
            (void)socket_mode;
            PreprocessingOptions options {};
            options.remove_duplicates      = params.value("remove_duplicates", options.remove_duplicates);
            options.remove_non_manifold    = params.value("remove_non_manifold", options.remove_non_manifold);
            options.remove_3_face_fans     = params.value("remove_3_face_fans", options.remove_3_face_fans);
            options.remove_isolated        = params.value("remove_isolated", options.remove_isolated);
            // IPC: default to false unless explicitly true to avoid accidental pruning when addon omits the field
            options.keep_largest_component = params.value("keep_largest_component", false);
            options.non_manifold_passes    = params.value("non_manifold_passes", options.non_manifold_passes);
            options.verbose                = params.value("verbose", options.verbose);
            options.debug                  = params.value("debug", options.debug);
            engine.preprocess_mesh(options);
            nlohmann::json resp = create_success_response("Preprocessing complete");
            resp["stats"]       = engine.get_preprocessing_stats();
            resp["mesh_info"]   = engine.get_mesh_info();
            return resp;
        }

        nlohmann::json dispatch_detect(EngineWrapper& engine, const nlohmann::json& params, bool verbose,
                                       bool show_stats, bool socket_mode)
        {
            (void)show_stats;
            (void)socket_mode;
            FillingOptions options {};
            options.fairing_continuity           = params.value("continuity", options.fairing_continuity);
            options.max_hole_boundary_vertices   = params.value("max_boundary", options.max_hole_boundary_vertices);
            options.min_partition_boundary_edges = params.value("min_partition_edges",
                                                                options.min_partition_boundary_edges);
            options.max_hole_diameter_ratio      = params.value("max_diameter", options.max_hole_diameter_ratio);
            options.use_2d_cdt                   = params.value("use_2d_cdt", options.use_2d_cdt);
            options.use_3d_delaunay              = params.value("use_3d_delaunay", options.use_3d_delaunay);
            options.skip_cubic_search            = params.value("skip_cubic", options.skip_cubic_search);
            options.refine                       = params.value("refine", options.refine);
            options.guard_selection_boundary     = params.value("guard_selection_boundary",
                                                                options.guard_selection_boundary);
            options.keep_largest_component       = params.value("keep_largest_component",
                                                                options.keep_largest_component);
            options.holes_only                   = params.value("holes_only", options.holes_only);
            options.verbose                      = verbose;
            engine.detect_holes(options);
            nlohmann::json resp = create_success_response("Hole detection complete");
            resp["stats"]       = engine.get_hole_detection_stats();
            resp["mesh_info"]   = engine.get_mesh_info();
            return resp;
        }

        nlohmann::json dispatch_fill(EngineWrapper& engine, const nlohmann::json& params, bool verbose, bool show_stats,
                                     bool socket_mode)
        {
            (void)show_stats;
            (void)socket_mode;
            FillingOptions options {};
            options.fairing_continuity           = params.value("continuity", options.fairing_continuity);
            options.max_hole_boundary_vertices   = params.value("max_boundary", options.max_hole_boundary_vertices);
            options.min_partition_boundary_edges = params.value("min_partition_edges",
                                                                options.min_partition_boundary_edges);
            options.max_hole_diameter_ratio      = params.value("max_diameter", options.max_hole_diameter_ratio);
            options.use_2d_cdt                   = params.value("use_2d_cdt", options.use_2d_cdt);
            options.use_3d_delaunay              = params.value("use_3d_delaunay", options.use_3d_delaunay);
            options.skip_cubic_search            = params.value("skip_cubic", options.skip_cubic_search);
            options.refine                       = params.value("refine", options.refine);
            options.guard_selection_boundary     = params.value("guard_selection_boundary",
                                                                options.guard_selection_boundary);
            options.keep_largest_component       = params.value("keep_largest_component",
                                                                options.keep_largest_component);
            options.holes_only                   = params.value("holes_only", options.holes_only);
            options.verbose                      = verbose;
            bool use_partitioned                 = params.value("use_partitioned", true);
            engine.fill_holes(options, use_partitioned);
            nlohmann::json resp = create_success_response("Hole filling complete");
            resp["stats"]       = engine.get_hole_filling_stats();
            resp["mesh_info"]   = engine.get_mesh_info();
            return resp;
        }

        nlohmann::json dispatch_save(EngineWrapper& engine, const nlohmann::json& params, bool verbose, bool show_stats,
                                     bool socket_mode)
        {
            (void)verbose;
            (void)show_stats;
            (void)socket_mode;
            std::string file_path = params.value("file_path", std::string {});
            bool binary_ply       = params.value("binary_ply", true);

            const bool return_binary = params.value("return_binary", false);

            if (return_binary) {
                try {
                    const Mesh& mesh         = engine.get_mesh();
                    auto binary              = serialize_mesh_binary(mesh);
                    nlohmann::json resp      = create_success_response("Mesh saved (binary)");
                    resp["mesh_data_binary"] = base64_encode(binary);
                    resp["data"] = { { "save_time_ms", 0.0 }, { "serialize_time_ms", 0.0 }, { "encode_time_ms", 0.0 } };
                    resp["mesh_info"] = engine.get_mesh_info();
                    return resp;
                } catch (const std::exception& ex) {
                    return create_error_response(ex.what(), "exception");
                }
            }

            if (file_path.empty()) {
                return create_error_response("Missing file_path", "invalid_params");
            }
            engine.save_mesh(file_path, binary_ply);
            nlohmann::json resp = create_success_response("Mesh saved");
            resp["mesh_info"]   = engine.get_mesh_info();
            return resp;
        }

        nlohmann::json dispatch_info(EngineWrapper& engine, const nlohmann::json& params, bool verbose, bool show_stats,
                                     bool socket_mode)
        {
            (void)params;
            (void)verbose;
            (void)show_stats;
            (void)socket_mode;
            nlohmann::json resp = create_success_response();
            resp["version"]       = Config::VERSION;
            resp["version_major"] = Config::VERSION_MAJOR;
            resp["version_minor"] = Config::VERSION_MINOR;
            resp["version_patch"] = Config::VERSION_PATCH;
            resp["build_date"]    = Config::BUILD_DATE;
            resp["build_time"]    = Config::BUILD_TIME;
            resp["state"]         = static_cast<int>(engine.get_state());
            resp["has_mesh"]      = engine.has_mesh();
            if (engine.has_mesh()) {
                resp["mesh_info"] = engine.get_mesh_info();
            }
            resp["preprocess_stats"] = engine.get_preprocessing_stats();
            resp["hole_stats"]       = engine.get_hole_filling_stats();
            return resp;
        }

        nlohmann::json dispatch_shutdown(EngineWrapper& /*engine*/, const nlohmann::json& /*params*/, bool /*verbose*/,
                                         bool /*show_stats*/, bool /*socket_mode*/)
        {
            return create_success_response("Shutdown");
        }

    }  // namespace

    nlohmann::json dispatch_command_procedural(EngineWrapper& engine, const nlohmann::json& cmd, bool verbose,
                                               bool show_stats, bool socket_mode)
    {
        static const std::unordered_map<std::string, DispatchFn> table = {
            { "init", dispatch_init },
            { "load_mesh", dispatch_load_mesh },
            { "preprocess", dispatch_preprocess },
            { "detect_holes", dispatch_detect },
            { "fill_holes", dispatch_fill },
            { "save_mesh", dispatch_save },
            { "get_info", dispatch_info },
            { "shutdown", dispatch_shutdown },
        };

        if (!cmd.is_object() || !cmd.contains("command") || !cmd["command"].is_string()) {
            return create_error_response("Invalid command: missing 'command' string", "invalid_command");
        }

        const std::string command = cmd["command"].get<std::string>();
        auto it                   = table.find(command);
        if (it == table.end()) {
            return create_error_response("Unknown command: " + command, "unknown_command");
        }

        nlohmann::json params = cmd.value("params", nlohmann::json::object());
        try {
            return it->second(engine, params, verbose, show_stats, socket_mode);
        } catch (const std::exception& ex) {
            return create_error_response(ex.what(), "exception");
        } catch (...) {
            return create_error_response("Unknown exception in dispatcher", "exception");
        }
    }

}  // namespace Engine
}  // namespace MeshRepair
