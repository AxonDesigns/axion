/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: ax_render_wgpu, the default renderer, on ax_gpu (WebGPU).
 *
 * Rects and rounded rects are instanced quads whose fragment shader computes each pixel's exact
 * area coverage of the shape's edges from the pixel's footprint in the shape's local space, which
 * the CPU derives from the inverse of the instance's affine transform (decision 55). Colors are
 * encoded to sRGB per instance and blended in sRGB space with premultiplied alpha (decision 53).
 *
 * This header is the part that needs no window: creating the renderer and drawing a snapshot
 * into any texture (used by the coverage tests). Presenting into windows goes through the
 * ax_renderer_vtbl from axion/render/ax_render_wgpu_surface.h.
 *
 * Thread: everything runs on the renderer's ax_gpu thread ("presenter").
 */
#ifndef AX_RENDER_WGPU_H
#define AX_RENDER_WGPU_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/gpu/ax_gpu.h>

#include <axion/render/ax_render_ir.h>
#include <axion/render/ax_renderer_types.h>

AX_EXTERN_C_BEGIN

typedef struct ax_render_wgpu ax_render_wgpu;

/* Options, chained from ax_renderer_desc.next (type AX_RENDERER_CHAIN_WGPU) or passed to
 * ax_render_wgpu_create. */
typedef struct ax_render_wgpu_desc {
    ax_renderer_chain chain; /* type AX_RENDERER_CHAIN_WGPU when chained */
    /* An existing ax_gpu to render with (not owned), or NULL to create one with `adapter`. */
    ax_gpu *gpu;
    ax_gpu_adapter_kind adapter;
    int pacing;                /* ax_gpu_pacing for attached surfaces; 0 = the default */
    int present_mode;          /* WGPUPresentMode for attached surfaces; 0 = the pacing's own */
    uint32_t diagnostic_flags; /* AX_RENDER_WGPU_DIAG_*; 0 in anything but a diagnostic */
} ax_render_wgpu_desc;

/*
 * Diagnostic options. UNSUPPORTED: for comparing renderings during development only. They may
 * change meaning or disappear in any version, nothing outside this module's own tests may rely on
 * them, and an application must never set them.
 *
 * AX_RENDER_WGPU_DIAG_LINEAR_BLEND: blend in linear light instead of sRGB space (decision 58).
 * Colors are uploaded in linear light; window surfaces render through the swapchain's sRGB view
 * (ax_gpu_surface_desc.srgb_view), and ax_render_wgpu_draw_ir expects an sRGB-format target.
 */
#define AX_RENDER_WGPU_DIAG_LINEAR_BLEND 0x1u

typedef struct ax_render_wgpu_stats {
    uint32_t struct_size;       /* set by the caller */
    uint64_t frames;            /* frames presented with content */
    uint64_t clear_only_frames; /* frames presented with the background only */
    uint64_t pipelines_created; /* asynchronous creations completed (each device generation) */
    uint64_t pipeline_ready_ns; /* from requesting the first pipeline to its completion */
    uint64_t encode_ns_total, encode_ns_max; /* CPU time turning snapshots into instances */
    uint32_t instances_last;
} ax_render_wgpu_stats;

typedef struct ax_render_wgpu_draw {
    uint32_t struct_size; /* set by the caller */
    WGPUTextureView target;
    WGPUTextureFormat format; /* the view's format: 8-bit unorm (sRGB-encoded content), or its
                               * sRGB counterpart with AX_RENDER_WGPU_DIAG_LINEAR_BLEND */
    uint32_t width, height;   /* pixels */
    float scale;              /* device pixels per logical unit */
    ax_color background;      /* used when ir is NULL */
} ax_render_wgpu_draw;

/* thread: presenter */
AX_API ax_result ax_render_wgpu_create(const ax_allocator *allocator,
                                       const ax_render_wgpu_desc *desc, ax_render_wgpu **out);
AX_API void ax_render_wgpu_destroy(ax_render_wgpu *r);

/* The ax_gpu it renders with. thread: presenter */
AX_API ax_gpu *ax_render_wgpu_gpu(ax_render_wgpu *r);

/* Starts creating the pipeline for a target format, without waiting (once per format and device
 * generation). thread: presenter */
AX_API void ax_render_wgpu_prepare(ax_render_wgpu *r, WGPUTextureFormat format);

/* True once the pipeline for `format` exists on the current device. thread: presenter */
AX_API bool ax_render_wgpu_ready(ax_render_wgpu *r, WGPUTextureFormat format);

/*
 * Encodes and submits one snapshot into draw->target, cleared to the snapshot's background (or
 * draw->background when ir is NULL). If the pipeline for the format is not ready yet, only the
 * clear is submitted and *out_status is AX_FRAME_CLEARED, else AX_FRAME_PRESENTED. out_status and
 * out_counts may be NULL.
 * Errors: AX_ERR_ABI_MISMATCH (another IR major), AX_ERR_OUT_OF_MEMORY.
 * thread: presenter
 */
AX_API ax_result ax_render_wgpu_draw_ir(ax_render_wgpu *r, const ax_render_wgpu_draw *draw,
                                        const ax_render_ir *ir, ax_frame_status *out_status,
                                        ax_render_counts *out_counts);

/* thread: presenter */
AX_API void ax_render_wgpu_get_stats(ax_render_wgpu *r, ax_render_wgpu_stats *out, bool reset);

AX_EXTERN_C_END

#endif /* AX_RENDER_WGPU_H */
