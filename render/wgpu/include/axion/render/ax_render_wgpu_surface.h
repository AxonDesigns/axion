/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: ax_render_wgpu behind the renderer interface, presenting into windows.
 *
 *     const ax_renderer_vtbl *vt = ax_render_wgpu_vtbl();
 *     vt->create(&desc, &self);          // on the render thread
 *     vt->attach_surface(self, surface); // starts compiling the pipeline, without waiting
 *     for (;;) { vt->wait_frame(...); vt->render(self, surface, newest_ir, info); }
 *
 * Until the pipeline is ready (and while a lost device is recovered), render() presents frames
 * cleared to the snapshot's background (AX_FRAME_CLEARED), so a window appears at once: on
 * Wayland it is mapped only with its first buffer.
 */
#ifndef AX_RENDER_WGPU_SURFACE_H
#define AX_RENDER_WGPU_SURFACE_H

#include <axion/gpu/ax_gpu_surface.h>

#include "ax_render_wgpu.h"
#include <axion/render/ax_renderer.h>

AX_EXTERN_C_BEGIN

/* The renderer's table. self is an ax_render_wgpu. thread: any (the table is static) */
AX_API const ax_renderer_vtbl *ax_render_wgpu_vtbl(void);

typedef struct ax_render_wgpu_surface_stats {
    uint32_t struct_size;                /* set by the caller */
    uint64_t attach_to_first_frame_ns;   /* first frame presented (cleared or with content) */
    uint64_t attach_to_first_content_ns; /* first frame presented with a snapshot drawn */
    uint64_t content_mismatches; /* render() calls that waited for a snapshot of the new size */
    uint64_t accepted_any_size;  /* frames acknowledged with a snapshot of another size */
} ax_render_wgpu_surface_stats;

/* The ax_gpu_surface behind an attached surface (for its statistics), NULL if not attached.
 * thread: presenter */
AX_API ax_gpu_surface *ax_render_wgpu_gpu_surface(ax_render_wgpu *r, ax_surface surface);

/* thread: presenter */
AX_API void ax_render_wgpu_get_surface_stats(ax_render_wgpu *r, ax_surface surface,
                                             ax_render_wgpu_surface_stats *out);

AX_EXTERN_C_END

#endif
