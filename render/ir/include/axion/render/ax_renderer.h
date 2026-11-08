/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (for now): the renderer interface. Every renderer (ax_render_wgpu, and later
 * render_gl, ax_render_soft and plugins) turns Render IR snapshots into pixels behind this table.
 *
 * Threads: a renderer and the surfaces attached to it belong to one thread, the render thread
 * ("presenter" in the platform's thread tags), which is also each attached surface's presenter.
 *
 * One frame on the render thread:
 *
 *     vtbl->wait_frame(self, surface, animating, timeout, &reason);
 *     ir = newest snapshot (ax_ir_mailbox_acquire);
 *     vtbl->render(self, surface, ir, info);   // info.result tells what happened
 *
 * render() latches the surface's newest configure, and presents `ir` drawn at the surface's
 * scale, never stretched. When that configure has another logical size than `ir`, nothing is
 * acknowledged (AX_FRAME_CONTENT_MISMATCH) unless info.flags has AX_FRAME_ACCEPT_ANY_SIZE: the
 * caller then asks the scene for a snapshot of the new size and calls again (decision 54).
 */
#ifndef AX_RENDER_RENDERER_H
#define AX_RENDER_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_surface.h>
#include <axion/platform/ax_present.h>

#include "ax_render_ir.h"
#include "ax_renderer_types.h"

AX_EXTERN_C_BEGIN

typedef struct ax_renderer_desc {
    uint32_t struct_size;          /* sizeof(ax_renderer_desc) */
    const ax_renderer_chain *next; /* renderer-specific options */
    const ax_allocator *allocator; /* NULL: ax_default_allocator() */
    ax_color initial_background;   /* fills frames presented before the first snapshot */
} ax_renderer_desc;

/* ax_renderer_caps.flags */
#define AX_RENDERER_CAP_COMPUTE  0x1u
#define AX_RENDERER_CAP_HDR      0x2u
#define AX_RENDERER_CAP_MSAA     0x4u
#define AX_RENDERER_CAP_BACKDROP 0x8u

typedef struct ax_renderer_caps {
    uint32_t struct_size; /* set by the caller */
    uint32_t flags;       /* AX_RENDERER_CAP_* */
    uint32_t max_texture_size;
    uint16_t ir_version_major, ir_version_minor; /* the Render IR version it reads */
} ax_renderer_caps;

/* ax_frame_info.flags */
#define AX_FRAME_ONLY_IF_REQUIRED 0x1u /* draw only to answer a configure or a commit request */
#define AX_FRAME_ACCEPT_ANY_SIZE  0x2u /* acknowledge even with a snapshot of another size */

typedef struct ax_frame_result {
    uint32_t struct_size;   /* set by the caller */
    uint32_t status;        /* ax_frame_status */
    ax_frame_target target; /* the frame render() latched (logical size, scale, present time) */
    ax_render_counts counts;
} ax_frame_result;

typedef struct ax_frame_info {
    uint32_t struct_size;    /* sizeof(ax_frame_info) */
    uint32_t flags;          /* AX_FRAME_* */
    ax_frame_result *result; /* optional */
} ax_frame_info;

/*
 * The document's six entries, then wait_frame, which AXION adds (decision 55): how a render
 * thread waits for its next frame depends on the renderer (the swapchain's pacing, and polling
 * the device so a lost one is recreated).
 */
typedef struct ax_renderer_vtbl {
    uint32_t struct_size; /* sizeof(ax_renderer_vtbl) of the renderer */
    ax_result (*create)(const ax_renderer_desc *desc, void **out_self);
    void (*destroy)(void *self);
    ax_result (*attach_surface)(void *self, ax_surface surface);
    void (*detach_surface)(void *self, ax_surface surface);
    ax_result (*render)(void *self, ax_surface surface, const ax_render_ir *ir, ax_frame_info info);
    void (*query_caps)(void *self, ax_renderer_caps *out_caps);
    ax_result (*wait_frame)(void *self, ax_surface surface, bool animating, uint64_t timeout_ns,
                            ax_wake_reason *out_reason);
    /* Appended in M1 (check struct_size): wait_frame over several attached surfaces, for one
     * render thread presenting them all (ax_surface_wait_frames). out_reasons[i] for
     * surfaces[i]; returns once at least one is due, or after timeout_ns. */
    ax_result (*wait_frames)(void *self, const ax_surface *surfaces, uint32_t count, bool animating,
                             uint64_t timeout_ns, ax_wake_reason *out_reasons);
} ax_renderer_vtbl;

AX_EXTERN_C_END

#endif /* AX_RENDER_RENDERER_H */
