cmake_minimum_required(VERSION 3.21)

project(flora
    VERSION 0.1.0
    DESCRIPTION "Procedural vegetation and forest simulation"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FLORA_BUILD_TESTS "Build unit tests" ON)
option(FLORA_BUILD_BENCHMARKS "Build benchmarks" ON)
option(FLORA_BUILD_CLI "Build command-line tool" ON)
option(FLORA_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

add_library(flora_warnings INTERFACE)
if(MSVC)
    target_compile_options(flora_warnings INTERFACE /W4 /permissive-)
    if(FLORA_WARNINGS_AS_ERRORS)
        target_compile_options(flora_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(flora_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(FLORA_WARNINGS_AS_ERRORS)
        target_compile_options(flora_warnings INTERFACE -Werror)
    endif()
endif()

add_subdirectory(flora-core)

if(FLORA_BUILD_CLI)
    add_subdirectory(flora-cli)
endif()

if(FLORA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FLORA_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
