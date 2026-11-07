/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: presenting into a surface, for the renderer's thread.
 *
 * One thread at a time presents a given surface (the "presenter"). Each frame:
 *
 *     ax_frame_target t = {sizeof t};
 *     ax_surface_begin_frame(s, &t);          // latest configured size and scale, predicted time
 *     if (t.ready) {
 *         draw at t.pixel_width x t.pixel_height for t.predicted_present_ns;
 *         ax_surface_prepare_commit(s, &t);   // acks the configure, sets scale, asks for timing
 *         attach the buffer and commit;       // the caller's own code, or the GPU swapchain
 *     }
 *     ax_surface_wait_frame(s, timeout, &reason);   // until the next frame should start
 *
 * The rule that keeps resizing exact: a configure is acknowledged in the same commit that
 * delivers a buffer of the configured size, and that commit is made by the presenter. The main
 * thread only records the latest configure and wakes the presenter; it never acknowledges and,
 * after the window's first (empty) commit, never commits the surface.
 *
 * State the main thread changes that only applies on commit (minimum size, for example) also
 * wakes the presenter, with AX_WAKE_COMMIT_REQUESTED, so it commits even when nothing animates.
 */
#ifndef AX_PLATFORM_PRESENT_H
#define AX_PLATFORM_PRESENT_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_surface.h"

AX_EXTERN_C_BEGIN

typedef struct ax_frame_target {
    uint32_t struct_size;  /* set by the caller: sizeof(ax_frame_target) */
    bool ready;            /* false until the system has configured the window */
    bool acks_configure;   /* this frame acknowledges a new configure */
    bool commit_requested; /* the main thread changed state that needs a commit */
    uint32_t pixel_width;  /* render target size in device pixels */
    uint32_t pixel_height;
    float logical_width;
    float logical_height;
    float scale;
    uint64_t predicted_present_ns; /* when this frame is expected on screen (monotonic clock) */
    uint64_t refresh_interval_ns;  /* measured refresh interval of the display */
    uint32_t configure_serial;     /* internal: the configure this frame acknowledges */
    uint64_t configure_time_ns;    /* internal: when that configure arrived */
} ax_frame_target;

/* Latches the latest configure and fills the frame to draw. thread: presenter */
AX_API ax_result ax_surface_begin_frame(ax_surface surface, ax_frame_target *frame);

/*
 * Applies the frame's surface state ahead of the caller's commit: acknowledges the configure the
 * frame latched, sets the buffer scale or viewport, and requests a frame callback and
 * presentation feedback. The caller's next commit (with a buffer of the frame's size) carries it.
 * thread: presenter
 */
AX_API ax_result ax_surface_prepare_commit(ax_surface surface, const ax_frame_target *frame);

typedef enum ax_wake_reason {
    AX_WAKE_FRAME = 1,            /* the system is ready for the next frame */
    AX_WAKE_CONFIGURE = 2,        /* a new configure (size or scale) is waiting */
    AX_WAKE_COMMIT_REQUESTED = 3, /* commit-time state changed; commit even if nothing moves */
    AX_WAKE_TIMEOUT = 4
} ax_wake_reason;

/*
 * Waits until the next frame should start, dispatching the surface's event queue meanwhile:
 *   AX_WAKE_FRAME             the frame callback fired (a pending configure is picked up too);
 *   AX_WAKE_CONFIGURE         a new configure, when no frame is in flight;
 *   AX_WAKE_COMMIT_REQUESTED  commit-time state changed, when no frame is in flight;
 *   AX_WAKE_TIMEOUT           nothing is due.
 * While a frame is in flight, configures and commit requests wait for its callback, so commits
 * never outpace the display; if the callback is late by two refreshes (a hidden window gets none),
 * they wake the presenter anyway, so configures are always acknowledged. thread: presenter
 */
AX_API ax_result ax_surface_wait_frame(ax_surface surface, uint64_t timeout_ns,
                                       ax_wake_reason *out_reason);

typedef struct ax_present_stats {
    uint32_t struct_size;         /* set by the caller: sizeof(ax_present_stats) */
    uint64_t presented;           /* frames reported on screen */
    uint64_t discarded;           /* frames the system dropped without showing */
    uint64_t missed_vblanks;      /* refresh cycles skipped between consecutive presented frames */
    uint64_t refresh_interval_ns; /* measured */
    uint64_t mean_interval_ns;    /* mean time between consecutive presented frames */
    uint64_t latency_samples;     /* presented frames that acknowledged a configure */
    uint64_t mean_latency_ns;     /* configure received -> that size on screen */
    uint64_t max_latency_ns;
} ax_present_stats;

/* Statistics since the last reset. thread: presenter */
AX_API ax_result ax_surface_get_present_stats(ax_surface surface, ax_present_stats *out_stats,
                                              bool reset);

AX_EXTERN_C_END

#endif /* AX_PLATFORM_PRESENT_H */
