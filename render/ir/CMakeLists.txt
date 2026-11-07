# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_ir: the Render IR, the contract between the scene and every renderer (decision 51).
# Depends on base only, so any renderer (in-tree or a plugin) can consume it.
#
# Headers: internal SDK for now (include/axion/render/), designed as the future contract.

add_library(ax_render_ir STATIC src/ir.c src/serialize.c src/dump.c src/mailbox.c)
add_library(axion::render_ir ALIAS ax_render_ir)
target_include_directories(ax_render_ir
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_render_ir PUBLIC ax_base)
ax_target_defaults(ax_render_ir)
