# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# core: the retained node tree, properties and transactions (layout, animation, gestures and the
# rest arrive with their milestones). Depends on base and the Render IR (render/ir).
#
# Headers:
#   include/axion/        stable C ABI (nodes, property values, transactions)
#   include/axion/core/   internal SDK (node store, ...), never part of the ABI

add_library(ax_core STATIC
    src/anim/curves.c
    src/apply.c
    src/events.c
    src/frame.c
    src/layout/layout.c
    src/layout/layout_flex.c
    src/mirror.c
    src/node_store.c
    src/prop_registry.c
    src/scene.c
    src/txn.c
    src/world.c)
add_library(axion::core ALIAS ax_core)

target_include_directories(ax_core
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# The scene records the Render IR (decision 76): core reads only ax_render_ir.h and the mailbox,
# never the renderer interface (ax_renderer.h), which pulls in the platform module.
target_link_libraries(ax_core PUBLIC ax_base ax_render_ir)
ax_target_defaults(ax_core)
