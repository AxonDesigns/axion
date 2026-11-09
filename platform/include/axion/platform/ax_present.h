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

/*
 * Reports that the commit the last ax_surface_prepare_commit prepared was not made (the
 * swapchain's present failed). Call it before the next ax_surface_wait_frame. It forgets the frame
 * callback and presentation feedback that commit asked for, puts the frame clock's target back,
 * and raises the commit request again if that commit was answering one, so the next frame
 * commits anyway.
 * The configure acknowledgement is not sent again: it is already on its way and applies with the
 * next commit made, and acknowledging the same configure twice is a protocol error. The next
 * frame must therefore deliver that configure's size, which it does, since begin_frame latches
 * the newest configure.
 * Errors: AX_ERR_UNSUPPORTED if no prepared commit is outstanding. thread: presenter
 */
AX_API ax_result ax_surface_commit_failed(ax_surface surface);

/*
 * Ends the presenter's frame begun with ax_surface_begin_frame, whether it was committed or not
 * (it drew nothing new at rest, its content had another size, every buffer was busy...). Until
 * then the window may not be destroyed: begin_frame marks the surface as being presented and only
 * a commit or this clears it (decision 95). Harmless after a commit. thread: presenter
 */
AX_API void ax_surface_end_frame(ax_surface surface);

/*
 * Waits until the compositor has reported the fate (presented or discarded) of every commit made
 * so far, and two refreshes have passed since the last presentation (a swapchain may add commits
 * of its own that wait one refresh behind ours), dispatching the surface's queue meanwhile;
 * *out_applied tells whether that happened within the timeout.
 * For swapchains whose commits can wait in a compositor-side queue (FIFO with wp_fifo_v1): a
 * configure must not be acknowledged while older commits are still queued, because some
 * compositors apply the acknowledgement on receipt and then check the older, queued commits
 * against it, a protocol error (decision 48). thread: presenter
 */
AX_API ax_result ax_surface_wait_commits_applied(ax_surface surface, uint64_t timeout_ns,
                                                 bool *out_applied);

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
 * they wake the presenter anyway, so configures are always acknowledged.
 * A frame callback that has not come after eight refreshes (50-250 ms) is given up while waiting
 * (timeout_ns > 0): the frame is counted lost and AX_WAKE_FRAME is returned, so a commit lost
 * without an error cannot stall the presenter. thread: presenter
 */
AX_API ax_result ax_surface_wait_frame(ax_surface surface, uint64_t timeout_ns,
                                       ax_wake_reason *out_reason);

/*
 * The presenter rests: it began a frame and drew nothing, because nothing changed and nothing
 * asked for a commit (decision 95), so it stops presenting a frame per refresh. The gap from the
 * last commit it made to the next one shown is then rest time (ax_present_stats.rest_ns), not
 * missed refreshes. Calling it while it keeps presenting would hide misses. thread: presenter
 */
AX_API void ax_surface_rest(ax_surface surface);

/* Asks the presenter for a frame although nothing on the surface asked for one: the surface's
 * content is about to change while it rests without frames (a transaction was committed; decision
 * 95). Its waits then return AX_WAKE_COMMIT_REQUESTED, as for commit-time state. A stale surface
 * is ignored. thread: any */
AX_API void ax_surface_request_commit(ax_surface surface);

#define AX_SURFACE_WAIT_MAX 64u

/*
 * ax_surface_wait_frame over several surfaces of one app, for a thread presenting all of them
 * (the render thread). Returns as soon as at least one surface has a reason other than
 * AX_WAKE_TIMEOUT, or when timeout_ns passes; out_reasons[i] is surfaces[i]'s reason, with the
 * rules of ax_surface_wait_frame for each. Every surface that is ready is reported, so a frame
 * callback is never consumed without being returned.
 * Errors: AX_ERR_INVALID_ARGUMENT (no surfaces, more than AX_SURFACE_WAIT_MAX, NULL arrays,
 * surfaces of different apps), AX_ERR_INVALID_HANDLE. thread: presenter of every surface
 */
AX_API ax_result ax_surface_wait_frames(const ax_surface *surfaces, uint32_t count,
                                        uint64_t timeout_ns, ax_wake_reason *out_reasons);

/*
 * Commit ids. Each successful ax_surface_prepare_commit gives the commit it prepares the next id,
 * from 1; ax_surface_prepared_commit returns the last one (0 before any). The caller keeps what
 * it drew with each id, and ax_surface_get_presented_commit tells which commit reached the screen
 * last and when (the monotonic clock): measured by presentation feedback where the system has it,
 * else estimated by the frame callback. Ids only move forward; a commit that failed
 * (ax_surface_commit_failed) or was discarded is never reported, and a later one shown covers it.
 * Both: thread: presenter
 */
AX_API uint64_t ax_surface_prepared_commit(ax_surface surface);
AX_API ax_result ax_surface_get_presented_commit(ax_surface surface, uint64_t *out_commit,
                                                 uint64_t *out_present_ns);

typedef struct ax_present_stats {
    uint32_t struct_size;         /* set by the caller: sizeof(ax_present_stats) */
    uint64_t presented;           /* frames reported on screen */
    uint64_t discarded;           /* frames the system dropped without showing */
    uint64_t missed_vblanks;      /* refresh cycles skipped between consecutive presented frames,
                                   * except across a rest */
    uint64_t refresh_interval_ns; /* measured */
    uint64_t mean_interval_ns;    /* mean time between consecutive presented frames */
    uint64_t latency_samples;     /* presented frames that acknowledged a configure */
    uint64_t mean_latency_ns;     /* configure received -> that size on screen */
    uint64_t max_latency_ns;
    uint64_t lost_frames; /* commits reported failed, plus frame callbacks that never came */
    uint64_t rests;       /* gaps between presented frames that spanned a rest (ax_surface_rest) */
    uint64_t rest_ns;     /* their total length, left out of missed_vblanks and the intervals */
} ax_present_stats;

/* Statistics since the last reset. thread: presenter */
AX_API ax_result ax_surface_get_present_stats(ax_surface surface, ax_present_stats *out_stats,
                                              bool reset);

AX_EXTERN_C_END

#endif /* AX_PLATFORM_PRESENT_H */
