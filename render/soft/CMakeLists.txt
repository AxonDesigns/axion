# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_soft: the software renderer (decision 81). It needs no window and no GPU, so it builds
# everywhere, the web included. Window presentation (its ax_renderer_vtbl) needs the platform
# module.
#
# Determinism: the same snapshot must give the same bytes on every platform. ax_target_defaults
# builds every AXION target without floating-point contraction (cmake/AxionCompilerOptions.cmake).

add_library(ax_render_soft STATIC src/soft.c src/prepare.c src/raster.c src/soft_math.c)
add_library(axion::render_soft ALIAS ax_render_soft)
target_include_directories(ax_render_soft
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_render_soft PUBLIC ax_render_ir ax_base)
ax_target_defaults(ax_render_soft)
