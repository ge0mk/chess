cmake_minimum_required(VERSION 3.22)

project(chess)

set(CMAKE_C_STANDARD 23 CACHE STRING "C standard to conform to")

find_package(glfw3 REQUIRED)

add_compile_options(-Wall -Wextra -Werror -DSPIRV_SHADERS)

add_executable(chess main.c glad.c)
target_link_libraries(chess PRIVATE glfw m)
