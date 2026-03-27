cmake_minimum_required(VERSION 3.21)
project(graphics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(PHYSICS_ONLY ON CACHE BOOL "Only build Jolt library")
set(TARGET_HELLO_WORLD OFF CACHE BOOL "")
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "")
set(TARGET_SAMPLES OFF CACHE BOOL "")
set(TARGET_UNIT_TESTS OFF CACHE BOOL "")
set(TARGET_VIEWER OFF CACHE BOOL "")
add_subdirectory(third_party/JoltPhysics/Build)

add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(shaders)
