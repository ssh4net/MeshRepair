#pragma once

#include "types.h"
#include <string>
#include <optional>

namespace MeshRepair {

/**
 * @brief Mesh I/O handler for OBJ and PLY formats
 *
 * Supports loading and saving mesh files using CGAL's I/O facilities.
 * Automatically detects format from file extension.
 */
class MeshLoader {
public:
    enum class Format {
        OBJ,  // Wavefront OBJ format
        PLY,  // Stanford PLY format
        OFF,  // Object File Format (CGAL native)
        AUTO  // Auto-detect from extension
    };

    /**
     * @brief Load mesh from file
     * @param filename Path to mesh file
     * @param format File format (AUTO for auto-detection)
     * @param force_cgal_loader Force CGAL OBJ loader instead of RapidOBJ (default: false)
     * @return Loaded mesh on success, std::nullopt on failure
     */
    static std::optional<Mesh> load(const std::string& filename, Format format = Format::AUTO,
                                    bool force_cgal_loader = false);

    /**
     * @brief Save mesh to file
     * @param mesh Mesh to save
     * @param filename Output file path
     * @param format File format (AUTO for auto-detection)
     * @param binary_ply Use binary PLY format (ignored for OBJ/OFF)
     * @return true on success, false on failure
     */
    static bool save(const Mesh& mesh, const std::string& filename, Format format = Format::AUTO,
                     bool binary_ply = true);

    /**
     * @brief Validate mesh file exists and is readable
     * @param filename Path to file
     * @return true if file exists and is readable
     */
    static bool validate_input_file(const std::string& filename);

    /**
     * @brief Get human-readable error message for last operation
     * @return Error message string
     */
    static const std::string& get_last_error();

private:
    static Format detect_format(const std::string& filename);
    static std::string last_error_;

#ifdef HAVE_RAPIDOBJ
    /**
     * @brief Load OBJ file using RapidOBJ parser (10-50x faster than CGAL)
     * @param filename Path to OBJ file
     * @return Loaded mesh on success, std::nullopt on failure
     */
    static std::optional<Mesh> load_obj_rapidobj(const std::string& filename);
#endif
};

}  // namespace MeshRepair
