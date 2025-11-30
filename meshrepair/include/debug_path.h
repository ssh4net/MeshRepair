#pragma once

#include <string>

namespace MeshRepair {
namespace DebugPath {

    // Configure base directory for debug outputs (PLY dumps, etc.)
    void set_base_directory(const std::string& path);

    // Resolve filename against base directory (creates directories if needed).
    std::string resolve(const std::string& filename);

    // Retrieve current base directory.
    const std::string& get_base_directory();

    // Generate an auto-incremented debug filename with optional extension (defaults to .ply)
    std::string next_debug_filename(const std::string& label, const std::string& extension = ".ply");

    // Start a new logical step and return the resolved prefix (no extension).
    std::string start_step(const std::string& label = "");

    // Start a new step and return a full filename with extension (defaults to .ply).
    std::string step_file(const std::string& label, const std::string& extension = ".ply");

}  // namespace DebugPath
}  // namespace MeshRepair
