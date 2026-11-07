# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# core: the retained node tree, properties and transactions (layout, animation, gestures and the
# rest arrive with their milestones). Depends on base only.
#
# Headers:
#   include/axion/        stable C ABI (nodes, property values, transactions)
#   include/axion/core/   internal SDK (node store, ...), never part of the ABI

add_library(ax_core STATIC
    src/node_store.c
    src/prop_registry.c
    src/scene.c
    src/txn.c
    src/world.c)
add_library(axion::core ALIAS ax_core)

target_include_directories(ax_core
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_core PUBLIC ax_base)
ax_target_defaults(ax_core)
