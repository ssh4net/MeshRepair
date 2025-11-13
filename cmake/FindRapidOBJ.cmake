# FindRapidOBJ.cmake
# Helper module to find RapidOBJ header-only library
#
# Sets the following variables:
#   RapidOBJ_FOUND - True if RapidOBJ is found
#   RapidOBJ_INCLUDE_DIR - Include directory for RapidOBJ

# Manual search for header-only library
set(RapidOBJ_SEARCH_PATHS
    ${RapidOBJ_INCLUDE_DIR}
    ${CMAKE_PREFIX_PATH}/include
    /mnt/e/UBS/include
    e:/UBS/include
    /usr/local/include
    /usr/include
    /opt/local/include
    $ENV{RapidOBJ_INCLUDE_DIR}
)

find_path(RapidOBJ_INCLUDE_DIR
    NAMES rapidobj/rapidobj.hpp
    PATHS ${RapidOBJ_SEARCH_PATHS}
    DOC "RapidOBJ include directory"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RapidOBJ
    REQUIRED_VARS RapidOBJ_INCLUDE_DIR
)

if(RapidOBJ_FOUND AND NOT TARGET RapidOBJ::RapidOBJ)
    add_library(RapidOBJ::RapidOBJ INTERFACE IMPORTED)
    set_target_properties(RapidOBJ::RapidOBJ PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${RapidOBJ_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(RapidOBJ_INCLUDE_DIR)
