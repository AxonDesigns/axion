# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# gpu: the WebGPU layer. Depends on base and, for window surfaces, on platform's internal SDK.
#
# Headers: everything is internal SDK for now (include/axion/gpu/); no stable ABI header exposes
# webgpu.h types until hosted textures arrive.

include(AxionWebGPU)

if(AX_WEBGPU STREQUAL "dawn")
    set(_ax_webgpu_impl src/impl_dawn.c)
else()
    set(_ax_webgpu_impl src/impl_wgpu_native.c)
endif()

add_library(ax_gpu STATIC src/gpu.c ${_ax_webgpu_impl})
add_library(axion::gpu ALIAS ax_gpu)
target_include_directories(ax_gpu
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_gpu PUBLIC ax_base ax_webgpu)
ax_target_defaults(ax_gpu)

# Window surfaces need the platform module (Linux only for now). Without it gpu/ still renders
# offscreen, which is what the macOS build tests.
if(AX_BUILD_PLATFORM)
    target_sources(ax_gpu PRIVATE src/surface.c)
    target_link_libraries(ax_gpu PUBLIC ax_platform)
endif()
