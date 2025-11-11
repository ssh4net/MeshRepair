# FindEigen3.cmake
# Helper module to find Eigen3 library
#
# Sets the following variables:
#   EIGEN3_FOUND - True if Eigen3 is found
#   EIGEN3_INCLUDE_DIR - Include directory for Eigen3
#   EIGEN3_VERSION - Version string

# First try standard config-based search
find_package(Eigen3 ${Eigen3_FIND_VERSION} QUIET CONFIG)

if(EIGEN3_FOUND OR Eigen3_FOUND)
    message(STATUS "Found Eigen3 config")
    return()
endif()

# Manual search
set(EIGEN3_SEARCH_PATHS
    ${EIGEN3_INCLUDE_DIR}
    /mnt/e/UBS/include/eigen3
    /usr/local/include/eigen3
    /usr/include/eigen3
    /opt/local/include/eigen3
    $ENV{EIGEN3_INCLUDE_DIR}
)

find_path(EIGEN3_INCLUDE_DIR
    NAMES Eigen/Core
    PATHS ${EIGEN3_SEARCH_PATHS}
    DOC "Eigen3 include directory"
)

# Extract version
if(EIGEN3_INCLUDE_DIR AND EXISTS "${EIGEN3_INCLUDE_DIR}/Eigen/src/Core/util/Macros.h")
    file(READ "${EIGEN3_INCLUDE_DIR}/Eigen/src/Core/util/Macros.h" _eigen_version_header)
    string(REGEX MATCH "define[ \t]+EIGEN_WORLD_VERSION[ \t]+([0-9]+)" _eigen_world_version_match "${_eigen_version_header}")
    string(REGEX MATCH "define[ \t]+EIGEN_MAJOR_VERSION[ \t]+([0-9]+)" _eigen_major_version_match "${_eigen_version_header}")
    string(REGEX MATCH "define[ \t]+EIGEN_MINOR_VERSION[ \t]+([0-9]+)" _eigen_minor_version_match "${_eigen_version_header}")

    if(_eigen_world_version_match)
        set(EIGEN3_WORLD_VERSION "${CMAKE_MATCH_1}")
    endif()
    if(_eigen_major_version_match)
        set(EIGEN3_MAJOR_VERSION "${CMAKE_MATCH_1}")
    endif()
    if(_eigen_minor_version_match)
        set(EIGEN3_MINOR_VERSION "${CMAKE_MATCH_1}")
    endif()

    set(EIGEN3_VERSION "${EIGEN3_WORLD_VERSION}.${EIGEN3_MAJOR_VERSION}.${EIGEN3_MINOR_VERSION}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Eigen3
    REQUIRED_VARS EIGEN3_INCLUDE_DIR
    VERSION_VAR EIGEN3_VERSION
)

if(EIGEN3_FOUND AND NOT TARGET Eigen3::Eigen)
    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(EIGEN3_INCLUDE_DIR)
