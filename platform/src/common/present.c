/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The presenter side of a surface (axion/platform/ax_present.h). See decision 34.
 */
#include <string.h>

#include "axion/base/ax_assert.h"
#include "platform_internal.h"

static ax_window_impl *presenter_window(ax_surface surface, const char *fn)
{
    ax_window_impl *w = ax__window_from_handle(surface.h);
    if (w == NULL) {
        ax_set_error(AX_ERR_INVALID_HANDLE, "%s: invalid surface", fn);
    }
    return w;
}

ax_result ax_surface_begin_frame(ax_surface surface, ax_frame_target *frame)
{
    ax_window_impl *w;
    const ax_surface_config *c;

    if (frame == NULL || frame->struct_size < sizeof(ax_frame_target)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH,
                            "ax_surface_begin_frame: struct_size must be >= %u",
                            (unsigned)sizeof(ax_frame_target));
    }
    w = presenter_window(surface, "ax_surface_begin_frame");
    if (w == NULL) {
        return AX_ERR_INVALID_HANDLE;
    }

    /* Latch the newest configure; serial 0 means the window was never configured. */
    if (ax_tb_has_new(&w->config_tb)) {
        const ax_surface_config *latest = &w->config[ax_tb_acquire(&w->config_tb)];
        if (latest->serial != 0) {
            w->presenter_config = *latest;
            w->presenter_has_config = true;
        }
    }

    frame->ready = w->presenter_has_config;
    frame->commit_requested = ax_atomic_load_u32(&w->commit_requested, AX_MO_SEQ_CST) != 0;
    if (!frame->ready) {
        frame->acks_configure = false;
        frame->pixel_width = frame->pixel_height = 0;
        frame->logical_width = frame->logical_height = 0.0f;
        frame->scale = 1.0f;
        frame->predicted_present_ns = 0;
        frame->refresh_interval_ns = ax_frame_clock_refresh(&w->clock);
        frame->configure_serial = 0;
        frame->configure_time_ns = 0;
        return AX_OK;
    }

    c = &w->presenter_config;
    frame->acks_configure = c->serial != w->presenter_acked_serial;
    frame->pixel_width = c->pixel_width;
    frame->pixel_height = c->pixel_height;
    frame->logical_width = c->logical_width;
    frame->logical_height = c->logical_height;
    frame->scale = c->scale;
    frame->predicted_present_ns = ax_frame_clock_target(&w->clock, ax_time_monotonic_ns());
    frame->refresh_interval_ns = ax_frame_clock_refresh(&w->clock);
    frame->configure_serial = c->serial;
    frame->configure_time_ns = c->time_ns;
    ax_atomic_store_u32(&w->presenting, 1, AX_MO_RELEASE);
    return AX_OK;
}

ax_result ax_surface_prepare_commit(ax_surface surface, const ax_frame_target *frame)
{
    ax_window_impl *w;
    ax_result r;
    bool request;
    bool frame_was_pending;

    if (frame == NULL || frame->struct_size < sizeof(ax_frame_target)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH,
                            "ax_surface_prepare_commit: struct_size must be >= %u",
                            (unsigned)sizeof(ax_frame_target));
    }
    w = presenter_window(surface, "ax_surface_prepare_commit");
    if (w == NULL) {
        return AX_ERR_INVALID_HANDLE;
    }
    if (!frame->ready) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_surface_prepare_commit: the window is not configured yet");
    }
    /* Cleared before the commit is prepared: state the main thread sent before setting the flag
     * is already on the wire ahead of this commit, so the commit carries it. */
    request = ax_atomic_load_u32(&w->commit_requested, AX_MO_SEQ_CST) != 0;
    ax_atomic_store_u32(&w->commit_requested, 0, AX_MO_SEQ_CST);
    frame_was_pending = w->app->ops->surface_frame_pending(w);
    r = w->app->ops->surface_prepare_commit(w, frame);
    if (r == AX_OK) {
        w->presenter_prepared = true;
        w->presenter_prepared_request = request;
        w->presenter_prev_target_ns = w->clock.last_target_ns;
        ax_frame_clock_committed(&w->clock, frame->predicted_present_ns);
        /* The acknowledgement is on the wire now and applies with the next commit made, so it
         * stays marked even if this commit fails (decision 47). */
        if (frame->acks_configure) {
            w->presenter_acked_serial = frame->configure_serial;
        }
        if (!frame_was_pending && w->app->ops->surface_frame_pending(w)) {
            w->presenter_frame_requested_ns = ax_time_monotonic_ns();
        }
    } else if (request) {
        ax_atomic_store_u32(&w->commit_requested, 1, AX_MO_SEQ_CST);
    }
    ax_atomic_store_u32(&w->presenting, 0, AX_MO_RELEASE);
    return r;
}

ax_result ax_surface_commit_failed(ax_surface surface)
{
    ax_window_impl *w = presenter_window(surface, "ax_surface_commit_failed");
    if (w == NULL) {
        return AX_ERR_INVALID_HANDLE;
    }
    if (!w->presenter_prepared) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_surface_commit_failed: no prepared commit is outstanding");
    }
    w->presenter_prepared = false;
    w->app->ops->surface_commit_failed(w);
    ax_frame_clock_restore_target(&w->clock, w->presenter_prev_target_ns);
    if (w->presenter_prepared_request) {
        ax_atomic_store_u32(&w->commit_requested, 1, AX_MO_SEQ_CST);
    }
    w->presenter_lost_frames++;
    return AX_OK;
}

ax_result ax_surface_wait_commits_applied(ax_surface surface, uint64_t timeout_ns,
                                          bool *out_applied)
{
    ax_window_impl *w = presenter_window(surface, "ax_surface_wait_commits_applied");
    ax_app_impl *app;
    uint64_t deadline;
    bool applied = false;

    if (w == NULL) {
        return AX_ERR_INVALID_HANDLE;
    }
    app = w->app;
    deadline = timeout_ns > UINT64_MAX - ax_time_monotonic_ns()
                   ? UINT64_MAX
                   : ax_time_monotonic_ns() + timeout_ns;
    for (;;) {
        uint64_t gen = ax_atomic_load_u64(&app->wake_gen, AX_MO_SEQ_CST);
        uint64_t now;

        app->ops->surface_dispatch(w);
        now = ax_time_monotonic_ns();
        if (app->ops->surface_commits_in_flight(w) == 0) {
            /* Every commit with our feedback is done. A swapchain may follow each of them with a
             * commit of its own that waits on the barrier the previous one set (Mesa's FIFO path
             * with commit-timing); it is applied at the refresh after that one is shown, so allow
             * two refreshes after the last presentation. */
            uint64_t settled = w->clock.have_last ? w->clock.last_present_ns +
                                                        2 * ax_frame_clock_refresh(&w->clock)
                                                  : 0;
            if (now >= settled) {
                applied = true;
                break;
            }
            if (settled < deadline) {
                /* nothing else will wake us: sleep exactly until then */
                if (now >= deadline) {
                    break;
                }
                ax_mutex_lock(&app->wake_lock);
                ax_cond_wait_timeout(&app->wake_cond, &app->wake_lock, settled - now);
                ax_mutex_unlock(&app->wake_lock);
                continue;
            }
        }
        if (now >= deadline) {
            break;
        }
        /* Same handshake as ax_surface_wait_frame: sleep until the main thread reads events. */
        ax_mutex_lock(&app->wake_lock);
        ax_atomic_fetch_add_u32(&app->presenters_waiting, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&app->wake_gen, AX_MO_SEQ_CST) == gen) {
            ax_cond_wait_timeout(&app->wake_cond, &app->wake_lock, deadline - now);
        }
        ax_atomic_fetch_add_u32(&app->presenters_waiting, (uint32_t)-1, AX_MO_SEQ_CST);
        ax_mutex_unlock(&app->wake_lock);
    }
    if (out_applied != NULL) {
        *out_applied = applied;
    }
    return AX_OK;
}

/* How long an outstanding frame callback may take before the frame is presumed lost: long enough
 * for any compositor to answer a visible window, short enough that a commit lost without an error
 * (decision 47) only stalls the presenter briefly. */
static uint64_t frame_lost_bound(const ax_frame_clock *clock)
{
    uint64_t bound = 8 * ax_frame_clock_refresh(clock);
    return bound < 50000000u ? 50000000u : (bound > 250000000u ? 250000000u : bound);
}

ax_result ax_surface_wait_frame(ax_surface surface, uint64_t timeout_ns, ax_wake_reason *out_reason)
{
    ax_window_impl *w = presenter_window(surface, "ax_surface_wait_frame");
    ax_app_impl *app;
    uint64_t deadline;
    ax_wake_reason reason = AX_WAKE_TIMEOUT;

    if (w == NULL) {
        return AX_ERR_INVALID_HANDLE;
    }
    app = w->app;
    deadline = timeout_ns > UINT64_MAX - ax_time_monotonic_ns()
                   ? UINT64_MAX
                   : ax_time_monotonic_ns() + timeout_ns;
    for (;;) {
        uint64_t gen = ax_atomic_load_u64(&app->wake_gen, AX_MO_SEQ_CST);
        uint64_t now;
        uint64_t sleep_until = deadline;
        bool configure;
        bool commit;

        /* Run the presenter queue's listeners (frame done, presentation feedback) here, on the
         * presenter thread, where they were registered. */
        app->ops->surface_dispatch(w);
        now = ax_time_monotonic_ns();
        w->presenter_prepared = false; /* the caller made its commit (or never will) */
        if (app->ops->surface_frame_due(w)) {
            reason = AX_WAKE_FRAME; /* the frame drawn now also picks up any pending configure */
            break;
        }
        /* Only a presenter that waits relies on the callback; one polling with a zero timeout
         * (a GPU swapchain that paces itself) is not stalled by a late one. */
        if (timeout_ns != 0 && app->ops->surface_frame_pending(w) &&
            w->presenter_frame_requested_ns != 0) {
            uint64_t lost_at = w->presenter_frame_requested_ns + frame_lost_bound(&w->clock);
            if (now >= lost_at) {
                /* No callback: the commit never happened, or the window gets no callbacks
                 * (hidden). Draw again; a hidden window then draws at this slow rate. */
                app->ops->surface_drop_frame(w);
                w->presenter_frame_requested_ns = 0;
                w->presenter_lost_frames++;
                reason = AX_WAKE_FRAME;
                break;
            }
            if (lost_at < sleep_until) {
                sleep_until = lost_at;
            }
        }
        configure = ax_tb_has_new(&w->config_tb);
        commit = ax_atomic_load_u32(&w->commit_requested, AX_MO_SEQ_CST) != 0;
        if (configure || commit) {
            /* Commit for it at once only if no frame is in flight. Otherwise wait for the frame
             * callback: committing on every configure of an interactive resize outruns the display,
             * and the compositor just discards the extra frames (decision 40). If the callback does
             * not come within two refreshes (a hidden window gets none), commit anyway so the
             * configure is still acknowledged. */
            uint64_t bound = 2 * ax_frame_clock_refresh(&w->clock);
            bound = bound < 8000000u ? 8000000u : (bound > 100000000u ? 100000000u : bound);
            if (w->presenter_work_seen_ns == 0) {
                w->presenter_work_seen_ns = now;
            }
            if (!app->ops->surface_frame_pending(w) || now - w->presenter_work_seen_ns >= bound) {
                reason = configure ? AX_WAKE_CONFIGURE : AX_WAKE_COMMIT_REQUESTED;
                break;
            }
            if (w->presenter_work_seen_ns + bound < sleep_until) {
                sleep_until = w->presenter_work_seen_ns + bound;
            }
        }
        if (now >= deadline) {
            reason = AX_WAKE_TIMEOUT;
            break;
        }
        /* Sleep until the main thread reads more events or publishes something (Dekker handshake
         * with ax__app_wake_presenters through presenters_waiting and wake_gen). */
        ax_mutex_lock(&app->wake_lock);
        ax_atomic_fetch_add_u32(&app->presenters_waiting, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&app->wake_gen, AX_MO_SEQ_CST) == gen && sleep_until > now) {
            ax_cond_wait_timeout(&app->wake_cond, &app->wake_lock, sleep_until - now);
        }
        ax_atomic_fetch_add_u32(&app->presenters_waiting, (uint32_t)-1, AX_MO_SEQ_CST);
        ax_mutex_unlock(&app->wake_lock);
    }
    if (reason != AX_WAKE_TIMEOUT) {
        w->presenter_work_seen_ns = 0; /* the frame drawn next handles what was waiting */
    }
    if (out_reason != NULL) {
        *out_reason = reason;
    }
    return AX_OK;
}

ax_result ax_surface_get_present_stats(ax_surface surface, ax_present_stats *out, bool reset)
{
    ax_window_impl *w;
    const ax_frame_clock *c;
    if (out == NULL || out->struct_size < sizeof(ax_present_stats)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH,
                            "ax_surface_get_present_stats: struct_size must be >= %u",
                            (unsigned)sizeof(ax_present_stats));
    }
    w = presenter_window(surface, "ax_surface_get_present_stats");
    if (w == NULL) {
        return AX_ERR_INVALID_HANDLE;
    }
    c = &w->clock;
    out->presented = c->presented;
    out->discarded = c->discarded;
    out->missed_vblanks = c->missed_vblanks;
    out->refresh_interval_ns = ax_frame_clock_refresh(c);
    out->mean_interval_ns = c->interval_count != 0 ? c->interval_sum_ns / c->interval_count : 0;
    out->latency_samples = c->latency_count;
    out->mean_latency_ns = c->latency_count != 0 ? c->latency_sum_ns / c->latency_count : 0;
    out->max_latency_ns = c->latency_max_ns;
    out->lost_frames = w->presenter_lost_frames;
    if (reset) {
        ax_frame_clock_reset_stats(&w->clock);
        w->presenter_lost_frames = 0;
    }
    return AX_OK;
}
