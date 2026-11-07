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
    ax_atomic_store_u32(&w->commit_requested, 0, AX_MO_SEQ_CST);
    r = w->app->ops->surface_prepare_commit(w, frame);
    if (r == AX_OK) {
        ax_frame_clock_committed(&w->clock, frame->predicted_present_ns);
        if (frame->acks_configure) {
            w->presenter_acked_serial = frame->configure_serial;
        }
    }
    ax_atomic_store_u32(&w->presenting, 0, AX_MO_RELEASE);
    return r;
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

        /* Run the presenter queue's listeners (frame done, presentation feedback) here, on the
         * presenter thread, where they were registered. */
        app->ops->surface_dispatch(w);
        if (ax_tb_has_new(&w->config_tb)) {
            reason = AX_WAKE_CONFIGURE;
            break;
        }
        if (ax_atomic_load_u32(&w->commit_requested, AX_MO_SEQ_CST) != 0) {
            reason = AX_WAKE_COMMIT_REQUESTED;
            break;
        }
        if (app->ops->surface_frame_due(w)) {
            reason = AX_WAKE_FRAME;
            break;
        }
        now = ax_time_monotonic_ns();
        if (now >= deadline) {
            reason = AX_WAKE_TIMEOUT;
            break;
        }
        /* Sleep until the main thread reads more events or publishes something (Dekker handshake
         * with ax__app_wake_presenters through presenters_waiting and wake_gen). */
        ax_mutex_lock(&app->wake_lock);
        ax_atomic_fetch_add_u32(&app->presenters_waiting, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&app->wake_gen, AX_MO_SEQ_CST) == gen) {
            ax_cond_wait_timeout(&app->wake_cond, &app->wake_lock, deadline - now);
        }
        ax_atomic_fetch_add_u32(&app->presenters_waiting, (uint32_t)-1, AX_MO_SEQ_CST);
        ax_mutex_unlock(&app->wake_lock);
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
    if (reset) {
        ax_frame_clock_reset_stats(&w->clock);
    }
    return AX_OK;
}
