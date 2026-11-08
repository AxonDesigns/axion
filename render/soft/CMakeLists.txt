# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_soft: the software renderer (decision 81). It needs no window and no GPU, so it builds
# everywhere, the web included. Window presentation (its ax_renderer_vtbl) needs the platform
# module.
#
# Determinism: the same snapshot must give the same bytes on every platform. ax_target_defaults
# builds every AXION target without floating-point contraction (cmake/AxionCompilerOptions.cmake).

add_library(ax_render_soft STATIC
    src/soft.c src/prepare.c src/raster.c src/raster_simd.c src/soft_math.c)
add_library(axion::render_soft ALIAS ax_render_soft)
target_include_directories(ax_render_soft
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_render_soft PUBLIC ax_render_ir ax_base)
ax_target_defaults(ax_render_soft)

# Windows: the ax_renderer_vtbl over the platform's CPU buffers (decision 84).
if(AX_BUILD_PLATFORM)
    target_sources(ax_render_soft PRIVATE src/surface.c)
    target_link_libraries(ax_render_soft PUBLIC ax_platform)
endif()

# The web build's SIMD path needs WebAssembly SIMD128 (every current browser and Node). Without
# it the renderer uses its scalar path, which gives the same bytes.
if(EMSCRIPTEN)
    option(AX_WASM_SIMD "Emscripten: build ax_render_soft's SIMD path with -msimd128" ON)
    if(AX_WASM_SIMD)
        target_compile_options(ax_render_soft PRIVATE "$<$<COMPILE_LANGUAGE:C>:-msimd128>")
    endif()
endif()
