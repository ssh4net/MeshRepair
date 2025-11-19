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

}  // namespace DebugPath
}  // namespace MeshRepair

