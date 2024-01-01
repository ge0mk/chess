cmake_minimum_required(VERSION 3.22)

project(chess)

set(CMAKE_C_STANDARD 23)

add_subdirectory(glfw)

include_directories(glfw/include)

if(WIN32)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

if(SPIRV_SHADERS)
    add_compile_options(-DSPIRV_SHADERS)

    if (WIN32)
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
else()
    add_custom_target(shaders)
endif()

add_executable(chess main.c glad.c)
add_dependencies(chess shaders)
target_link_libraries(chess PRIVATE glfw SDL2_net)

if(UNIX)
    target_link_libraries(chess PRIVATE m)
endif()
