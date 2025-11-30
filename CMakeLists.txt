cmake_minimum_required(VERSION 3.12...3.27)

# Preserve important cache variables for cross-platform builds
# These should be set via command line and cached:
# - CMAKE_MSVC_RUNTIME_LIBRARY (Windows MSVC runtime)
# - CMAKE_INSTALL_PREFIX (Install location)
# - CMAKE_DEBUG_POSTFIX (Debug library suffix)
# - CMAKE_PREFIX_PATH (Search paths for dependencies)
# - BUILD_SHARED_LIBS (Static vs shared libraries)

project(MeshRepair
    VERSION 1.0.0
    DESCRIPTION "Mesh hole filling CLI tool using CGAL"
    LANGUAGES CXX
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Cache variables for portable configuration
if(NOT DEFINED CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/INSTALL" CACHE PATH "Installation directory" FORCE)
endif()

set(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "Debug library postfix (e.g., 'd' for libfoo.lib -> libfood.lib)")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")

# Windows-specific: MSVC runtime library configuration
# Use command line: -DCMAKE_MSVC_RUNTIME_LIBRARY="$<$<CONFIG:Debug>:MultiThreadedDebug>$<$<CONFIG:Release>:MultiThreaded>"
if(MSVC)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        # Default to static runtime if not specified
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library")
    endif()
endif()

# Add cmake module path for custom Find*.cmake files
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Support 3rd party CMake config files (typically in lib/cmake)
# CMake will search CMAKE_PREFIX_PATH for <prefix>/lib/cmake/<package>/
# Example: if CMAKE_PREFIX_PATH=e:/DVS, CMake looks in e:/DVS/lib/cmake/CGAL/
if(CMAKE_PREFIX_PATH)
    # Ensure CMake searches lib/cmake subdirectories
    foreach(_prefix ${CMAKE_PREFIX_PATH})
        if(EXISTS "${_prefix}/lib/cmake")
            list(APPEND CMAKE_PREFIX_PATH "${_prefix}/lib/cmake")
        endif()
        if(EXISTS "${_prefix}/lib64/cmake")
            list(APPEND CMAKE_PREFIX_PATH "${_prefix}/lib64/cmake")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
endif()

# Options
option(BUILD_TESTS "Build test suite" ON)
option(ENABLE_OPENMP "Enable OpenMP for parallel processing" ON)

# Platform-specific settings
if(WIN32)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

# Find CGAL
# Users should set CMAKE_PREFIX_PATH or CGAL_DIR if CGAL is not in standard location
find_package(CGAL REQUIRED CONFIG)
if(NOT CGAL_FOUND)
    message(FATAL_ERROR
        "CGAL not found.\n"
        "Please install CGAL or set one of:\n"
        "  -DCMAKE_PREFIX_PATH=/path/to/cgal/installation\n"
        "  -DCGAL_DIR=/path/to/cgal/lib/cmake/CGAL")
endif()
message(STATUS "CGAL version: ${CGAL_VERSION}")

# Find Eigen3 (required for fairing)
# Users should set CMAKE_PREFIX_PATH or EIGEN3_INCLUDE_DIR if Eigen3 is not in standard location
find_package(Eigen3 3.2 QUIET CONFIG)
if(NOT Eigen3_FOUND AND NOT EIGEN3_FOUND)
    message(STATUS "Eigen3 not found via find_package, trying standard system paths")

    # Try standard system paths only
    if(NOT EIGEN3_INCLUDE_DIR)
        set(_eigen_search_paths
            "/usr/include/eigen3"
            "/usr/local/include/eigen3"
        )

        foreach(_path ${_eigen_search_paths})
            if(EXISTS "${_path}/Eigen/Core")
                set(EIGEN3_INCLUDE_DIR "${_path}" CACHE PATH "Path to Eigen3 include directory")
                message(STATUS "Found Eigen3 in system path: ${EIGEN3_INCLUDE_DIR}")
                break()
            endif()
        endforeach()
    endif()

    if(EIGEN3_INCLUDE_DIR AND EXISTS "${EIGEN3_INCLUDE_DIR}/Eigen/Core")
        message(STATUS "Using Eigen3 from: ${EIGEN3_INCLUDE_DIR}")
        add_library(Eigen3::Eigen INTERFACE IMPORTED)
        set_target_properties(Eigen3::Eigen PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}"
        )
        set(EIGEN3_FOUND TRUE)
    else()
        message(FATAL_ERROR
            "Eigen3 not found.\n"
            "Please install Eigen3 or set one of:\n"
            "  -DCMAKE_PREFIX_PATH=/path/to/eigen3/installation\n"
            "  -DEIGEN3_INCLUDE_DIR=/path/to/eigen3/include")
    endif()
else()
    message(STATUS "Eigen3 found via find_package")
endif()

# Enable CGAL to use Eigen for fairing
add_definitions(-DCGAL_EIGEN3_ENABLED)

# Find Boost (required by CGAL)
find_package(Boost REQUIRED)
if(NOT Boost_FOUND)
    message(WARNING "Boost not found. CGAL may require Boost for some features.")
endif()

# Optional: Find OpenMP
if(ENABLE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
    else()
        message(STATUS "OpenMP not found. Parallel processing disabled.")
    endif()
endif()

# Threading support (required for threadpool)
find_package(Threads REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/meshrepair/include
)

# Find RapidOBJ (header-only library for fast OBJ parsing)
find_package(RapidOBJ QUIET)
if(RapidOBJ_FOUND)
    message(STATUS "RapidOBJ found: ${RapidOBJ_INCLUDE_DIR}")
    add_definitions(-DHAVE_RAPIDOBJ)
else()
    message(STATUS "RapidOBJ not found - OBJ loading will use CGAL parser (slower)")
endif()

# Find nlohmann/json (header-only library for JSON parsing)
# Required for engine IPC mode
# First try to find via CMake config (if available)
find_package(nlohmann_json QUIET)

if(NOT nlohmann_json_FOUND)
    # Try to find header directly in CMAKE_PREFIX_PATH/include
    set(_nlohmann_json_found FALSE)

    if(CMAKE_PREFIX_PATH)
        foreach(_prefix ${CMAKE_PREFIX_PATH})
            if(EXISTS "${_prefix}/include/nlohmann/json.hpp")
                set(NLOHMANN_JSON_INCLUDE_DIR "${_prefix}/include" CACHE PATH "Path to nlohmann/json include directory")
                message(STATUS "nlohmann/json found: ${NLOHMANN_JSON_INCLUDE_DIR}")
                add_library(nlohmann_json INTERFACE IMPORTED)
                target_include_directories(nlohmann_json INTERFACE "${NLOHMANN_JSON_INCLUDE_DIR}")
                add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
                set(_nlohmann_json_found TRUE)
                break()
            endif()
        endforeach()
    endif()

    # If still not found, try standard system paths
    if(NOT _nlohmann_json_found)
        set(_nlohmann_search_paths
            "/usr/include"
            "/usr/local/include"
        )

        foreach(_path ${_nlohmann_search_paths})
            if(EXISTS "${_path}/nlohmann/json.hpp")
                set(NLOHMANN_JSON_INCLUDE_DIR "${_path}" CACHE PATH "Path to nlohmann/json include directory")
                message(STATUS "nlohmann/json found in system path: ${NLOHMANN_JSON_INCLUDE_DIR}")
                add_library(nlohmann_json INTERFACE IMPORTED)
                target_include_directories(nlohmann_json INTERFACE "${NLOHMANN_JSON_INCLUDE_DIR}")
                add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
                set(_nlohmann_json_found TRUE)
                break()
            endif()
        endforeach()
    endif()

    # If still not found, fetch from GitHub
    if(NOT _nlohmann_json_found)
        message(STATUS "nlohmann/json not found locally, fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
        )
        FetchContent_MakeAvailable(nlohmann_json)
        message(STATUS "nlohmann/json: fetched from GitHub")
    endif()
else()
    message(STATUS "nlohmann/json found via CMake package")
endif()

# Source files
set(MESHREPAIR_SOURCES
    meshrepair/main.cpp
    meshrepair/cli_main.cpp
    meshrepair/engine_main.cpp
    meshrepair/debug_path.cpp
    meshrepair/engine/protocol.cpp
    meshrepair/engine/engine_wrapper.cpp
    meshrepair/engine/engine_dispatch.cpp
    meshrepair/engine/mesh_binary.cpp
    meshrepair/engine/socket_stream.cpp
    meshrepair/mesh_loader.cpp
    meshrepair/hole_ops.cpp
    meshrepair/mesh_validator.cpp
    meshrepair/progress_reporter.cpp
    meshrepair/mesh_preprocessor.cpp
    meshrepair/worker_pool.cpp
    meshrepair/pipeline_ops.cpp
    meshrepair/submesh_ops.cpp
)

# Header files
set(MESHREPAIR_HEADERS
    meshrepair/engine/protocol.h
    meshrepair/engine/engine_wrapper.h
    meshrepair/engine/engine_dispatch.h
    meshrepair/engine/mesh_binary.h
    meshrepair/engine/socket_stream.h
    meshrepair/include/c_api.h
    meshrepair/include/types.h
    meshrepair/include/config.h
    meshrepair/include/help_printer.h
    meshrepair/include/mesh_loader.h
    meshrepair/include/hole_ops.h
    meshrepair/include/mesh_validator.h
    meshrepair/include/progress_reporter.h
    meshrepair/include/mesh_preprocessor.h
    meshrepair/include/polygon_soup_repair.h
    meshrepair/include/worker_pool.h
    meshrepair/include/pipeline_ops.h
    meshrepair/include/parallel_utils.h
    meshrepair/include/parallel_detection.h
    meshrepair/include/submesh_ops.h
    meshrepair/include/hole_ops.h
    meshrepair/include/thread_safe_cout.h
    meshrepair/include/debug_path.h
)

# Main executable
add_executable(meshrepair
    ${MESHREPAIR_SOURCES}
    ${MESHREPAIR_HEADERS}
)

# Link libraries
target_link_libraries(meshrepair
    CGAL::CGAL
    Eigen3::Eigen
    Threads::Threads
    nlohmann_json::nlohmann_json
)

if(RapidOBJ_FOUND)
    target_link_libraries(meshrepair RapidOBJ::RapidOBJ)
endif()

if(OpenMP_CXX_FOUND)
    target_link_libraries(meshrepair OpenMP::OpenMP_CXX)
    target_compile_definitions(meshrepair PRIVATE USE_OPENMP)
endif()

# Windows: Link Winsock2 for socket support
if(WIN32)
    target_link_libraries(meshrepair ws2_32)
endif()

# Compiler-specific optimizations (configuration-aware for multi-config generators)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang flags
    target_compile_options(meshrepair PRIVATE
        -Wall
        -Wextra
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-Wpedantic>
    )
elseif(MSVC)
    # MSVC flags (supports multi-config generators like Visual Studio)
    target_compile_options(meshrepair PRIVATE
        /W4
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Debug>:/Zi>
    )

    # Link-time code generation for Release
    target_link_options(meshrepair PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )

    # Apply debug postfix
    set_target_properties(meshrepair PROPERTIES
        DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}"
    )
endif()

# Installation
install(TARGETS meshrepair
    RUNTIME DESTINATION bin
)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "======================================")
message(STATUS "  MeshRepair Configuration")
message(STATUS "======================================")
message(STATUS "Project version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  CGAL version: ${CGAL_VERSION}")
if(EIGEN3_INCLUDE_DIR)
    message(STATUS "  Eigen3: ${EIGEN3_INCLUDE_DIR}")
else()
    message(STATUS "  Eigen3: found via package")
endif()
if(Boost_FOUND)
    message(STATUS "  Boost: ${Boost_VERSION}")
endif()
message(STATUS "  OpenMP: ${OpenMP_CXX_FOUND}")
if(RapidOBJ_FOUND)
    message(STATUS "  RapidOBJ: ${RapidOBJ_INCLUDE_DIR}")
else()
    message(STATUS "  RapidOBJ: NOT FOUND (will use CGAL OBJ parser)")
endif()
message(STATUS "  nlohmann/json: found (required for engine IPC mode)")
message(STATUS "")
message(STATUS "Options:")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Debug postfix: ${CMAKE_DEBUG_POSTFIX}")
if(MSVC)
    message(STATUS "  MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()
if(CMAKE_PREFIX_PATH)
    message(STATUS "  Prefix path: ${CMAKE_PREFIX_PATH}")
endif()
message(STATUS "======================================")
message(STATUS "")
