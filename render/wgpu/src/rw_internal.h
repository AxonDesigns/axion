/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_wgpu internals shared by its source files.
 */
#ifndef AX_RENDER_WGPU_INTERNAL_H
#define AX_RENDER_WGPU_INTERNAL_H

#include <stddef.h>

#include <axion/base/ax_arena.h>
#include <axion/gpu/ax_gpu_upload.h>
#include <axion/render/ax_render_wgpu.h>

/* The embedded shader (generated from shaders/rect.wgsl by CMake). */
extern const char ax__rw_rect_wgsl[];
extern const size_t ax__rw_rect_wgsl_size;

/* One instance: 6 x vec4f, matching rect.wgsl's Instance. */
typedef struct rw_instance {
    float rect[4];  /* x, y, w, h (local) */
    float radii[4]; /* tl, tr, br, bl (clamped) */
    float color[4]; /* sRGB-encoded, premultiplied */
    float xform[4]; /* local -> clip space, linear part a, b, c, d */
    float xlate[4]; /* local -> clip space translation tx, ty; z = flags */
    float inv[4];   /* device pixel -> local, linear part a, b, c, d */
} rw_instance;

#define RW_FLAG_ROUNDED                    1u
#define RW_FLAG_FOOTPRINT_FROM_DERIVATIVES 2u

enum { RW_MAX_PIPELINES = 4, RW_MAX_SURFACES = 8 };

typedef struct rw_pipeline {
    ax_render_wgpu *owner;
    WGPUTextureFormat format;
    WGPURenderPipeline pipeline; /* NULL until ready */
    uint32_t generation;         /* device generation of the pipeline or of the pending request */
    bool pending;
    bool done; /* the pending request's callback ran */
    WGPUFuture future;
} rw_pipeline;

typedef struct rw_surface rw_surface;

struct ax_render_wgpu {
    const ax_allocator *allocator;
    ax_gpu *gpu;
    bool owns_gpu;
    int pacing;
    int present_mode;
    ax_color initial_background;

    WGPUShaderModule module;
    uint32_t module_generation;
    rw_pipeline pipelines[RW_MAX_PIPELINES];
    uint32_t pipeline_count;
    uint64_t first_request_ns;

    ax_gpu_upload_ring *ring;
    uint32_t ring_generation;
    ax_arena scratch; /* per-frame encoding memory */

    uint32_t test_flags; /* RW_TEST_* (render/wgpu/src/rw_testing.h) */

    rw_surface *surfaces; /* window surfaces (surface.c), NULL without the platform module */
    uint32_t surface_count;

    ax_render_wgpu_stats stats;
};

/* Releases the window surfaces (surface.c; a no-op without the platform module). */
void ax__render_wgpu_destroy_surfaces(ax_render_wgpu *r);

#endif
