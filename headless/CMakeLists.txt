# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_headless: the headless host (decision 85) and its PNG codec (decision 82). Its code uses no
# platform module: it builds everywhere, the web included, and runs any app without a window or a
# GPU. With the gpu module it can also draw with ax_render_wgpu offscreen.

add_library(ax_headless STATIC src/png.c src/headless.c)
add_library(axion::headless ALIAS ax_headless)
target_include_directories(ax_headless
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_headless PUBLIC ax_runtime ax_render_soft ax_core ax_render_ir ax_base)
ax_target_defaults(ax_headless)
if(AX_BUILD_GPU)
    target_sources(ax_headless PRIVATE src/gpu.c)
    target_link_libraries(ax_headless PUBLIC ax_render_wgpu)
else()
    target_sources(ax_headless PRIVATE src/no_gpu.c)
endif()
