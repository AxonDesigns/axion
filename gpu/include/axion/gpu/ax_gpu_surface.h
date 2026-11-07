/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: presenting an ax_gpu's frames into a window's ax_surface.
 *
 * One ax_gpu_surface per ax_surface, used on the ax_gpu's thread, which is also the surface's
 * presenter (axion/platform/ax_present.h). Each frame:
 *
 *     ax_gpu_frame f = {sizeof f};
 *     ax_gpu_surface_begin_frame(s, &f);       // latch the configure, resize the swapchain,
 * acquire if (f.texture != NULL) { render into f.view (f.width x f.height, for
 * f.target.predicted_present_ns); ax_gpu_surface_present(s, &f);       // ack + viewport + timing
 * requests, then present
 *     }
 *     ax_gpu_surface_wait(s, animating, timeout, &reason);   // until the next frame should start
 *
 * The swapchain is resized in begin_frame, before the texture is acquired, to the exact pixel size
 * of the configure the frame acknowledges, so the acknowledgement and a buffer of that size reach
 * the compositor in the same commit (decision 34). At most one resize per frame, because
 * begin_frame latches only the newest configure.
 */
#ifndef AX_GPU_GPU_SURFACE_H
#define AX_GPU_GPU_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/ax_surface.h>
#include <axion/platform/ax_present.h>

#include "ax_gpu.h"

AX_EXTERN_C_BEGIN

typedef struct ax_gpu_surface ax_gpu_surface;

/* Who sets the frame rate (decision 38, risk 1; decision 49). */
typedef enum ax_gpu_pacing {
    /* Chosen for the adapter's driver (decision 49). */
    AX_GPU_PACING_DEFAULT = 0,
    /* (a) The compositor's frame callbacks and AXION's frame clock pace the presenter, which
     *     sleeps in ax_gpu_surface_wait. The present mode should not block (MAILBOX). */
    AX_GPU_PACING_COMPOSITOR = 1,
    /* (b) The driver paces: a FIFO swapchain blocks in present or acquire, so
     *     ax_gpu_surface_wait does not wait while animating. */
    AX_GPU_PACING_DRIVER = 2
} ax_gpu_pacing;

typedef struct ax_gpu_surface_desc {
    uint32_t struct_size; /* sizeof(ax_gpu_surface_desc) */
    ax_gpu_pacing pacing;
    /* WGPUPresentMode_Undefined: the pacing's own (Mailbox for COMPOSITOR, Fifo for DRIVER). When
     * the surface does not support the mode asked for, the implementation's fallback is used and
     * ax_gpu_surface_get_info reports it. */
    WGPUPresentMode present_mode;
    /* Also allow sRGB views of the swapchain's textures (ax_gpu_surface_info.srgb_view_format),
     * for passes that blend in linear light. Frames render through the unorm view either way. */
    bool srgb_view;
} ax_gpu_surface_desc;

typedef struct ax_gpu_surface_info {
    uint32_t struct_size;         /* set by the caller */
    ax_gpu_pacing pacing;         /* resolved: never DEFAULT */
    WGPUPresentMode present_mode; /* asked for (resolved from Undefined) */
    bool present_mode_supported;  /* the surface lists it; otherwise a fallback is in use */
    WGPUPresentMode effective_present_mode; /* what the swapchain actually uses */
    WGPUTextureFormat format;               /* the swapchain's format */
    /* The format of frame->view: 8-bit unorm. Frames hold sRGB-encoded, premultiplied colors, so
     * blending happens in sRGB space (decision 53). */
    WGPUTextureFormat view_format;
    WGPUTextureFormat srgb_view_format; /* the sRGB view, when asked for; Undefined otherwise */
    uint32_t width, height;             /* current swapchain size */
} ax_gpu_surface_info;

typedef struct ax_gpu_frame {
    uint32_t struct_size; /* set by the caller: sizeof(ax_gpu_frame) */
    /* Set by the caller: acquire a texture only when the frame must be drawn anyway (it
     * acknowledges a configure or answers a commit request). For a presenter with nothing new to
     * show, e.g. after a wait that timed out. */
    bool only_if_required;
    ax_frame_target target;
    /* The texture to render into, NULL when there is nothing to draw (the window is not
     * configured yet, or the frame was skipped). Owned by the ax_gpu_surface; valid until
     * ax_gpu_surface_present. */
    WGPUTexture texture;
    WGPUTextureView view; /* in ax_gpu_surface_info.view_format */
    uint32_t width, height;
    bool resized; /* the swapchain was resized for this frame */
    /* Set by the caller: the logical size the frame's content was recorded for, 0 to accept
     * any. When the frame would acknowledge a configure of another logical size, begin_frame
     * acquires nothing and sets content_mismatch: the configure stays unacknowledged until
     * content of its size exists (decision 54). */
    float content_logical_width, content_logical_height;
    bool content_mismatch;
} ax_gpu_frame;

typedef struct ax_gpu_surface_stats {
    uint32_t struct_size;     /* set by the caller */
    uint64_t frames;          /* frames presented (wgpuSurfacePresent succeeded) */
    uint64_t failed_presents; /* presents that failed (ax_surface_commit_failed reported) */
    uint64_t skipped_frames;  /* begin_frame could not acquire a texture */
    uint64_t resizes;         /* swapchain resizes (wgpuSurfaceConfigure for a new size) */
    uint64_t resize_ns_total, resize_ns_max;
    uint64_t acquire_ns_total, acquire_ns_max; /* wgpuSurfaceGetCurrentTexture */
    uint64_t present_ns_total, present_ns_max; /* wgpuSurfacePresent */
    uint64_t recreations; /* surface recreated after a device loss or a lost surface */
    /* Frames that would acknowledge a configure while older commits could still be queued in the
     * compositor wait for the queue to drain (decision 48); one that does not drain within a
     * short slice is skipped, unacknowledged, and retried on the next frame. */
    uint64_t drain_ns_total, drain_ns_max;
    uint64_t deferred_acks;
} ax_gpu_surface_stats;

/*
 * Creates the WebGPU surface for a window's ax_surface. It is configured on the first frame,
 * when the window's size is known.
 * Errors: AX_ERR_UNSUPPORTED (the surface's windowing system is not supported, or the adapter
 * cannot present to it), AX_ERR_INVALID_HANDLE, AX_ERR_OUT_OF_MEMORY.
 * thread: presenter
 */
AX_API ax_result ax_gpu_surface_create(ax_gpu *gpu, ax_surface surface,
                                       const ax_gpu_surface_desc *desc,
                                       ax_gpu_surface **out_surface);

/* Releases the WebGPU surface. Destroy it before the window. NULL is ignored. thread: presenter */
AX_API void ax_gpu_surface_destroy(ax_gpu_surface *surface);

/*
 * Starts a frame: ax_surface_begin_frame, then (re)configures the swapchain when the pixel size,
 * the device or the surface changed, then acquires the texture. frame->texture is NULL when there
 * is nothing to draw; that is not an error.
 * Errors: AX_ERR_DEVICE_LOST (the device was lost; ax_gpu_poll recovers it), the errors of
 * ax_surface_begin_frame.
 * thread: presenter
 */
AX_API ax_result ax_gpu_surface_begin_frame(ax_gpu_surface *surface, ax_gpu_frame *frame);

/*
 * Presents the frame's texture: ax_surface_prepare_commit, then wgpuSurfacePresent, which makes
 * the commit. If the present fails, ax_surface_commit_failed is reported. Releases the frame's
 * texture either way.
 * thread: presenter
 */
AX_API ax_result ax_gpu_surface_present(ax_gpu_surface *surface, ax_gpu_frame *frame);

/*
 * Waits until the next frame should start (ax_surface_wait_frame), following the pacing:
 * - COMPOSITOR: always waits for the frame callback, a configure, a commit request or the timeout;
 * - DRIVER: while `animating`, returns AX_WAKE_FRAME at once (after dispatching the surface's
 *   events), because the swapchain blocks instead; otherwise waits like COMPOSITOR.
 * Also runs ax_gpu_poll, so a lost device is recreated here.
 * thread: presenter
 */
AX_API ax_result ax_gpu_surface_wait(ax_gpu_surface *surface, bool animating, uint64_t timeout_ns,
                                     ax_wake_reason *out_reason);

/* thread: presenter */
AX_API void ax_gpu_surface_get_info(ax_gpu_surface *surface, ax_gpu_surface_info *out_info);

/* Statistics since the last reset. thread: presenter */
AX_API void ax_gpu_surface_get_stats(ax_gpu_surface *surface, ax_gpu_surface_stats *out_stats,
                                     bool reset);

AX_EXTERN_C_END

#endif
