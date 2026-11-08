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
    ax_atomic_u32 bad_predictions; /* a target earlier than the previous frame's */
    ax_atomic_u32 failures;
    ax_atomic_u64 max_lead_ns;      /* largest (target - now) seen at begin_frame */
    ax_atomic_u64 presented_commit; /* ax_surface_get_presented_commit's id */
    ax_atomic_u32 id_errors;        /* commit ids out of order or ahead of the commits made */

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
    ax_atomic_init_u64(&p->max_lead_ns, 0);
    ax_atomic_init_u64(&p->presented_commit, 0);
    ax_atomic_init_u32(&p->id_errors, 0);
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
            uint64_t now = ax_time_monotonic_ns();
            if (t.predicted_present_ns < last_predicted) {
                ax_atomic_fetch_add_u32(&p->bad_predictions, 1, AX_MO_RELAXED);
            }
            if (t.predicted_present_ns > now &&
                t.predicted_present_ns - now > ax_atomic_load_u64(&p->max_lead_ns, AX_MO_RELAXED)) {
                ax_atomic_store_u64(&p->max_lead_ns, t.predicted_present_ns - now, AX_MO_RELAXED);
            }
            last_predicted = t.predicted_present_ns;
            if (ax_surface_prepare_commit(p->surface, &t) != AX_OK ||
                !axt_shm_commit(&shm, wl_surface, t.pixel_width, t.pixel_height,
                                0xFF203040u + (n * 8u))) {
                ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
                break;
            }
            if (ax_surface_prepared_commit(p->surface) != (uint64_t)n + 1u) {
                ax_atomic_fetch_add_u32(&p->id_errors, 1, AX_MO_RELAXED);
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
        {
            uint64_t id = 0, at = 0;
            uint64_t prev = ax_atomic_load_u64(&p->presented_commit, AX_MO_RELAXED);
            if (ax_surface_get_presented_commit(p->surface, &id, &at) != AX_OK || id < prev ||
                id > n || (id != 0 && at == 0)) {
                ax_atomic_fetch_add_u32(&p->id_errors, 1, AX_MO_RELAXED);
            }
            ax_atomic_store_u64(&p->presented_commit, id, AX_MO_RELEASE);
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

/* A burst of configures (an interactive resize sends one per pointer motion) must not make the
 * presenter commit faster than the display, run the animation clock ahead, or leave the newest
 * configure unacknowledged.
 *
 * Limitation: weston answers maximize toggles one configure at a time, waiting for each ack, so it
 * never produces configures faster than frames the way KWin's interactive resize does; this test
 * guards the behaviour but does not reproduce that burst. The clock running ahead is reproduced by
 * tests/unit/platform/test_frame_clock.c (extra_commits_do_not_run_the_clock_ahead), and the burst
 * itself is checked by resizing examples/m0_window by hand. */
AX_TEST(configure_bursts_do_not_outrun_frames)
{
    static fixture f;
    ack_goal goal;
    int i;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(fixture_start(&f, 320, 240, false));
    AX_REQUIRE(pump_until_count(f.t.app, &f.p.frame_wakes, 5, AXT_SECONDS(5)));

    {
        uint32_t commits_before = ax_atomic_load_u32(&f.p.committed, AX_MO_ACQUIRE);
        uint32_t frames_before = ax_atomic_load_u32(&f.p.frame_wakes, AX_MO_ACQUIRE);
        uint32_t commits;
        uint32_t frames;
        for (i = 0; i < 40; ++i) {
            ax__wl_test_set_maximized(f.window, (i % 2) == 0);
            ax_app_pump(f.t.app, 1000000u); /* 1 ms between configures */
        }
        commits = ax_atomic_load_u32(&f.p.committed, AX_MO_ACQUIRE) - commits_before;
        frames = ax_atomic_load_u32(&f.p.frame_wakes, AX_MO_ACQUIRE) - frames_before;
        /* About one commit per frame callback, not one per configure. */
        AX_CHECK(commits <= frames + 3);
    }
    /* The last request unmaximized: the newest configure is acknowledged at 320x240. */
    goal.p = &f.p;
    goal.width = 320;
    goal.height = 240;
    AX_CHECK(axt_pump_until(f.t.app, acked_size, &goal, AXT_SECONDS(5)));
    {
        uint64_t until = ax_time_monotonic_ns() + 200u * 1000000u;
        while (ax_time_monotonic_ns() < until) {
            ax_app_pump(f.t.app, 10u * 1000000u);
        }
    }
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.failures, AX_MO_RELAXED), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.bad_predictions, AX_MO_RELAXED), 0);
    /* The animation clock never runs ahead of real time by more than a few refreshes. */
    AX_CHECK(ax_atomic_load_u64(&f.p.max_lead_ns, AX_MO_RELAXED) <=
             4 * f.p.stats.refresh_interval_ns);
    if (f.p.stats.presented > 0) {
        AX_CHECK(f.p.stats.discarded <= 2);
    }
}

/* Each prepared commit gets the next id, and the newest one shown is reported with its time. */
AX_TEST(presented_commits_are_reported_by_id)
{
    static fixture f;
    uint64_t until;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(fixture_start(&f, 320, 240, false));
    until = ax_time_monotonic_ns() + AXT_SECONDS(10);
    while (ax_atomic_load_u64(&f.p.presented_commit, AX_MO_ACQUIRE) < 20 &&
           ax_time_monotonic_ns() < until) {
        ax_app_pump(f.t.app, 5u * 1000000u);
    }
    AX_CHECK(ax_atomic_load_u64(&f.p.presented_commit, AX_MO_ACQUIRE) >= 20);
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.failures, AX_MO_RELAXED), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.p.id_errors, AX_MO_RELAXED), 0);
}

/* ---- one presenter, several surfaces (the render thread) ------------------------------------- */

typedef struct multi_presenter {
    ax_surface surfaces[2];
    ax_atomic_u32 stop;
    ax_atomic_u32 ready;
    ax_atomic_u32 frames[2];
    ax_atomic_u32 failures;
} multi_presenter;

static int multi_presenter_main(void *arg)
{
    multi_presenter *m = (multi_presenter *)arg;
    axt_shm shm[2];
    struct wl_surface *wl_surfaces[2];
    bool draw[2] = {true, true};
    int i;

    for (i = 0; i < 2; ++i) {
        ax_surface_native native;
        memset(&native, 0, sizeof native);
        native.struct_size = sizeof native;
        if (!axt_shm_init(&shm[i], m->surfaces[i]) ||
            ax_surface_get_native(m->surfaces[i], &native) != AX_OK) {
            ax_atomic_fetch_add_u32(&m->failures, 1, AX_MO_RELAXED);
            return 1;
        }
        wl_surfaces[i] = (struct wl_surface *)native.u.wayland.surface;
    }
    ax_atomic_store_u32(&m->ready, 1, AX_MO_RELEASE);

    while (ax_atomic_load_u32(&m->stop, AX_MO_ACQUIRE) == 0) {
        ax_wake_reason reasons[2];
        for (i = 0; i < 2; ++i) {
            ax_frame_target t;
            memset(&t, 0, sizeof t);
            t.struct_size = sizeof t;
            if (!draw[i]) {
                continue;
            }
            if (ax_surface_begin_frame(m->surfaces[i], &t) != AX_OK) {
                ax_atomic_fetch_add_u32(&m->failures, 1, AX_MO_RELAXED);
                return 1;
            }
            if (t.ready && (ax_surface_prepare_commit(m->surfaces[i], &t) != AX_OK ||
                            !axt_shm_commit(&shm[i], wl_surfaces[i], t.pixel_width, t.pixel_height,
                                            i == 0 ? 0xFF402020u : 0xFF204020u))) {
                ax_atomic_fetch_add_u32(&m->failures, 1, AX_MO_RELAXED);
                return 1;
            }
        }
        if (ax_surface_wait_frames(m->surfaces, 2, 50u * 1000000u, reasons) != AX_OK) {
            ax_atomic_fetch_add_u32(&m->failures, 1, AX_MO_RELAXED);
            break;
        }
        for (i = 0; i < 2; ++i) {
            draw[i] = reasons[i] != AX_WAKE_TIMEOUT;
            if (reasons[i] == AX_WAKE_FRAME) {
                ax_atomic_fetch_add_u32(&m->frames[i], 1, AX_MO_RELAXED);
            }
        }
    }
    axt_shm_deinit(&shm[0]);
    axt_shm_deinit(&shm[1]);
    return 0;
}

typedef struct two_counts {
    multi_presenter *m;
    uint32_t at_least;
} two_counts;

static bool both_reached(void *ctx)
{
    two_counts *g = (two_counts *)ctx;
    return ax_atomic_load_u32(&g->m->frames[0], AX_MO_RELAXED) >= g->at_least &&
           ax_atomic_load_u32(&g->m->frames[1], AX_MO_RELAXED) >= g->at_least;
}

AX_TEST(one_presenter_waits_on_two_surfaces)
{
    static multi_presenter m;
    axt_app t;
    ax_window_desc d = axt_window_desc(200, 150, AX_DECORATIONS_NONE);
    ax_window w[2];
    ax_thread thread;
    two_counts goal;
    ax_wake_reason reasons[2];
    ax_surface bad[2];
    int i;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    memset(&m, 0, sizeof m);
    for (i = 0; i < 2; ++i) {
        AX_REQUIRE(ax_window_create(t.app, &d, &w[i]) == AX_OK);
        AX_REQUIRE(ax_window_get_surface(w[i], &m.surfaces[i]) == AX_OK);
        ax_atomic_init_u32(&m.frames[i], 0);
    }
    ax_atomic_init_u32(&m.stop, 0);
    ax_atomic_init_u32(&m.ready, 0);
    ax_atomic_init_u32(&m.failures, 0);

    /* Arguments are checked before anything waits. */
    AX_CHECK_RESULT(ax_surface_wait_frames(m.surfaces, 0, 0, reasons), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_surface_wait_frames(NULL, 1, 0, reasons), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_surface_wait_frames(m.surfaces, AX_SURFACE_WAIT_MAX + 1u, 0, reasons),
                    AX_ERR_INVALID_ARGUMENT);
    bad[0] = m.surfaces[0];
    bad[1].h = 0;
    AX_CHECK_RESULT(ax_surface_wait_frames(bad, 2, 0, reasons), AX_ERR_INVALID_HANDLE);

    AX_REQUIRE(ax_thread_create(&thread, multi_presenter_main, &m) == AX_OK);
    AX_REQUIRE(pump_until_count(t.app, &m.ready, 1, AXT_SECONDS(5)));
    /* Both surfaces keep their own cadence from the one thread. */
    goal.m = &m;
    goal.at_least = 30;
    AX_CHECK(axt_pump_until(t.app, both_reached, &goal, AXT_SECONDS(10)));
    ax_atomic_store_u32(&m.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&thread);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&m.failures, AX_MO_RELAXED), 0);
    AX_CHECK_RESULT(ax_app_pump(t.app, 0), AX_OK); /* no protocol error */
    for (i = 0; i < 2; ++i) {
        AX_CHECK_RESULT(ax_window_destroy(w[i]), AX_OK);
    }
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}
