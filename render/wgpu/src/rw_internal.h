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

/* One clip record: 4 x vec4f, matching rect.wgsl's Clip (decision 93). */
typedef struct rw_clip {
    float m[4];     /* device pixel -> local, linear part a, b, c, d */
    float t[4];     /* translation tx, ty; z = flags; w = parent record + 1 (0: none) */
    float rect[4];  /* x, y, w, h (local) */
    float radii[4]; /* clamped */
} rw_clip;

/* Clips evaluated per pixel at most, besides the one device-space rect the CPU intersects the
 * axis-aligned rect clips into; the shader's MAX_CLIPS is one more. */
#define RW_MAX_CLIP_CHAIN 4u

#define RW_FLAG_ROUNDED                    1u
#define RW_FLAG_FOOTPRINT_FROM_DERIVATIVES 2u
#define RW_FLAG_GLYPH                      4u /* radii = the glyph's texels; page and LUT row in
                                               * bits 8-15 and 16-23 of the flags */

/* ---- The glyph atlas (atlas.c, decision 101) ---- */

#define RW_ATLAS_SIZE    1024u
#define RW_ATLAS_PAGES   4u
#define RW_ATLAS_SHELVES 96u
#define RW_ATLAS_ENTRIES 8192u
#define RW_ATLAS_TABLE   16384u /* power of two, at least twice the entries */

typedef struct rw_shelf {
    uint32_t y, height, x;
} rw_shelf;

typedef struct rw_atlas_page {
    rw_shelf shelves[RW_ATLAS_SHELVES];
    uint32_t shelf_count;
    uint64_t last_frame;
} rw_atlas_page;

typedef struct rw_atlas_entry {
    uint32_t instance, glyph, bin;
    uint16_t page, x, y, w, h; /* x, y: the bitmap's first texel (inside its border) */
    int16_t left, top;
} rw_atlas_entry;

typedef struct rw_atlas {
    WGPUTexture texture; /* R8Unorm, RW_ATLAS_PAGES layers */
    WGPUTextureView view;
    WGPUTexture lut; /* 256 x AX_IR_TEXT_LUT_BUCKETS, R8Unorm */
    WGPUTextureView lut_view;
    WGPUSampler sampler;
    uint32_t generation;
    uint64_t frame;
    rw_atlas_page pages[RW_ATLAS_PAGES];
    rw_atlas_entry entries[RW_ATLAS_ENTRIES];
    uint32_t entry_count;
    uint32_t table[RW_ATLAS_TABLE]; /* entry index + 1, 0: empty */
    uint64_t uploads, evictions, skipped;
} rw_atlas;

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

    bool linear_blend;   /* AX_RENDER_WGPU_DIAG_LINEAR_BLEND (unsupported, decision 58) */
    uint32_t test_flags; /* RW_TEST_* (render/wgpu/src/rw_testing.h) */

    rw_atlas atlas; /* glyphs (atlas.c) */

    rw_surface *surfaces; /* window surfaces (surface.c), NULL without the platform module */
    uint32_t surface_count;

    ax_render_wgpu_stats stats;
};

/* The atlas (atlas.c): a frame begins (textures for the current device; false when they cannot
 * be made), a glyph's place (uploaded on first use; NULL when it has no bitmap or no room is left;
 * valid until the next call), and the textures freed. */
bool ax__rw_atlas_begin(ax_render_wgpu *r);
const rw_atlas_entry *ax__rw_atlas_glyph(ax_render_wgpu *r, const ax_ir_glyph_source *src,
                                         uint32_t instance, uint32_t glyph, uint32_t bin);
void ax__rw_atlas_free(ax_render_wgpu *r);

/* Releases the window surfaces (surface.c; a no-op without the platform module). */
void ax__render_wgpu_destroy_surfaces(ax_render_wgpu *r);

#endif
