# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_headless: the headless host (decision 84) and its PNG codec (decision 82). No platform
# dependency: it builds everywhere, the web included.

add_library(ax_headless STATIC src/png.c)
add_library(axion::headless ALIAS ax_headless)
target_include_directories(ax_headless
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_headless PUBLIC ax_base)
ax_target_defaults(ax_headless)
