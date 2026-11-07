# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

# The version lives in base/include/axion/ax_version.h, the header bindings check at load time,
# so CMake reads it from there instead of keeping a second copy.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/base/include/axion/ax_version.h" _ax_version_lines
     REGEX "^#define AX_VERSION_(MAJOR|MINOR|PATCH) +[0-9]+")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*#define AX_VERSION_${_part} +([0-9]+).*" "\\1" _ax_${_part}
                         "${_ax_version_lines}")
endforeach()

# Only C for now. Platform glue in other languages (Objective-C on Apple, Kotlin or Java on
# Android) is enabled per platform through cmake/AxionLanguages.cmake when its module lands.
project(axion
    VERSION "${_ax_MAJOR}.${_ax_MINOR}.${_ax_PATCH}"
    DESCRIPTION "AXION UI runtime"
    LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(AxionCompilerOptions)
include(AxionLanguages)

option(AX_BUILD_TESTS "Build AXION's unit tests" ${PROJECT_IS_TOP_LEVEL})
option(AX_WEB_THREADS "Emscripten only: build with -pthread (needs a cross-origin isolated page)" OFF)
set(AX_SANITIZE "" CACHE STRING
    "Sanitizers for GCC and Clang builds, as a list: address;undefined")

ax_enable_platform_languages()

add_subdirectory(base)

if(AX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
