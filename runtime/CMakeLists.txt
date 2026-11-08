# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# runtime: runs a world's frame pipeline (decision 77). The scene stage and the single-threaded
# scheduler (ax_host_frame) need only core and the Render IR, so they build everywhere, the web
# included; surfaces and renderers are reached through the presenter interface
# (ax_runtime_sdk.h).
#
# Headers:
#   include/axion/            stable C ABI (ax_runtime.h)
#   include/axion/runtime/    internal SDK

add_library(ax_runtime STATIC
    src/runtime.c
    src/threads.c)
add_library(axion::runtime ALIAS ax_runtime)
target_include_directories(ax_runtime
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_runtime PUBLIC ax_core ax_render_ir ax_base)
ax_target_defaults(ax_runtime)
