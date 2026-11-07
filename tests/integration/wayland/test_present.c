/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland integration tests for the presenter path: buffers, frame callbacks, presentation
 * feedback, resize acknowledgement and commit requests. The test thread is the main thread and
 * pumps; a presenter thread draws with shared-memory buffers.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_present.h"
#include "axion/platform/ax_surface_native.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

#define MAX_COMMITS 512

typedef struct commit_record {
    bool acked;
    uint32_t serial;
    uint32_t width, height;
} commit_record;

typedef struct presenter {
    ax_surface surface;
    bool idle; /* commit only for configures and commit requests, not every frame */

    ax_atomic_u32 stop;
    ax_atomic_u32 ready;
    ax_atomic_u32 committed;
    ax_atomic_u32 frame_wakes;
    ax_atomic_u32 configure_wakes;
    ax_atomic_u32 commit_wakes;
    ax_atomic_u32 presented;
    ax_atomic_u32 bad_predictions;
    ax_atomic_u32 failures;

    /* written by the presenter, read by the test after an acquire of `committed` */
    commit_record commits[MAX_COMMITS];
    ax_present_stats stats; /* final, read after join */
} presenter;

static void presenter_init(presenter *p, ax_surface surface, bool idle)
{
    memset(p, 0, sizeof *p);
    p->surface = surface;
    p->idle = idle;
    ax_atomic_init_u32(&p->stop, 0);
    ax_atomic_init_u32(&p->ready, 0);
    ax_atomic_init_u32(&p->committed, 0);
    ax_atomic_init_u32(&p->frame_wakes, 0);
    ax_atomic_init_u32(&p->configure_wakes, 0);
    ax_atomic_init_u32(&p->commit_wakes, 0);
    ax_atomic_init_u32(&p->presented, 0);
    ax_atomic_init_u32(&p->bad_predictions, 0);
    ax_atomic_init_u32(&p->failures, 0);
}

static int presenter_main(void *arg)
{
    presenter *p = (presenter *)arg;
    axt_shm shm;
    ax_surface_native native;
    struct wl_surface *wl_surface;
    bool draw = true;
    uint64_t last_predicted = 0;
    uint32_t n = 0;

    memset(&native, 0, sizeof native);
    native.struct_size = sizeof native;
    if (!axt_shm_init(&shm, p->surface) || ax_surface_get_native(p->surface, &native) != AX_OK) {
        ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
        return 1;
    }
    wl_surface = (struct wl_surface *)native.u.wayland.surface;
    ax_atomic_store_u32(&p->ready, 1, AX_MO_RELEASE);

    while (ax_atomic_load_u32(&p->stop, AX_MO_ACQUIRE) == 0) {
        ax_frame_target t;
        ax_wake_reason reason;
        ax_present_stats stats;

        memset(&t, 0, sizeof t);
        t.struct_size = sizeof t;
        if (ax_surface_begin_frame(p->surface, &t) != AX_OK) {
            ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
            break;
        }
        if (t.ready && (draw || t.acks_configure || t.commit_requested)) {
            if (t.predicted_present_ns <= last_predicted) {
                ax_atomic_fetch_add_u32(&p->bad_predictions, 1, AX_MO_RELAXED);
            }
            last_predicted = t.predicted_present_ns;
            if (ax_surface_prepare_commit(p->surface, &t) != AX_OK ||
                !axt_shm_commit(&shm, wl_surface, t.pixel_width, t.pixel_height,
                                0xFF203040u + (n * 8u))) {
                ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
                break;
            }
            if (n < MAX_COMMITS) {
                p->commits[n].acked = t.acks_configure;
                p->commits[n].serial = t.configure_serial;
                p->commits[n].width = t.pixel_width;
                p->commits[n].height = t.pixel_height;
            }
            n++;
            ax_atomic_store_u32(&p->committed, n, AX_MO_RELEASE);
        }

        ax_surface_wait_frame(p->surface, 50u * 1000000u, &reason);
        switch (reason) {
        case AX_WAKE_FRAME:
            ax_atomic_fetch_add_u32(&p->frame_wakes, 1, AX_MO_RELAXED);
            draw = !p->idle;
            break;
        case AX_WAKE_CONFIGURE:
            ax_atomic_fetch_add_u32(&p->configure_wakes, 1, AX_MO_RELAXED);
            draw = true;
            break;
        case AX_WAKE_COMMIT_REQUESTED:
            ax_atomic_fetch_add_u32(&p->commit_wakes, 1, AX_MO_RELAXED);
            draw = true;
            break;
        case AX_WAKE_TIMEOUT:
        default:
            draw = false; /* nothing is due: an animating presenter waits for the callback */
            break;
        }
        stats.struct_size = sizeof stats;
        if (ax_surface_get_present_stats(p->surface, &stats, false) == AX_OK) {
            ax_atomic_store_u32(&p->presented, (uint32_t)stats.presented, AX_MO_RELEASE);
        }
    }
    p->stats.struct_size = sizeof p->stats;
    ax_surface_get_present_stats(p->surface, &p->stats, false);
    axt_shm_deinit(&shm);
    return 0;
}

typedef struct counter_goal {
    ax_atomic_u32 *counter;
    uint32_t at_least;
} counter_goal;

static bool counter_reached(void *ctx)
{
    counter_goal *g = (counter_goal *)ctx;
    return ax_atomic_load_u32(g->counter, AX_MO_ACQUIRE) >= g->at_least;
}

static bool pump_until_count(ax_app app, ax_atomic_u32 *counter, uint32_t at_least,
                             uint64_t timeout_ns)
{
    counter_goal g;
    g.counter = counter;
    g.at_least = at_least;
    return axt_pump_until(app, counter_reached, &g, timeout_ns);
}

typedef struct fixture {
    axt_app t;
    ax_window window;
    ax_surface surface;
    presenter p;
    ax_thread thread;
} fixture;

static bool fixture_start(fixture *f, float width, float height, bool idle)
{
    ax_window_desc d = axt_window_desc(width, height, AX_DECORATIONS_NONE);
    if (axt_app_create(&f->t, 0) != AX_OK || ax_window_create(f->t.app, &d, &f->window) != AX_OK ||
        ax_window_get_surface(f->window, &f->surface) != AX_OK) {
        return false;
    }
    presenter_init(&f->p, f->surface, idle);
    if (ax_thread_create(&f->thread, presenter_main, &f->p) != AX_OK) {
        return false;
    }
    /* The presenter's shm roundtrip needs the main thread pumping. */
    return pump_until_count(f->t.app, &f->p.ready, 1, AXT_SECONDS(5));
}

static size_t fixture_stop(fixture *f)
{
    ax_atomic_store_u32(&f->p.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&f->thread);
    ax_window_destroy(f->window);
    return axt_app_destroy(&f->t);
}

AX_TEST(frames_are_presented_with_callbacks_and_feedback)
{
    static fixture f;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(fixture_start(&f, 320, 240, false));

    /* An animating presenter: one commit per frame callback. */
    AX_CHECK(pump_until_count(f.t.app, &f.p.frame_wakes, 30, AXT_SECONDS(10)));
    if ((ax_app_caps(f.t.app) & AX_CAP_PRESENTATION_TIMING) != 0) {
        AX_CHECK(pump_until_count(f.t.app, &f.p.presented, 20, AXT_SECONDS(10)));
    }
    AX_CHECK_EQ_U(fixture_stop(&f), 0);

    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.failures, AX_MO_RELAXED), 0);
    AX_CHECK(ax_atomic_load_u32(&f.p.committed, AX_MO_RELAXED) >= 30);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.bad_predictions, AX_MO_RELAXED), 0);
    /* The first commit acknowledged the first configure, at the configured size. */
    AX_CHECK(f.p.commits[0].acked);
    AX_CHECK_EQ_U(f.p.commits[0].width, 320);
    AX_CHECK_EQ_U(f.p.commits[0].height, 240);
    AX_CHECK(!f.p.commits[1].acked); /* never twice */
    /* A plausible refresh interval: between 20 Hz and 240 Hz. */
    AX_CHECK(f.p.stats.refresh_interval_ns > 4000000u && f.p.stats.refresh_interval_ns < 50000000u);
    AX_CHECK(f.p.stats.latency_samples >= 1);
}

/* The newest commit that acknowledged a configure. */
static const commit_record *last_ack(const presenter *p)
{
    uint32_t n = ax_atomic_load_u32(&p->committed, AX_MO_ACQUIRE);
    uint32_t i;
    if (n > MAX_COMMITS) {
        n = MAX_COMMITS;
    }
    for (i = n; i > 0; --i) {
        if (p->commits[i - 1].acked) {
            return &p->commits[i - 1];
        }
    }
    return NULL;
}

typedef struct ack_goal {
    presenter *p;
    uint32_t width, height;
} ack_goal;

static bool acked_size(void *ctx)
{
    ack_goal *g = (ack_goal *)ctx;
    const commit_record *c = last_ack(g->p);
    return c != NULL && c->width == g->width && c->height == g->height;
}

AX_TEST(resize_is_acknowledged_with_a_buffer_of_the_new_size)
{
    static fixture f;
    ax_event e;
    ack_goal goal;
    uint32_t first_serial;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(fixture_start(&f, 320, 240, false));
    AX_REQUIRE(axt_wait_event(f.t.app, AX_EVENT_RESIZED, f.window, &e, AXT_SECONDS(5)));
    goal.p = &f.p;
    goal.width = 320;
    goal.height = 240;
    AX_REQUIRE(axt_pump_until(f.t.app, acked_size, &goal, AXT_SECONDS(5)));
    first_serial = last_ack(&f.p)->serial;

    /* The compositor resizes the window: maximize it. */
    ax__wl_test_set_maximized(f.window, true);
    AX_REQUIRE(axt_wait_event(f.t.app, AX_EVENT_RESIZED, f.window, &e, AXT_SECONDS(5)));
    AX_CHECK(e.data.resized.width > 320 && e.data.resized.height > 240);
    /* The presenter acknowledges that configure in a commit with a buffer of exactly that size. */
    goal.width = e.data.resized.pixel_width;
    goal.height = e.data.resized.pixel_height;
    AX_CHECK(axt_pump_until(f.t.app, acked_size, &goal, AXT_SECONDS(5)));
    AX_CHECK(last_ack(&f.p)->serial != first_serial);

    /* And back. */
    ax__wl_test_set_maximized(f.window, false);
    AX_REQUIRE(axt_wait_event(f.t.app, AX_EVENT_RESIZED, f.window, &e, AXT_SECONDS(5)));
    goal.width = e.data.resized.pixel_width;
    goal.height = e.data.resized.pixel_height;
    AX_CHECK(axt_pump_until(f.t.app, acked_size, &goal, AXT_SECONDS(5)));

    /* No protocol error: the connection is still healthy. */
    AX_CHECK_RESULT(ax_app_pump(f.t.app, 0), AX_OK);
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.failures, AX_MO_RELAXED), 0);
}

AX_TEST(commit_time_state_wakes_an_idle_presenter)
{
    static fixture f;
    uint32_t committed;
    uint64_t until;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(fixture_start(&f, 320, 240, true));

    /* The idle presenter commits once for the first configure, then sits waiting. */
    AX_REQUIRE(pump_until_count(f.t.app, &f.p.committed, 1, AXT_SECONDS(5)));
    until = ax_time_monotonic_ns() + 300u * 1000000u;
    while (ax_time_monotonic_ns() < until) {
        ax_app_pump(f.t.app, 10u * 1000000u);
    }
    committed = ax_atomic_load_u32(&f.p.committed, AX_MO_ACQUIRE);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.commit_wakes, AX_MO_RELAXED), 0);

    /* Minimum size is double-buffered state: it needs a commit, and only the presenter commits. */
    AX_REQUIRE(ax_window_set_min_size(f.window, 200, 150) == AX_OK);
    AX_CHECK(pump_until_count(f.t.app, &f.p.commit_wakes, 1, AXT_SECONDS(5)));
    AX_CHECK(pump_until_count(f.t.app, &f.p.committed, committed + 1, AXT_SECONDS(5)));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.failures, AX_MO_RELAXED), 0);
}

AX_TEST(begin_frame_before_configure_is_not_ready)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(100, 100, AX_DECORATIONS_NONE);
    ax_window w;
    ax_surface s;
    ax_frame_target f;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &w) == AX_OK);
    AX_REQUIRE(ax_window_get_surface(w, &s) == AX_OK);
    memset(&f, 0, sizeof f);
    f.struct_size = sizeof f;
    AX_CHECK_RESULT(ax_surface_begin_frame(s, &f), AX_OK); /* no pump yet: no configure */
    AX_CHECK(!f.ready);
    AX_CHECK_RESULT(ax_surface_prepare_commit(s, &f), AX_ERR_UNSUPPORTED);
    AX_CHECK_RESULT(ax_window_destroy(w), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}
