find_package(glfw3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(nfd REQUIRED CONFIG)

set(IMGUI_DIR "" CACHE PATH "Path to Dear ImGui source tree")
if(NOT IMGUI_DIR)
    set(_imgui_candidates
        "${CMAKE_SOURCE_DIR}/../imgui"
        "${CMAKE_SOURCE_DIR}/../../imgui"
        "/mnt/e/GH/imgui"
    )
    foreach(_candidate ${_imgui_candidates})
        if(NOT IMGUI_DIR AND EXISTS "${_candidate}/imgui.h")
            set(IMGUI_DIR "${_candidate}" CACHE PATH "Path to Dear ImGui source tree" FORCE)
        endif()
    endforeach()
endif()

if(NOT IMGUI_DIR OR NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "IMGUI_DIR must point to Dear ImGui sources (imgui.h).")
endif()

if(NOT TARGET GLEW::glew_s)
    message(FATAL_ERROR "GLEW::glew_s target not found (ensure GLEW is available in CMAKE_PREFIX_PATH).")
endif()

if(NOT TARGET nfd::nfd)
    message(FATAL_ERROR "nfd::nfd target not found (nativefiledialog-extended).")
endif()

set(GLFW_TARGET "")
if(TARGET glfw)
    set(GLFW_TARGET glfw)
elseif(TARGET glfw::glfw)
    set(GLFW_TARGET glfw::glfw)
else()
    message(FATAL_ERROR "glfw target not found after find_package(glfw3 CONFIG).")
endif()

if(UNIX AND NOT APPLE)
    # Validate that the imported nfd target is built for this platform (avoid Windows import libs)
    get_target_property(_nfd_location nfd::nfd IMPORTED_LOCATION)
    if(NOT _nfd_location)
        get_target_property(_nfd_location nfd::nfd IMPORTED_LOCATION_RELEASE)
    endif()
    if(_nfd_location AND _nfd_location MATCHES "\\.lib$")
        message(FATAL_ERROR "nfd::nfd appears to point to a Windows import library (${_nfd_location}). "
            "Install a Linux build of nativefiledialog-extended or adjust CMAKE_PREFIX_PATH.")
    endif()
endif()

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui
    PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUI_DIR}/misc/cpp
)
target_link_libraries(imgui PUBLIC ${GLFW_TARGET})
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLEW GLEW_STATIC)

add_executable(meshrepair_gui
    main.cpp
    mr_gui.cpp
    include/mr_gui.h
)

if(APPLE)
    # Build a proper .app bundle on macOS.
    set_target_properties(meshrepair_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "MeshRepair"
    )

    # App icon (.icns) generated from PNG iconset sources.
    set(_icon_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/icons")
    set(_iconset_dir "${CMAKE_CURRENT_BINARY_DIR}/MeshRepair.iconset")
    set(_icns_file   "${CMAKE_CURRENT_BINARY_DIR}/MeshRepair.icns")

    set(_iconset_pngs
        "${_icon_src_dir}/icon_16x16.png"
        "${_icon_src_dir}/icon_16x16@2x.png"
        "${_icon_src_dir}/icon_32x32.png"
        "${_icon_src_dir}/icon_32x32@2x.png"
        "${_icon_src_dir}/icon_64x64.png"
        "${_icon_src_dir}/icon_64x64@2x.png"
        "${_icon_src_dir}/icon_128x128.png"
        "${_icon_src_dir}/icon_128x128@2x.png"
        "${_icon_src_dir}/icon_256x256.png"
        "${_icon_src_dir}/icon_256x256@2x.png"
        "${_icon_src_dir}/icon_512x512.png"
        "${_icon_src_dir}/icon_512x512@2x.png"
    )

    foreach(_png IN LISTS _iconset_pngs)
        if(NOT EXISTS "${_png}")
            message(FATAL_ERROR "Missing icon asset: ${_png}")
        endif()
    endforeach()

    add_custom_command(
        OUTPUT "${_icns_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_iconset_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_16x16.png"       "${_iconset_dir}/icon_16x16.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_16x16@2x.png"    "${_iconset_dir}/icon_16x16@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_32x32.png"       "${_iconset_dir}/icon_32x32.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_32x32@2x.png"    "${_iconset_dir}/icon_32x32@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_64x64.png"       "${_iconset_dir}/icon_64x64.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_64x64@2x.png"    "${_iconset_dir}/icon_64x64@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_128x128.png"     "${_iconset_dir}/icon_128x128.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_128x128@2x.png"  "${_iconset_dir}/icon_128x128@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_256x256.png"     "${_iconset_dir}/icon_256x256.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_256x256@2x.png"  "${_iconset_dir}/icon_256x256@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_512x512.png"     "${_iconset_dir}/icon_512x512.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_icon_src_dir}/icon_512x512@2x.png"  "${_iconset_dir}/icon_512x512@2x.png"
        COMMAND /usr/bin/iconutil -c icns "${_iconset_dir}" -o "${_icns_file}"
        DEPENDS ${_iconset_pngs}
        VERBATIM
        COMMENT "Generating MeshRepair.icns"
    )

    add_custom_target(meshrepair_gui_icns ALL DEPENDS "${_icns_file}")
    add_dependencies(meshrepair_gui meshrepair_gui_icns)

    set_source_files_properties("${_icns_file}" PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(meshrepair_gui PRIVATE "${_icns_file}")
    set_target_properties(meshrepair_gui PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "MeshRepair.icns"
    )
endif()

set(MESHREPAIR_GUI_FONT "${CMAKE_SOURCE_DIR}/external/Fira/FiraSans-Medium.otf")
if(NOT EXISTS "${MESHREPAIR_GUI_FONT}")
    message(FATAL_ERROR "Expected font not found at ${MESHREPAIR_GUI_FONT}")
endif()

add_custom_command(TARGET meshrepair_gui PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MESHREPAIR_GUI_FONT}" "${CMAKE_CURRENT_BINARY_DIR}/fonts/FiraSans-Medium.otf"
    COMMENT "Copy FiraSans font for runtime use")

target_link_libraries(meshrepair_gui
    PRIVATE
        meshrepair_core
        imgui
        ${GLFW_TARGET}
        dnd_glfw
        OpenGL::GL
        GLEW::glew_s
        nfd::nfd
)

if(APPLE)
    # Bundle-friendly rpaths:
    # - .app layout: Contents/MacOS/<exe> loads deps from Contents/Frameworks
    # - non-bundle dev layout: allow deps next to the executable too
    set_target_properties(meshrepair_gui PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks;@executable_path"
        INSTALL_RPATH "@executable_path/../Frameworks;@executable_path"
        # Avoid embedding absolute build-tree rpaths (e.g. /Users/.../MOS/lib) which makes install non-idempotent.
        SKIP_BUILD_RPATH TRUE
        # Embed the install rpath at link time and avoid install_name_tool rpath edits (keeps install idempotent).
        BUILD_WITH_INSTALL_RPATH TRUE
        SKIP_INSTALL_RPATH TRUE
    )
endif()

target_include_directories(meshrepair_gui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(meshrepair_gui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLEW GLEW_STATIC)
target_compile_definitions(meshrepair_gui PRIVATE MESHREPAIR_GUI_FONT_PATH="${MESHREPAIR_GUI_FONT}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(native_flag $<$<AND:$<CONFIG:Release>,$<BOOL:${MESHREPAIR_ENABLE_NATIVE_ARCH}>>:-march=native>)
    set(lto_flag $<$<AND:$<CONFIG:Release>,$<BOOL:${MESHREPAIR_ENABLE_LTO}>>:-flto>)
    target_compile_options(meshrepair_gui PRIVATE
        -Wall
        -Wextra
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g>
        ${native_flag}
        ${lto_flag}
    )
    if(MESHREPAIR_ENABLE_LTO)
        target_link_options(meshrepair_gui PRIVATE ${lto_flag})
    endif()
elseif(MSVC)
    target_compile_options(meshrepair_gui PRIVATE /W4 /permissive- /EHsc)
endif()

if(WIN32)
    target_link_libraries(meshrepair_gui PRIVATE ole32)
endif()

if(APPLE)
    install(TARGETS meshrepair_gui
        BUNDLE DESTINATION .
    )
else()
    install(TARGETS meshrepair_gui
        RUNTIME DESTINATION bin
    )
endif()

if(APPLE AND TARGET TBB::tbb)
    # Bundle oneTBB dylibs so @rpath lookups succeed via @executable_path/../Frameworks.
    get_target_property(_tbb_location TBB::tbb IMPORTED_LOCATION)
    if(NOT _tbb_location)
        get_target_property(_tbb_location TBB::tbb IMPORTED_LOCATION_RELEASE)
    endif()
    if(_tbb_location)
        get_filename_component(_tbb_dir "${_tbb_location}" DIRECTORY)
        file(GLOB _tbb_dylibs "${_tbb_dir}/libtbb*.dylib")
        if(_tbb_dylibs)
            install(FILES ${_tbb_dylibs} DESTINATION "MeshRepair.app/Contents/Frameworks")
        endif()
    endif()
endif()

install(FILES "${MESHREPAIR_GUI_FONT}"
    DESTINATION bin/fonts
)

if(APPLE)
    install(FILES "${MESHREPAIR_GUI_FONT}"
        DESTINATION "MeshRepair.app/Contents/Resources/fonts"
    )
endif()
