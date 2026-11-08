/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (builds with the platform module): ax_render_soft behind the renderer interface,
 * presenting into windows through the platform's CPU buffers (axion/platform/ax_present_cpu.h,
 * wl_shm on Wayland), without knowing the windowing system (decision 84). The last resort when no
 * GPU renderer can run.
 *
 *     const ax_renderer_vtbl *vt = ax_render_soft_vtbl();
 *     vt->create(&desc, &self);          // desc.next may chain an ax_render_soft_desc
 *     vt->attach_surface(self, surface);
 *     for (;;) { vt->wait_frame(...); vt->render(self, surface, newest_ir, info); }
 *
 * render() follows the interface's rules: a configure is acknowledged only with a snapshot of its
 * logical size (else AX_FRAME_CONTENT_MISMATCH) unless AX_FRAME_ACCEPT_ANY_SIZE. When every CPU
 * buffer is still held by the compositor the frame is skipped (AX_FRAME_NOTHING) and retried at
 * the next one.
 */
#ifndef AX_RENDER_SOFT_SURFACE_H
#define AX_RENDER_SOFT_SURFACE_H

#include "ax_render_soft.h"
#include <axion/render/ax_renderer.h>

AX_EXTERN_C_BEGIN

/* The renderer's table. self is an ax_render_soft. thread: any (the table is static) */
AX_API const ax_renderer_vtbl *ax_render_soft_vtbl(void);

typedef struct ax_render_soft_surface_stats {
    uint32_t struct_size;              /* set by the caller */
    uint64_t frames;                   /* frames presented */
    uint64_t attach_to_first_frame_ns; /* first frame presented */
    uint64_t content_mismatches;       /* render() calls that waited for the new size */
    uint64_t accepted_any_size;        /* configures acknowledged with another size */
    uint64_t buffers_busy;             /* frames skipped: every buffer held by the compositor */
} ax_render_soft_surface_stats;

/* thread: presenter */
AX_API void ax_render_soft_get_surface_stats(ax_render_soft *r, ax_surface surface,
                                             ax_render_soft_surface_stats *out);

AX_EXTERN_C_END

#endif
