/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * A commit prepared but never made (the swapchain's present failed): ax_surface_commit_failed and
 * the lost-frame bound in ax_surface_wait_frame (decision 47). The test thread is the main thread
 * and pumps; a scripted presenter thread steps through the frames.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_present.h"
#include "axion/platform/ax_surface_native.h"
#include "common/platform_internal.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

enum {
    STEP_START = 0,
    STEP_FIRST_FRAME_SHOWN, /* presenter -> main: change the configure and request a commit */
    STEP_CHANGES_SENT,      /* main -> presenter */
    STEP_DONE               /* presenter -> main */
};

typedef struct script {
    ax_surface surface;
    bool lose_silently; /* skip ax_surface_commit_failed: only the lost-frame bound can help */
    ax_atomic_u32 step;
    ax_atomic_u32 failures;

    /* results, read by the main thread after STEP_DONE */
    bool failed_frame_acked;     /* the failed frame acknowledged the new configure */
    bool failed_frame_requested; /* it answered the commit request */
    bool target_restored;        /* the frame clock's target went back */
    bool frame_pending_after;    /* a frame callback was still outstanding afterwards */
    bool request_raised_again;   /* the commit request was raised again */
    ax_wake_reason wake_after;   /* what wait_frame returned after the failure */
    uint64_t wake_after_ns;      /* and how long it took */
    bool next_frame_acked;       /* the next frame acknowledged again (must not) */
    bool next_frame_requested;   /* the next frame saw the commit request */
    uint32_t failed_w, failed_h; /* size of the failed frame */
    uint32_t next_w, next_h;     /* size of the frame committed after it */
    bool next_frame_shown;       /* its frame callback came */
    uint64_t lost_frames;
} script;

static void fail(script *s, const char *what)
{
    (void)what;
    ax_atomic_fetch_add_u32(&s->failures, 1, AX_MO_RELAXED);
}

static bool begin(script *s, ax_frame_target *t)
{
    memset(t, 0, sizeof *t);
    t->struct_size = sizeof *t;
    return ax_surface_begin_frame(s->surface, t) == AX_OK;
}

/* Waits for a wake of the given reason, up to `timeout_ns`. */
static bool wait_for(script *s, ax_wake_reason want, uint64_t timeout_ns)
{
    uint64_t end = ax_time_monotonic_ns() + timeout_ns;
    while (ax_time_monotonic_ns() < end) {
        ax_wake_reason r = AX_WAKE_TIMEOUT;
        ax_surface_wait_frame(s->surface, 20u * 1000000u, &r);
        if (r == want) {
            return true;
        }
    }
    return false;
}

static bool wait_step(script *s, uint32_t step)
{
    uint64_t end = ax_time_monotonic_ns() + AXT_SECONDS(5);
    while (ax_atomic_load_u32(&s->step, AX_MO_ACQUIRE) < step) {
        if (ax_time_monotonic_ns() > end) {
            return false;
        }
        ax_thread_yield();
    }
    return true;
}

static int script_main(void *arg)
{
    script *s = (script *)arg;
    ax_window_impl *w = ax__window_from_handle(s->surface.h);
    axt_shm shm;
    ax_surface_native native;
    struct wl_surface *wl_surface;
    ax_frame_target t;
    uint64_t target_before;
    uint64_t t0;
    uint64_t deadline;

    memset(&native, 0, sizeof native);
    native.struct_size = sizeof native;
    if (w == NULL || !axt_shm_init(&shm, s->surface) ||
        ax_surface_get_native(s->surface, &native) != AX_OK) {
        fail(s, "init");
        ax_atomic_store_u32(&s->step, STEP_DONE, AX_MO_RELEASE);
        return 1;
    }
    wl_surface = (struct wl_surface *)native.u.wayland.surface;

    /* 1. The first frame, committed normally and shown. */
    /* Loops are bounded by time, not iterations: a pending commit request makes wait_frame
     * return at once, while the configure needs a compositor roundtrip. */
    deadline = ax_time_monotonic_ns() + AXT_SECONDS(5);
    while (ax_time_monotonic_ns() < deadline) {
        if (!begin(s, &t)) {
            fail(s, "begin");
            break;
        }
        if (t.ready) {
            break;
        }
        ax_surface_wait_frame(s->surface, 20u * 1000000u, NULL);
        ax_thread_yield();
    }
    if (!t.ready || ax_surface_prepare_commit(s->surface, &t) != AX_OK ||
        !axt_shm_commit(&shm, wl_surface, t.pixel_width, t.pixel_height, 0xFF102030u) ||
        !wait_for(s, AX_WAKE_FRAME, AXT_SECONDS(2))) {
        fail(s, "first frame");
        goto done;
    }

    /* 2. The main thread maximizes the window (a new configure) and changes the minimum size (a
     *    commit request). */
    ax_atomic_store_u32(&s->step, STEP_FIRST_FRAME_SHOWN, AX_MO_RELEASE);
    if (!wait_step(s, STEP_CHANGES_SENT)) {
        fail(s, "main thread");
        goto done;
    }
    deadline = ax_time_monotonic_ns() + AXT_SECONDS(5);
    while (ax_time_monotonic_ns() < deadline) {
        if (!begin(s, &t)) {
            fail(s, "begin");
            goto done;
        }
        if (t.acks_configure && t.commit_requested) {
            break;
        }
        ax_surface_wait_frame(s->surface, 20u * 1000000u, NULL);
        ax_thread_yield();
    }
    s->failed_frame_acked = t.acks_configure;
    s->failed_frame_requested = t.commit_requested;
    s->failed_w = t.pixel_width;
    s->failed_h = t.pixel_height;

    /* 3. The frame that fails: prepared (acknowledging the configure, asking for a callback and
     *    feedback), then never committed. */
    target_before = w->clock.last_target_ns;
    if (ax_surface_prepare_commit(s->surface, &t) != AX_OK) {
        fail(s, "prepare");
        goto done;
    }
    if (!s->lose_silently) {
        if (ax_surface_commit_failed(s->surface) != AX_OK) {
            fail(s, "commit_failed");
        }
        s->target_restored = w->clock.last_target_ns == target_before;
        s->frame_pending_after = w->app->ops->surface_frame_pending(w);
        s->request_raised_again = ax_atomic_load_u32(&w->commit_requested, AX_MO_SEQ_CST) != 0;
        /* A second report has nothing to undo. */
        if (ax_surface_commit_failed(s->surface) != AX_ERR_UNSUPPORTED) {
            fail(s, "second commit_failed");
        }
    }
    t0 = ax_time_monotonic_ns();
    s->wake_after = AX_WAKE_TIMEOUT;
    ax_surface_wait_frame(s->surface, AXT_SECONDS(2), &s->wake_after);
    s->wake_after_ns = ax_time_monotonic_ns() - t0;

    /* 4. The next frame: the same configure, not acknowledged again, delivered with a buffer of
     *    its size; the compositor must accept it and answer with a frame callback. */
    if (!begin(s, &t)) {
        fail(s, "begin");
        goto done;
    }
    s->next_frame_acked = t.acks_configure;
    s->next_frame_requested = t.commit_requested;
    s->next_w = t.pixel_width;
    s->next_h = t.pixel_height;
    if (ax_surface_prepare_commit(s->surface, &t) != AX_OK ||
        !axt_shm_commit(&shm, wl_surface, t.pixel_width, t.pixel_height, 0xFF304050u)) {
        fail(s, "next frame");
        goto done;
    }
    s->next_frame_shown = wait_for(s, AX_WAKE_FRAME, AXT_SECONDS(2));
    {
        ax_present_stats stats;
        stats.struct_size = sizeof stats;
        ax_surface_get_present_stats(s->surface, &stats, false);
        s->lost_frames = stats.lost_frames;
    }

done:
    axt_shm_deinit(&shm);
    ax_atomic_store_u32(&s->step, STEP_DONE, AX_MO_RELEASE);
    return 0;
}

static bool step_reached(void *ctx)
{
    script *s = (script *)ctx;
    return ax_atomic_load_u32(&s->step, AX_MO_ACQUIRE) >= STEP_FIRST_FRAME_SHOWN;
}

static bool script_done(void *ctx)
{
    script *s = (script *)ctx;
    return ax_atomic_load_u32(&s->step, AX_MO_ACQUIRE) >= STEP_DONE;
}

/* Runs the script with the main thread's part; returns false on a setup failure. */
static bool run_script(script *s, axt_app *t, ax_window *window)
{
    ax_window_desc d = axt_window_desc(320, 200, AX_DECORATIONS_NONE);
    ax_thread thread;
    bool ok;

    if (axt_app_create(t, 0) != AX_OK || ax_window_create(t->app, &d, window) != AX_OK ||
        ax_window_get_surface(*window, &s->surface) != AX_OK) {
        return false;
    }
    ax_atomic_init_u32(&s->step, STEP_START);
    ax_atomic_init_u32(&s->failures, 0);
    if (ax_thread_create(&thread, script_main, s) != AX_OK) {
        return false;
    }
    ok = axt_pump_until(t->app, step_reached, s, AXT_SECONDS(5));
    if (ok && ax_atomic_load_u32(&s->step, AX_MO_ACQUIRE) == STEP_FIRST_FRAME_SHOWN) {
        ax__wl_test_set_maximized(*window, true);
        ok = ax_window_set_min_size(*window, 100, 80) == AX_OK;
        ax_atomic_store_u32(&s->step, STEP_CHANGES_SENT, AX_MO_RELEASE);
    }
    ok = axt_pump_until(t->app, script_done, s, AXT_SECONDS(10)) && ok;
    ax_thread_join(&thread);
    /* The connection is still healthy (a protocol error would have closed it): one more pump. */
    ok = ax_app_pump(t->app, 0) == AX_OK && ok;
    return ok;
}

AX_TEST(a_failed_commit_is_undone_without_acknowledging_twice)
{
    script s;
    axt_app t;
    ax_window window;

    AXT_REQUIRE_COMPOSITOR();
    memset(&s, 0, sizeof s);
    AX_REQUIRE(run_script(&s, &t, &window));
    AX_CHECK_EQ_U(ax_atomic_load_u32(&s.failures, AX_MO_RELAXED), 0);

    AX_CHECK(s.failed_frame_acked);
    AX_CHECK(s.failed_frame_requested);
    AX_CHECK(s.target_restored);
    AX_CHECK(!s.frame_pending_after);
    AX_CHECK(s.request_raised_again);
    /* Nothing is in flight, so the raised commit request wakes the presenter at once instead of
     * after the frame-callback bound. */
    AX_CHECK_EQ_U(s.wake_after, AX_WAKE_COMMIT_REQUESTED);
    AX_CHECK(s.wake_after_ns < 40u * 1000000u);

    AX_CHECK(!s.next_frame_acked);
    AX_CHECK(s.next_frame_requested);
    AX_CHECK_EQ_U(s.next_w, s.failed_w);
    AX_CHECK_EQ_U(s.next_h, s.failed_h);
    AX_CHECK(s.next_frame_shown);
    AX_CHECK_EQ_U(s.lost_frames, 1);

    ax_window_destroy(window);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(a_frame_callback_that_never_comes_is_given_up)
{
    script s;
    axt_app t;
    ax_window window;

    AXT_REQUIRE_COMPOSITOR();
    memset(&s, 0, sizeof s);
    s.lose_silently = true;
    AX_REQUIRE(run_script(&s, &t, &window));
    AX_CHECK_EQ_U(ax_atomic_load_u32(&s.failures, AX_MO_RELAXED), 0);

    /* The lost commit's callback never comes; after the bound (at least 50 ms) the presenter is
     * woken for a new frame instead of waiting for the 2 s timeout. */
    AX_CHECK_EQ_U(s.wake_after, AX_WAKE_FRAME);
    AX_CHECK(s.wake_after_ns >= 45u * 1000000u);
    AX_CHECK(s.wake_after_ns < 1000u * 1000000u);
    AX_CHECK(!s.next_frame_acked);
    AX_CHECK(s.next_frame_shown);
    AX_CHECK_EQ_U(s.lost_frames, 1);

    ax_window_destroy(window);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}
