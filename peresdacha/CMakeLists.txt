cmake_minimum_required(VERSION 3.13)

project(peresdacha)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Debug)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE PERESDA_SOURCES1 . ./*.[ch]pp)
file(GLOB_RECURSE PERESDA_SOURCES2 . ./*.[ch])

add_executable(hw ${PERESDA_SOURCES1} ${PERESDA_SOURCES2})
target_link_libraries(hw PUBLIC project_options project_warnings)
target_link_libraries(hw PUBLIC raylib flecs)

