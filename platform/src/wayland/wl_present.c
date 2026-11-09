/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: the presenter side of a surface (decision 34).
 *
 * Everything here runs on the surface's presenter thread, except ax_wl_surface_native (any
 * thread) and ax_wl_present_window_destroyed (main thread, after presenting stopped). Frame
 * callbacks and presentation feedback are created through proxy wrappers bound to the window's
 * private queue, and their listeners run when the presenter dispatches that queue.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <string.h>

#include "presentation-time-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wl_internal.h"
#include "xdg-shell-client-protocol.h"

void ax_wl_surface_native(ax_window_impl *w, ax_surface_native *out)
{
    wl_window *ww = wl_window_of(w);
    out->kind = AX_NATIVE_WAYLAND;
    out->u.wayland.display = wl_app_of(w->app)->display;
    out->u.wayland.surface = ww->surface;
    out->u.wayland.queue = ww->queue;
}

/* ---- frame callbacks ---------------------------------------------------------------------------
 */

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms)
{
    ax_window_impl *w = (ax_window_impl *)data;
    wl_window *ww = wl_window_of(w);
    (void)time_ms;
    wl_callback_destroy(cb);
    ww->frame_callback = NULL;
    ww->frame_done = true;
    if (wl_app_of(w->app)->presentation == NULL) {
        /* No presentation-time: the callback is the best estimate of when the last frame was
         * shown (it fires when the compositor is ready for the next one). */
        uint64_t now = ax_time_monotonic_ns();
        ax_frame_clock_presented(&w->clock, now, 0, 0, false, 0, ww->frame_callback_commit);
        ax__surface_commit_presented(w, ww->frame_callback_commit, now);
    }
}

static const struct wl_callback_listener frame_listener = {frame_done};

/* ---- presentation feedback ------------------------------------------------------------------ */

static void release_record(wl_frame_record *rec)
{
    wp_presentation_feedback_destroy(rec->feedback);
    rec->feedback = NULL;
    rec->in_use = false;
}

static void feedback_sync_output(void *data, struct wp_presentation_feedback *fb,
                                 struct wl_output *output)
{
    (void)data;
    (void)fb;
    (void)output;
}

static void feedback_presented(void *data, struct wp_presentation_feedback *fb, uint32_t tv_sec_hi,
                               uint32_t tv_sec_lo, uint32_t tv_nsec, uint32_t refresh,
                               uint32_t seq_hi, uint32_t seq_lo, uint32_t flags)
{
    wl_frame_record *rec = (wl_frame_record *)data;
    ax_window_impl *w = rec->window;
    uint64_t sec = ((uint64_t)tv_sec_hi << 32) | tv_sec_lo;
    uint64_t msc = ((uint64_t)seq_hi << 32) | seq_lo;
    uint64_t t = ax_wl_presentation_to_monotonic(wl_app_of(w->app), sec, tv_nsec);
    (void)fb;
    (void)flags;
    /* A zero counter means the output has none (the protocol's convention). */
    ax_frame_clock_presented(&w->clock, t, refresh, msc, msc != 0, rec->configure_ns,
                             rec->commit_id);
    ax__surface_commit_presented(w, rec->commit_id, t);
    release_record(rec);
}

static void feedback_discarded(void *data, struct wp_presentation_feedback *fb)
{
    wl_frame_record *rec = (wl_frame_record *)data;
    (void)fb;
    ax_frame_clock_discarded(&rec->window->clock);
    release_record(rec);
}

static const struct wp_presentation_feedback_listener feedback_listener = {
    feedback_sync_output, feedback_presented, feedback_discarded};

/* ---- commit preparation --------------------------------------------------------------------- */

static int32_t logical_px(float v)
{
    int32_t i = (int32_t)lroundf(v);
    return i < 1 ? 1 : i;
}

ax_result ax_wl_surface_prepare_commit(ax_window_impl *w, const ax_frame_target *frame)
{
    wl_window *ww = wl_window_of(w);
    int i;

    ww->prepared_callback = NULL;
    ww->prepared_record = NULL;

    /* 1. Acknowledge the configure this frame was drawn for. It applies with the caller's next
     *    commit, together with the buffer of the configured size. Through the surface's role:
     *    only an xdg surface has xdg_surface.ack_configure (decision 72). */
    if (frame->acks_configure) {
        ww->role->ack_configure(ww, frame->configure_serial);
    }

    /* 2. Map the buffer (pixel_width x pixel_height) onto the logical size. With a viewport any
     *    scale works, fractional included, and the buffer scale stays 1; without one, an integer
     *    buffer scale. */
    if (ww->viewport != NULL) {
        wp_viewport_set_destination(ww->viewport, logical_px(frame->logical_width),
                                    logical_px(frame->logical_height));
    } else {
        int scale = (int)ceilf(frame->scale);
        if (scale < 1) {
            scale = 1;
        }
        if (scale != ww->applied_buffer_scale) {
            wl_surface_set_buffer_scale(ww->surface, scale);
            ww->applied_buffer_scale = scale;
        }
    }

    /* 3. Ask when to draw next (one frame callback at a time). */
    if (ww->frame_callback == NULL) {
        ww->frame_callback = wl_surface_frame(ww->surface_wrapper);
        ww->frame_callback_commit = w->presenter_commit_id + 1u; /* this commit, if made */
        wl_callback_add_listener(ww->frame_callback, &frame_listener, w);
        ww->prepared_callback = ww->frame_callback;
    }

    /* 4. Ask when this frame reaches the screen. */
    if (ww->presentation_wrapper != NULL) {
        for (i = 0; i < AX_WL_MAX_FRAMES_IN_FLIGHT; ++i) {
            wl_frame_record *rec = &ww->frames[i];
            if (!rec->in_use) {
                rec->in_use = true;
                rec->window = w;
                rec->configure_ns = frame->acks_configure ? frame->configure_time_ns : 0;
                rec->commit_id = w->presenter_commit_id + 1u; /* this commit, if made */
                rec->feedback = wp_presentation_feedback(ww->presentation_wrapper, ww->surface);
                wp_presentation_feedback_add_listener(rec->feedback, &feedback_listener, rec);
                ww->prepared_record = rec;
                break;
            }
        }
        /* All records busy (the compositor is far behind): this frame goes unmeasured. */
    }
    return AX_OK;
}

void ax_wl_surface_dispatch(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    wl_display_dispatch_queue_pending(wl_app_of(w->app)->display, ww->queue);
}

bool ax_wl_surface_frame_due(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    if (ww->frame_done) {
        ww->frame_done = false;
        return true;
    }
    return false;
}

bool ax_wl_surface_frame_pending(ax_window_impl *w)
{
    return wl_window_of(w)->frame_callback != NULL;
}

/*
 * The caller's commit did not happen (decision 47). The requests prepare_commit queued (frame
 * callback, feedback) sit in the surface's pending state and would only take effect with the next
 * commit, which requests its own; their proxies are destroyed so nothing waits for them. Events
 * the compositor may still send to them are dropped by libwayland. The configure acknowledgement
 * is not undone: it already applies with the next commit.
 */
void ax_wl_surface_commit_failed(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    if (ww->prepared_callback != NULL && ww->prepared_callback == ww->frame_callback) {
        wl_callback_destroy(ww->frame_callback);
        ww->frame_callback = NULL;
    }
    if (ww->prepared_record != NULL && ww->prepared_record->in_use) {
        release_record(ww->prepared_record);
    }
    ww->prepared_callback = NULL;
    ww->prepared_record = NULL;
}

void ax_wl_surface_drop_frame(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    int i;
    if (ww->frame_callback != NULL) {
        wl_callback_destroy(ww->frame_callback);
        ww->frame_callback = NULL;
    }
    for (i = 0; i < AX_WL_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ww->frames[i].in_use) {
            release_record(&ww->frames[i]);
        }
    }
    ww->prepared_callback = NULL;
    ww->prepared_record = NULL;
}

void ax_wl_present_window_destroyed(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    int i;
    ax_wl_cpu_release(w);
    if (ww->frame_callback != NULL) {
        wl_callback_destroy(ww->frame_callback);
        ww->frame_callback = NULL;
    }
    for (i = 0; i < AX_WL_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ww->frames[i].in_use) {
            release_record(&ww->frames[i]);
        }
    }
}

uint32_t ax_wl_surface_commits_in_flight(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    uint32_t n = 0;
    int i;
    if (ww->presentation_wrapper == NULL) {
        return ww->frame_callback != NULL ? 1u : 0u;
    }
    /* Every commit gets a record while fewer than AX_WL_MAX_FRAMES_IN_FLIGHT are outstanding,
     * far more than any swapchain queues. */
    for (i = 0; i < AX_WL_MAX_FRAMES_IN_FLIGHT; ++i) {
        n += ww->frames[i].in_use ? 1u : 0u;
    }
    return n;
}
