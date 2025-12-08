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

install(TARGETS meshrepair_gui
    RUNTIME DESTINATION bin
)

install(FILES "${MESHREPAIR_GUI_FONT}"
    DESTINATION bin/fonts
)
