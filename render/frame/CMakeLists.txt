# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_frame: one render-thread frame for one surface, with the size handshake (decisions 54
# and 57). M0 scaffolding shared by examples/m0_window and the presentation tests; the core's
# render thread takes it over in M1. Needs the platform module.

add_library(ax_render_frame STATIC src/frame.c)
add_library(axion::render_frame ALIAS ax_render_frame)
target_include_directories(ax_render_frame
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(ax_render_frame PUBLIC ax_render_ir ax_platform)
ax_target_defaults(ax_render_frame)
