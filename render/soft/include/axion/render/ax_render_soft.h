/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: ax_render_soft, the software renderer (decision 81). It draws Render IR snapshots
 * on the CPU, in tiles of AX_RENDER_SOFT_TILE pixels, over the job system and with SIMD where the
 * target has it. It is the reference the GPU renderers are tested against, the renderer of the
 * headless host, and the last resort for windows without a usable GPU.
 *
 * Same semantics as ax_render_wgpu (decisions 53 and 55):
 *   - coverage is the exact area of each pixel on the inner side of every edge, at any rotation
 *     and scale, with rounded corners treated as locally straight;
 *   - colors are premultiplied and blend in sRGB space by default (decision 58), or in linear
 *     light for layers with AX_IR_LAYER_LINEAR_BLEND;
 *   - a snapshot is drawn at the target's scale, anchored top-left, never stretched; the
 *     snapshot's background fills the whole target.
 * As the reference it also draws what ax_render_wgpu cannot yet: rect and rounded-rect clips
 * (analytic, nested) and exact group opacity (layers with opacity below 1 and more than one item
 * or a child are composited as isolated groups).
 *
 * Determinism. The same snapshot gives the same bytes on every platform, with any number of
 * threads and with or without SIMD: every pixel is computed from the snapshot alone with IEEE
 * single-precision operations in a fixed order (no fused multiply-add, no transcendental function
 * from libm, values kept away from denormals). See decision 81.
 *
 * This header needs no window. Presenting into windows goes through the ax_renderer_vtbl of
 * axion/render/ax_render_soft_surface.h (built with the platform module).
 *
 * Thread: one thread at a time per renderer; the drawing itself is spread over the job system.
 */
#ifndef AX_RENDER_SOFT_H
#define AX_RENDER_SOFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/base/ax_jobs.h>
#include <axion/render/ax_render_ir.h>
#include <axion/render/ax_renderer_types.h>

AX_EXTERN_C_BEGIN

#define AX_RENDER_SOFT_TILE 64u /* tile side, in pixels */

#define AX_RENDERER_CHAIN_SOFT 2u /* ax_render_soft_desc */

/* ax_render_soft_desc.flags */
#define AX_RENDER_SOFT_SCALAR 0x1u /* never use the SIMD path (tests compare both) */

/*
 * Diagnostic options. UNSUPPORTED, as ax_render_wgpu's (decision 58): for comparing renderings
 * during development only.
 *
 * AX_RENDER_SOFT_DIAG_LINEAR_BLEND: blend every layer in linear light, as
 * AX_RENDER_WGPU_DIAG_LINEAR_BLEND does, so both renderers can be compared in either space.
 */
#define AX_RENDER_SOFT_DIAG_LINEAR_BLEND 0x1u

typedef struct ax_render_soft ax_render_soft;

/* Options, chained from ax_renderer_desc.next (type AX_RENDERER_CHAIN_SOFT) or passed to
 * ax_render_soft_create. */
typedef struct ax_render_soft_desc {
    ax_renderer_chain chain; /* type AX_RENDERER_CHAIN_SOFT when chained */
    /* Tiles run as frame jobs on this job system (not owned). NULL: every tile runs on the calling
     * thread. The result is the same either way. */
    ax_job_system *jobs;
    uint32_t flags;            /* AX_RENDER_SOFT_* */
    uint32_t diagnostic_flags; /* AX_RENDER_SOFT_DIAG_*; 0 in anything but a diagnostic */
} ax_render_soft_desc;

/* Pixel formats of a target: 8 bits per channel, premultiplied alpha, sRGB-encoded. The names give
 * the byte order in memory. */
typedef enum ax_soft_format {
    AX_SOFT_RGBA8 = 0,
    AX_SOFT_BGRA8 = 1 /* also Wayland's ARGB8888 on little-endian hosts */
} ax_soft_format;

typedef struct ax_soft_target {
    uint32_t struct_size; /* sizeof(ax_soft_target) */
    void *pixels;
    uint32_t width, height; /* pixels */
    size_t stride;          /* bytes between rows, at least width * 4 */
    uint32_t format;        /* ax_soft_format */
    float scale;            /* device pixels per logical unit */
    ax_color background;    /* fills the target when ir is NULL */
} ax_soft_target;

typedef struct ax_render_soft_stats {
    uint32_t struct_size; /* set by the caller */
    uint64_t frames;
    uint64_t tiles;
    uint64_t draw_ns_total, draw_ns_max; /* wall time of ax_render_soft_draw_ir */
    uint32_t commands_last;              /* items and group operations of the last frame */
    bool simd;                           /* the SIMD path is in use */
} ax_render_soft_stats;

/* desc may be NULL (defaults). Errors: AX_ERR_OUT_OF_MEMORY. thread: any */
AX_API ax_result ax_render_soft_create(const ax_allocator *allocator,
                                       const ax_render_soft_desc *desc, ax_render_soft **out);
AX_API void ax_render_soft_destroy(ax_render_soft *r);

/*
 * Draws `ir` into the target (or only target->background when ir is NULL). out_counts (may be
 * NULL) gets what could not be drawn as recorded, per the Render IR's "Compatibility" rules.
 * Errors: AX_ERR_ABI_MISMATCH (another IR major), AX_ERR_INVALID_ARGUMENT (target),
 * AX_ERR_OUT_OF_MEMORY.
 */
AX_API ax_result ax_render_soft_draw_ir(ax_render_soft *r, const ax_soft_target *target,
                                        const ax_render_ir *ir, ax_render_counts *out_counts);

AX_API void ax_render_soft_get_stats(ax_render_soft *r, ax_render_soft_stats *out, bool reset);

/* True when this build has a SIMD path (SSE2, NEON or WebAssembly SIMD128). */
AX_API bool ax_render_soft_has_simd(void);

AX_EXTERN_C_END

#endif /* AX_RENDER_SOFT_H */
