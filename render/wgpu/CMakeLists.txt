# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_wgpu: the default renderer, on ax_gpu (decision 55). Its WGSL lives in shaders/ here
# and is embedded into the build; the repository's top-level shaders/ is for the AXGL compiler.
#
# Headers: internal SDK (include/axion/render/). Window surfaces need the platform module; without
# it the renderer still draws into textures (the macOS offscreen tests).

set(_rw_wgsl "${CMAKE_CURRENT_SOURCE_DIR}/shaders/rect.wgsl")
set(_rw_wgsl_c "${CMAKE_CURRENT_BINARY_DIR}/generated/rect_wgsl.c")
add_custom_command(
    OUTPUT "${_rw_wgsl_c}"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${_rw_wgsl} -DOUTPUT=${_rw_wgsl_c} -DSYMBOL=ax__rw_rect_wgsl
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_wgsl.cmake"
    DEPENDS "${_rw_wgsl}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_wgsl.cmake"
    COMMENT "Embedding rect.wgsl"
    VERBATIM)

add_library(ax_render_wgpu STATIC src/renderer.c "${_rw_wgsl_c}")
add_library(axion::render_wgpu ALIAS ax_render_wgpu)
target_include_directories(ax_render_wgpu
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_render_wgpu PUBLIC ax_render_ir ax_gpu)
ax_target_defaults(ax_render_wgpu)

if(AX_BUILD_PLATFORM)
    target_sources(ax_render_wgpu PRIVATE src/surface.c)
else()
    target_sources(ax_render_wgpu PRIVATE src/no_surface.c)
endif()
