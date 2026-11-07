/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (M0 scaffolding): one render-thread frame for one surface, as in steps 1 and 7 of
 * the document's frame pipeline. In M1 the core's render thread takes this over.
 *
 *     ax_render_frame_init(&rf, &desc);
 *     for (;;) ax_render_frame_step(&rf, animating, timeout);
 *
 * Each step:
 *   1. waits for the surface's next frame (vtbl->wait_frame);
 *   2. takes the newest snapshot from the mailbox and renders it (vtbl->render);
 *   3. asks the scene to record the next frame (desc.request): the predicted present time one
 *      refresh after this frame, at this frame's logical size, so the scene works on frame N+1
 *      while the render thread presents frame N.
 *
 * The size handshake (decision 54): when the newest snapshot was recorded for another logical
 * size than the configure being acknowledged, the renderer acknowledges nothing; the step asks the
 * scene for the new size and waits for its next snapshot, then renders again. The wait has a
 * total bound (desc.max_wait_ns, decision 57). When it expires, or at once when the window is
 * hidden (so a hidden window always acknowledges its configures, decision 40), the frame is
 * acknowledged with the newest snapshot as it is: drawn unstretched, anchored top-left, the
 * background filling the rest.
 *
 * thread: the render thread (presenter of the surface).
 */
#ifndef AX_RENDER_FRAME_H
#define AX_RENDER_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/ax_window.h>
#include <axion/render/ax_ir_mailbox.h>
#include <axion/render/ax_renderer.h>

AX_EXTERN_C_BEGIN

/* What the scene should record next. */
typedef struct ax_scene_request {
    float logical_width, logical_height;
    float scale;            /* informational: the scene records in logical units */
    uint64_t frame_time_ns; /* predicted present time to sample animations at */
    uint64_t refresh_interval_ns;
    bool resize; /* a configure waits for this size: record it now */
} ax_scene_request;

typedef void (*ax_scene_request_fn)(void *ctx, const ax_scene_request *request);

typedef struct ax_render_frame_desc {
    uint32_t struct_size; /* sizeof(ax_render_frame_desc) */
    const ax_renderer_vtbl *vtbl;
    void *renderer;
    ax_surface surface;
    ax_window window; /* for its occlusion state */
    ax_ir_mailbox *mailbox;
    ax_scene_request_fn request;
    void *request_ctx;
    /* Total bound of the size handshake's wait per frame; 0 selects the default (decision 57). */
    uint64_t max_wait_ns;
} ax_render_frame_desc;

typedef struct ax_render_frame_stats {
    uint32_t struct_size; /* set by the caller */
    uint64_t presented;   /* frames presented with a snapshot drawn */
    uint64_t cleared;     /* frames presented with the background only */
    uint64_t scene_late;  /* configures that waited for the scene and got a snapshot in time */
    uint64_t forced_acks; /* configures acknowledged with another size after the bound expired */
    uint64_t hidden_acks; /* configures acknowledged at once with another size: window hidden */
    uint64_t wait_ns_total, wait_ns_max; /* time spent waiting for the scene */
    ax_render_counts counts;             /* summed over the frames */
} ax_render_frame_stats;

/* Visible so it can be embedded; the fields are private. */
typedef struct ax_render_frame {
    ax_render_frame_desc desc;
    ax_render_frame_stats stats;
    bool requested_first; /* the scene was asked for a first snapshot */
} ax_render_frame;

/* thread: render */
AX_API void ax_render_frame_init(ax_render_frame *rf, const ax_render_frame_desc *desc);

/*
 * One frame (see the top of this file). `animating`: the scene has motion, so frames keep coming.
 * `out_result` (may be NULL) receives the last render() result of the step.
 * Errors: those of the renderer's wait_frame and render (AX_ERR_DEVICE_LOST while a lost device
 * is recovered: call again).
 * thread: render
 */
AX_API ax_result ax_render_frame_step(ax_render_frame *rf, bool animating, uint64_t timeout_ns,
                                      ax_frame_result *out_result);

/* Statistics since the last reset. thread: render */
AX_API void ax_render_frame_get_stats(ax_render_frame *rf, ax_render_frame_stats *out, bool reset);

/* The bound used when desc.max_wait_ns is 0, for a display refreshing every refresh_ns. */
AX_API uint64_t ax_render_frame_default_max_wait(uint64_t refresh_ns);

AX_EXTERN_C_END

#endif
