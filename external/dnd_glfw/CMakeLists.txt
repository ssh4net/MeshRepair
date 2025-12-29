add_library(dnd_glfw INTERFACE)

target_include_directories(dnd_glfw
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(APPLE)
    # dnd_glfw_macos.mm includes <GLFW/glfw3.h>, so this target must be linked
    # to the GLFW imported target to inherit its include directories.
    find_package(glfw3 REQUIRED CONFIG)

    set(GLFW_TARGET "")
    if(TARGET glfw)
        set(GLFW_TARGET glfw)
    elseif(TARGET glfw::glfw)
        set(GLFW_TARGET glfw::glfw)
    else()
        message(FATAL_ERROR "glfw target not found after find_package(glfw3 CONFIG).")
    endif()

    add_library(dnd_glfw_macos STATIC
        dnd_glfw_macos.mm
    )

    target_link_libraries(dnd_glfw_macos
        PUBLIC
            ${GLFW_TARGET}
            "-framework Cocoa"
    )

    target_link_libraries(dnd_glfw
        INTERFACE
            dnd_glfw_macos
    )
endif()
