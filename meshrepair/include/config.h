#pragma once

#include <cstddef>

namespace MeshRepair {

// Configuration constants
namespace Config {

    // Version information
#define MESHREPAIR_VERSION_MAJOR 2
#define MESHREPAIR_VERSION_MINOR 7
#define MESHREPAIR_VERSION_PATCH 0
#define MESHREPAIR_STRINGIFY_INNER(x) #x
#define MESHREPAIR_STRINGIFY(x) MESHREPAIR_STRINGIFY_INNER(x)

// Build configuration string (Debug/Release/etc.)
#ifndef MESHREPAIR_BUILD_CONFIG
#    ifdef NDEBUG
#        define MESHREPAIR_BUILD_CONFIG "Release"
#    else
#        define MESHREPAIR_BUILD_CONFIG "Debug"
#    endif
#endif

    constexpr int VERSION_MAJOR   = MESHREPAIR_VERSION_MAJOR;
    constexpr int VERSION_MINOR   = MESHREPAIR_VERSION_MINOR;
    constexpr int VERSION_PATCH   = MESHREPAIR_VERSION_PATCH;
    constexpr const char* VERSION = MESHREPAIR_STRINGIFY(MESHREPAIR_VERSION_MAJOR) "." MESHREPAIR_STRINGIFY(
        MESHREPAIR_VERSION_MINOR) "." MESHREPAIR_STRINGIFY(MESHREPAIR_VERSION_PATCH);

#undef MESHREPAIR_STRINGIFY
#undef MESHREPAIR_STRINGIFY_INNER
#undef MESHREPAIR_VERSION_PATCH
#undef MESHREPAIR_VERSION_MINOR
#undef MESHREPAIR_VERSION_MAJOR
    constexpr const char* APP_NAME = "Mesh Repair";

    // Build information (updated at compile time)
    constexpr const char* BUILD_DATE = __DATE__;
    constexpr const char* BUILD_TIME = __TIME__;

    // Default hole filling parameters
    constexpr unsigned int DEFAULT_FAIRING_CONTINUITY = 1;     // C1 continuity
    constexpr size_t DEFAULT_MAX_HOLE_BOUNDARY        = 1000;  // vertices
    constexpr double DEFAULT_MAX_HOLE_DIAMETER_RATIO  = 0.10;  // 10% of mesh bbox

    // Algorithm defaults
    constexpr bool DEFAULT_USE_2D_CDT            = true;
    constexpr bool DEFAULT_USE_3D_DELAUNAY       = true;
    constexpr bool DEFAULT_SKIP_CUBIC            = false;
    constexpr bool DEFAULT_REFINE                = true;
    constexpr size_t DEFAULT_MIN_PARTITION_EDGES = 100;  // Minimum boundary edges to justify a partition

    // Performance tuning
    constexpr size_t PROGRESS_UPDATE_INTERVAL = 100;  // Update every N operations

    // Limits
    constexpr size_t MAX_MESH_VERTICES    = 100'000'000;  // 100M vertices
    constexpr size_t LARGE_MESH_THRESHOLD = 1'000'000;    // 1M vertices

}  // namespace Config

}  // namespace MeshRepair
