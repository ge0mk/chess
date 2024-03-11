cmake_minimum_required(VERSION 3.22)

project(chess)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib/")

add_subdirectory(SDL)
add_subdirectory(SDL_net)
add_subdirectory(SDL_image)

include_directories(cpp-httplib)
include_directories(imgui)
include_directories(SDL/include)
include_directories(SDL_net/include)
include_directories(SDL_image/include)

if(UNIX)
    add_compile_options(-Wall -Wextra -Werror)
endif()

if(SPIRV_SHADERS)
    add_compile_options(-DSPIRV_SHADERS)

    if(WIN32)
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
    else()
        set(GLSL_VALIDATOR "glslangValidator")
    endif()

    file(GLOB_RECURSE GLSL_SOURCE_FILES "shaders/*")

    foreach(GLSL_FILE ${GLSL_SOURCE_FILES})
        get_filename_component(FILE_NAME ${GLSL_FILE} NAME)
        set(SPIRV_FILE "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
        add_custom_command(
            OUTPUT ${SPIRV_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
            COMMAND ${GLSL_VALIDATOR} -V ${GLSL_FILE} -o ${SPIRV_FILE}
            DEPENDS ${GLSL_FILE})
        list(APPEND SPIRV_BINARY_FILES ${SPIRV_FILE})
    endforeach(GLSL_FILE)

    add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})
    set(SHADER_FILES ${SPIRV_BINARY_FILES})
else()
    add_custom_target(shaders)
    file(GLOB_RECURSE SHADER_FILES "shaders/*")
endif()

add_executable(main
    src/main.cpp src/chess.cpp src/gl.cpp src/session.cpp src/server.cpp src/window.cpp
    src/glad.c
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(main PRIVATE include)
target_link_libraries(main SDL3_net::SDL3_net SDL3_image::SDL3_image SDL3::SDL3)
add_dependencies(main shaders)

install(TARGETS SDL3-shared SDL3_image-shared SDL3_net-shared main)

install(FILES ${SHADER_FILES} DESTINATION shaders)

file(GLOB_RECURSE TEXTURES "textures/*")
install(FILES ${TEXTURES} DESTINATION textures)
