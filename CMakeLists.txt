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
# Search in CMAKE_PREFIX_PATH first, then fall back to hardcoded paths
find_package(CGAL QUIET CONFIG)
if(NOT CGAL_FOUND)
    # Try manual paths
    if(NOT CGAL_DIR)
        if(EXISTS "/mnt/e/GH/cgal")
            set(CGAL_DIR "/mnt/e/GH/cgal" CACHE PATH "Path to CGAL directory")
        elseif(EXISTS "e:/GH/cgal")
            set(CGAL_DIR "e:/GH/cgal" CACHE PATH "Path to CGAL directory")
        endif()
    endif()
    find_package(CGAL REQUIRED COMPONENTS Core)
endif()
if(NOT CGAL_FOUND)
    message(FATAL_ERROR "CGAL not found. Set CGAL_DIR to CGAL installation directory.")
endif()
message(STATUS "CGAL version: ${CGAL_VERSION}")

# Find Eigen3 (required for fairing)
# Search in CMAKE_PREFIX_PATH first, then fall back to known locations
find_package(Eigen3 3.2 QUIET CONFIG)
if(NOT Eigen3_FOUND AND NOT EIGEN3_FOUND)
    message(STATUS "Eigen3 not found via find_package, trying manual search")

    # Try to find Eigen3 manually
    if(NOT EIGEN3_INCLUDE_DIR)
        set(_eigen_search_paths
            "${CMAKE_PREFIX_PATH}/include/eigen3"
            "/mnt/e/UBS/include/eigen3"
            "e:/UBS/include/eigen3"
            "/usr/include/eigen3"
            "/usr/local/include/eigen3"
        )

        foreach(_path ${_eigen_search_paths})
            if(EXISTS "${_path}/Eigen/Core")
                set(EIGEN3_INCLUDE_DIR "${_path}" CACHE PATH "Path to Eigen3 include directory")
                break()
            endif()
        endforeach()
    endif()

    if(EXISTS "${EIGEN3_INCLUDE_DIR}/Eigen/Core")
        message(STATUS "Using Eigen3 from: ${EIGEN3_INCLUDE_DIR}")
        add_library(Eigen3::Eigen INTERFACE IMPORTED)
        set_target_properties(Eigen3::Eigen PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}"
        )
        set(EIGEN3_FOUND TRUE)
    else()
        message(FATAL_ERROR "Eigen3 not found. Set EIGEN3_INCLUDE_DIR or CMAKE_PREFIX_PATH to Eigen3 location.")
    endif()
else()
    message(STATUS "Eigen3 found via find_package")
    if(EIGEN3_INCLUDE_DIR)
        message(STATUS "Eigen3 include dir: ${EIGEN3_INCLUDE_DIR}")
    endif()
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

# Source files
set(MESHREPAIR_SOURCES
    meshrepair/main.cpp
    meshrepair/mesh_loader.cpp
    meshrepair/hole_detector.cpp
    meshrepair/hole_filler.cpp
    meshrepair/mesh_validator.cpp
    meshrepair/progress_reporter.cpp
    meshrepair/mesh_preprocessor.cpp
    meshrepair/thread_manager.cpp
    meshrepair/pipeline_processor.cpp
    meshrepair/mesh_partitioner.cpp
    meshrepair/submesh_extractor.cpp
    meshrepair/mesh_merger.cpp
    meshrepair/parallel_hole_filler.cpp
)

# Header files
set(MESHREPAIR_HEADERS
    meshrepair/include/types.h
    meshrepair/include/config.h
    meshrepair/include/mesh_loader.h
    meshrepair/include/hole_detector.h
    meshrepair/include/hole_filler.h
    meshrepair/include/mesh_validator.h
    meshrepair/include/progress_reporter.h
    meshrepair/include/mesh_preprocessor.h
    meshrepair/include/thread_manager.h
    meshrepair/include/threadpool.h
    meshrepair/include/pipeline_processor.h
    meshrepair/include/parallel_utils.h
    meshrepair/include/parallel_detection.h
    meshrepair/include/mesh_partitioner.h
    meshrepair/include/submesh_extractor.h
    meshrepair/include/mesh_merger.h
    meshrepair/include/parallel_hole_filler.h
    meshrepair/include/thread_safe_cout.h
)

# Main executable
add_executable(mesh_hole_filler
    ${MESHREPAIR_SOURCES}
    ${MESHREPAIR_HEADERS}
)

# Link libraries
target_link_libraries(mesh_hole_filler
    CGAL::CGAL
    Eigen3::Eigen
    Threads::Threads
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(mesh_hole_filler OpenMP::OpenMP_CXX)
    target_compile_definitions(mesh_hole_filler PRIVATE USE_OPENMP)
endif()

# Compiler-specific optimizations (configuration-aware for multi-config generators)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang flags
    target_compile_options(mesh_hole_filler PRIVATE
        -Wall
        -Wextra
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-Wpedantic>
    )
elseif(MSVC)
    # MSVC flags (supports multi-config generators like Visual Studio)
    target_compile_options(mesh_hole_filler PRIVATE
        /W4
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Debug>:/Zi>
    )

    # Link-time code generation for Release
    target_link_options(mesh_hole_filler PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )

    # Apply debug postfix
    set_target_properties(mesh_hole_filler PROPERTIES
        DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}"
    )
endif()

# Installation
install(TARGETS mesh_hole_filler
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
