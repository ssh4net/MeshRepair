#pragma once

#include <cstddef>

namespace MeshRepair {

// Configuration constants
namespace Config {

    // Version information
    constexpr const char* VERSION = "1.0.0";
    constexpr const char* APP_NAME = "MeshHoleFiller";

    // Default hole filling parameters
    constexpr unsigned int DEFAULT_FAIRING_CONTINUITY = 1;  // C¹ continuity
    constexpr size_t DEFAULT_MAX_HOLE_BOUNDARY = 1000;      // vertices
    constexpr double DEFAULT_MAX_HOLE_DIAMETER_RATIO = 0.1; // 10% of mesh bbox

    // Algorithm defaults
    constexpr bool DEFAULT_USE_2D_CDT = true;
    constexpr bool DEFAULT_USE_3D_DELAUNAY = true;
    constexpr bool DEFAULT_SKIP_CUBIC = false;
    constexpr bool DEFAULT_REFINE = true;

    // Performance tuning
    constexpr size_t PROGRESS_UPDATE_INTERVAL = 100;        // Update every N operations

    // Limits
    constexpr size_t MAX_MESH_VERTICES = 100'000'000;       // 100M vertices
    constexpr size_t LARGE_MESH_THRESHOLD = 1'000'000;      // 1M vertices

} // namespace Config

} // namespace MeshRepair
