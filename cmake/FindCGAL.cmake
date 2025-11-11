# FindCGAL.cmake
# Helper module to find CGAL library
#
# Sets the following variables:
#   CGAL_FOUND - True if CGAL is found
#   CGAL_INCLUDE_DIRS - Include directories for CGAL
#   CGAL_LIBRARIES - Libraries to link against
#   CGAL_VERSION - Version string

# First try to find CGAL using its own CMake config
find_package(CGAL QUIET CONFIG)

if(CGAL_FOUND)
    message(STATUS "Found CGAL config: ${CGAL_DIR}")
    return()
endif()

# Manual search if config not found
set(CGAL_SEARCH_PATHS
    ${CGAL_DIR}
    /mnt/e/GH/cgal
    /usr/local
    /usr
    $ENV{CGAL_DIR}
)

# Find CGAL include directory
find_path(CGAL_INCLUDE_DIR
    NAMES CGAL/basic.h
    PATHS ${CGAL_SEARCH_PATHS}
    PATH_SUFFIXES include
    DOC "CGAL include directory"
)

# Extract version if found
if(CGAL_INCLUDE_DIR AND EXISTS "${CGAL_INCLUDE_DIR}/CGAL/version.h")
    file(READ "${CGAL_INCLUDE_DIR}/CGAL/version.h" _cgal_version_header)
    string(REGEX MATCH "define[ \t]+CGAL_VERSION[ \t]+([0-9.]+)" _cgal_version_match "${_cgal_version_header}")
    if(_cgal_version_match)
        set(CGAL_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CGAL
    REQUIRED_VARS CGAL_INCLUDE_DIR
    VERSION_VAR CGAL_VERSION
)

if(CGAL_FOUND AND NOT TARGET CGAL::CGAL)
    set(CGAL_INCLUDE_DIRS ${CGAL_INCLUDE_DIR})
    add_library(CGAL::CGAL INTERFACE IMPORTED)
    set_target_properties(CGAL::CGAL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CGAL_INCLUDE_DIRS}"
    )
endif()

mark_as_advanced(CGAL_INCLUDE_DIR)
