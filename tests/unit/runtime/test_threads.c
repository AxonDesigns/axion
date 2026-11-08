/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The pipeline with its threads (decision 78) and a fake presenter that paces each surface like a
 * display: an app thread, the scene thread and the render thread exchange transactions, requests,
 * snapshots and presentation reports. Run under ThreadSanitizer in CI (label runtime).
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
#include "axion/ax_runtime.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/render/ax_render_ir.h"
#include "axion/runtime/ax_runtime_sdk.h"

#define MS    1000000ull
#define MAX_S 4

#define REQUIRE_THREADS()                               \
    do {                                                \
        if (!ax_threads_supported()) {                  \
            AX_SKIP("the threaded mode needs threads"); \
        }                                               \
    } while (0)

/* ---- A presenter that paces each surface like a display -------------------------------------- */

typedef struct fake_display {
    ax_runtime rt;
    uint64_t refresh[MAX_S];
    /* render thread only */
    bool live[MAX_S];
    uint64_t next_due[MAX_S];
    uint64_t last_seq[MAX_S];
    /* read by the test */
    ax_atomic_u64 frames[MAX_S];
    ax_atomic_u64 shown[MAX_S]; /* new snapshots shown */
    ax_atomic_u64 late[MAX_S];  /* frames that missed their refresh */
    ax_atomic_u32 detached;
} fake_display;

static void request_next(fake_display *d, uint32_t i)
{
    ax_rt_request q;
    memset(&q, 0, sizeof q);
    q.logical_width = 320.0f;
    q.logical_height = 200.0f;
    q.scale = 1.0f;
    q.time_ns = d->next_due[i];
    q.refresh_ns = d->refresh[i];
    ax_rt_request_frame(d->rt, i, &q);
}

static ax_result fd_attach(void *self, ax_runtime rt, uint32_t i, uint64_t native, uint64_t window)
{
    fake_display *d = (fake_display *)self;
    (void)native;
    (void)window;
    d->rt = rt;
    d->live[i] = true;
    d->last_seq[i] = 0;
    d->next_due[i] = ax_time_monotonic_ns() + d->refresh[i];
    request_next(d, i);
    return AX_OK;
}

static void fd_detach(void *self, uint32_t i)
{
    fake_display *d = (fake_display *)self;
    d->live[i] = false;
    ax_atomic_fetch_add_u32(&d->detached, 1, AX_MO_RELAXED);
}

static ax_result fd_wait(void *self, const uint32_t *surfaces, uint32_t count, uint64_t timeout,
                         bool *due)
{
    fake_display *d = (fake_display *)self;
    uint64_t now = ax_time_monotonic_ns();
    uint64_t until = now + timeout;
    uint32_t i;
    bool any = false;
    for (i = 0; i < count; ++i) {
        if (d->next_due[surfaces[i]] < until) {
            until = d->next_due[surfaces[i]];
        }
    }
    /* The vsync. Yielding until it is due, not a timed wait: timed waits wake up to 10 ms late
     * on macOS runners, which would make the display itself miss refreshes. */
    while (ax_time_monotonic_ns() < until) {
        ax_thread_yield();
    }
    now = ax_time_monotonic_ns();
    for (i = 0; i < count; ++i) {
        due[i] = now >= d->next_due[surfaces[i]];
        any = any || due[i];
    }
    (void)any;
    return AX_OK;
}

static ax_result fd_frame(void *self, uint32_t i)
{
    fake_display *d = (fake_display *)self;
    const ax_render_ir *ir = ax_ir_mailbox_acquire(ax_rt_surface_mailbox(d->rt, i));
    uint64_t now = ax_time_monotonic_ns();
    ax_atomic_fetch_add_u64(&d->frames[i], 1, AX_MO_RELAXED);
    if (ir != NULL && ir->sequence > d->last_seq[i]) {
        d->last_seq[i] = ir->sequence;
        ax_atomic_fetch_add_u64(&d->shown[i], 1, AX_MO_RELAXED);
        ax_rt_snapshot_presented(d->rt, i, ir->sequence, d->next_due[i], false);
    }
    if (now > d->next_due[i] + d->refresh[i]) {
        ax_atomic_fetch_add_u64(&d->late[i], 1, AX_MO_RELAXED);
    }
    d->next_due[i] += d->refresh[i];
    if (d->next_due[i] < now) {
        d->next_due[i] = now; /* far behind: start again from now */
    }
    request_next(d, i);
    return AX_OK;
}

static const ax_rt_presenter_vtbl k_display = {
    sizeof(ax_rt_presenter_vtbl), fd_attach, fd_detach, fd_wait, fd_frame, NULL};

static void display_init(fake_display *d, uint64_t refresh0, uint64_t refresh1)
{
    uint32_t i;
    memset(d, 0, sizeof *d);
    d->refresh[0] = refresh0;
    d->refresh[1] = refresh1;
    d->refresh[2] = refresh0;
    d->refresh[3] = refresh0;
    for (i = 0; i < MAX_S; ++i) {
        ax_atomic_init_u64(&d->frames[i], 0);
        ax_atomic_init_u64(&d->shown[i], 0);
        ax_atomic_init_u64(&d->late[i], 0);
    }
    ax_atomic_init_u32(&d->detached, 0);
}

static void display_deinit(fake_display *d)
{
    (void)d;
}

/* ---- Helpers --------------------------------------------------------------------------------- */

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* A root with one 40x40 child; returns the child. */
static ax_node make_root(ax_world w, ax_node *out_root)
{
    ax_txn *t = ax_txn_begin(w);
    ax_node root = ax_txn_create(t, AX_NODE_VIEW);
    ax_node child = ax_txn_create(t, AX_NODE_VIEW);
    ax_rgba white = {1, 1, 1, 1};
    ax_txn_set_dimension(t, child, AX_PROP_WIDTH, pt(40));
    ax_txn_set_dimension(t, child, AX_PROP_HEIGHT, pt(40));
    ax_txn_set_color(t, child, AX_PROP_BACKGROUND_COLOR, white);
    ax_txn_append_child(t, root, child);
    if (ax_txn_commit(t, NULL) != AX_OK) {
        child.h = 0;
    }
    *out_root = root;
    return child;
}

/* Sleeps without using the CPU: a blocked thread, as in a GC pause or a system call. */
static void sleep_for(uint64_t ns)
{
    ax_mutex m;
    ax_cond c;
    uint64_t until = ax_time_monotonic_ns() + ns;
    ax_mutex_init(&m);
    ax_cond_init(&c);
    ax_mutex_lock(&m);
    while (ax_time_monotonic_ns() < until) {
        ax_cond_wait_timeout(&c, &m, until - ax_time_monotonic_ns());
    }
    ax_mutex_unlock(&m);
    ax_cond_deinit(&c);
    ax_mutex_deinit(&m);
}

/* The app thread as usual: waiting for events. */
static void idle_for(ax_world w, uint64_t ns)
{
    uint64_t until = ax_time_monotonic_ns() + ns;
    while (ax_time_monotonic_ns() < until) {
        ax_world_event e;
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        (void)ax_world_wait_event(w, &e, 10 * MS);
    }
}

typedef struct order_log {
    uint64_t last_seq;
    uint32_t count;
    uint32_t out_of_order;
    uint32_t not_presented;
} order_log;

static void on_presented(void *user, uint64_t seq, ax_presented_status status, uint64_t t)
{
    order_log *l = (order_log *)user;
    (void)t;
    if (seq <= l->last_seq) {
        l->out_of_order++;
    }
    if (status != AX_PRESENTED) {
        l->not_presented++;
    }
    l->last_seq = seq;
    l->count++;
}

static void poll_all(ax_world w)
{
    ax_world_event e;
    do {
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        (void)ax_world_poll_event(w, &e);
    } while (e.type != AX_WORLD_EVENT_NONE);
}

/* ---- Tests ----------------------------------------------------------------------------------- */

AX_TEST(two_surfaces_keep_their_own_rates_while_the_app_commits)
{
    static fake_display d;
    static order_log log;
    ax_world w;
    ax_runtime rt;
    ax_rt_desc rd;
    ax_rt_surface_desc sd;
    ax_node root[2], child[2];
    uint32_t s[2];
    uint64_t start, until, last = 0;
    uint32_t i, committed = 0;
    float last_left = 0;
    REQUIRE_THREADS();
    display_init(&d, 16666667u, 6944444u); /* 60 Hz and 144 Hz */
    memset(&log, 0, sizeof log);
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    memset(&rd, 0, sizeof rd);
    rd.world = w;
    rd.mode = AX_RUNTIME_THREADED;
    rd.presenter = &k_display;
    rd.presenter_self = &d;
    AX_REQUIRE(ax_rt_create(&rd, &rt) == AX_OK);
    memset(&sd, 0, sizeof sd);
    for (i = 0; i < 2; ++i) {
        child[i] = make_root(w, &root[i]);
        AX_REQUIRE(!AX_HANDLE_IS_NULL(child[i]));
        AX_REQUIRE(ax_rt_add_surface(rt, root[i], &sd, &s[i]) == AX_OK);
    }

    /* For one second, a transaction every 3 ms moving one child, each awaited on screen. */
    start = ax_time_monotonic_ns();
    until = start + 1000 * MS;
    while (ax_time_monotonic_ns() < until) {
        ax_txn *t = ax_txn_begin(w);
        uint64_t seq = 0;
        last_left = (float)(committed % 200u);
        ax_txn_set_dimension(t, child[committed % 2u], AX_PROP_LEFT, pt(last_left));
        AX_REQUIRE(ax_txn_commit(t, &seq) == AX_OK);
        AX_REQUIRE(ax_txn_on_presented(w, seq, on_presented, &log) == AX_OK);
        last = seq;
        committed++;
        poll_all(w);
        sleep_for(3 * MS);
    }
    /* Every callback comes, in order, presented. */
    until = ax_time_monotonic_ns() + 5000 * MS;
    while (log.count < committed && ax_time_monotonic_ns() < until) {
        ax_world_event e;
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        (void)ax_world_wait_event(w, &e, 50 * MS);
    }
    AX_CHECK_EQ_U(log.count, committed);
    AX_CHECK_EQ_U(log.last_seq, last);
    AX_CHECK_EQ_U(log.out_of_order, 0);
    AX_CHECK_EQ_U(log.not_presented, 0);
    /* The mirror shows the last change. */
    {
        ax_rectf f;
        AX_CHECK(ax_world_mirror_sync(w) >= last);
        AX_REQUIRE(ax_mirror_get_frame(w, child[(committed - 1u) % 2u], &f) == AX_OK);
        AX_CHECK(f.x == last_left);
    }
    /* Each surface at its own rate: about 60 and 144 frames in that second. Loose bounds: CI
     * runners (macOS especially) wake timed waits late, and the 144 Hz surface suffers most. */
    {
        uint64_t f0 = ax_atomic_load_u64(&d.frames[s[0]], AX_MO_RELAXED);
        uint64_t f1 = ax_atomic_load_u64(&d.frames[s[1]], AX_MO_RELAXED);
        printf("frames: 60 Hz %llu, 144 Hz %llu; late %llu, %llu\n", (unsigned long long)f0,
               (unsigned long long)f1,
               (unsigned long long)ax_atomic_load_u64(&d.late[s[0]], AX_MO_RELAXED),
               (unsigned long long)ax_atomic_load_u64(&d.late[s[1]], AX_MO_RELAXED));
        AX_CHECK(f0 >= 40);
        AX_CHECK(3u * f1 >= 4u * f0); /* clearly faster than the 60 Hz one */
    }
    AX_CHECK_RESULT(ax_runtime_destroy(rt), AX_OK);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&d.detached, AX_MO_RELAXED), 2);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    display_deinit(&d);
}

/* The scene driver moves the child at every tick, as an animation would. */
static void slide(void *user, ax_world world, ax_node root, uint64_t t, float w, float h)
{
    (void)w;
    (void)h;
    ax_scene *scene = ax_world_scene(world);
    ax_value v;
    ax_node child = ax_node_store_first_child(&scene->store, root);
    (void)user;
    if (AX_HANDLE_IS_NULL(child)) {
        return;
    }
    v.dimension = pt((float)((t / MS) % 280u));
    (void)ax_scene_set(scene, child, AX_PROP_LEFT, AX_VALUE_DIMENSION, &v);
}

AX_TEST(a_blocked_app_thread_does_not_stop_frames)
{
    static fake_display d;
    ax_world w;
    ax_runtime rt;
    ax_rt_desc rd;
    ax_rt_surface_desc sd;
    ax_node root;
    uint32_t s;
    uint64_t frames0, shown0, late0, frames, shown, late;
    uint64_t ref_frames, ref_shown, ref_late;
    REQUIRE_THREADS();
    display_init(&d, 16666667u, 16666667u);
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    memset(&rd, 0, sizeof rd);
    rd.world = w;
    rd.mode = AX_RUNTIME_THREADED;
    rd.presenter = &k_display;
    rd.presenter_self = &d;
    rd.driver = slide;
    AX_REQUIRE(ax_rt_create(&rd, &rt) == AX_OK);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(make_root(w, &root)));
    memset(&sd, 0, sizeof sd);
    AX_REQUIRE(ax_rt_add_surface(rt, root, &sd, &s) == AX_OK);
    idle_for(w, 200 * MS); /* running */

    /* The reference: 500 ms with the app thread waiting for events as usual. */
    ref_frames = ax_atomic_load_u64(&d.frames[s], AX_MO_RELAXED);
    ref_shown = ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED);
    ref_late = ax_atomic_load_u64(&d.late[s], AX_MO_RELAXED);
    idle_for(w, 500 * MS);
    ref_frames = ax_atomic_load_u64(&d.frames[s], AX_MO_RELAXED) - ref_frames;
    ref_shown = ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED) - ref_shown;
    ref_late = ax_atomic_load_u64(&d.late[s], AX_MO_RELAXED) - ref_late;

    /* The app thread blocks for 500 ms: it polls nothing and commits nothing. */
    frames0 = ax_atomic_load_u64(&d.frames[s], AX_MO_RELAXED);
    shown0 = ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED);
    late0 = ax_atomic_load_u64(&d.late[s], AX_MO_RELAXED);
    sleep_for(500 * MS);
    frames = ax_atomic_load_u64(&d.frames[s], AX_MO_RELAXED) - frames0;
    shown = ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED) - shown0;
    late = ax_atomic_load_u64(&d.late[s], AX_MO_RELAXED) - late0;
    printf("500 ms: blocked: %llu frames, %llu new snapshots, %llu late; not blocked: %llu, %llu, "
           "%llu\n",
           (unsigned long long)frames, (unsigned long long)shown, (unsigned long long)late,
           (unsigned long long)ref_frames, (unsigned long long)ref_shown,
           (unsigned long long)ref_late);
    /* Every frame the unblocked window had, each with a new snapshot from the scene. Relative,
     * with two frames of slack at the windows' edges (a late frame is followed by one that
     * catches up, which may fall in either window); a block that held the pipeline back would lose
     * the whole window, about 30 frames. */
    AX_CHECK(ref_frames >= 15);
    AX_CHECK(frames + 2u >= ref_frames);
    AX_CHECK(shown + 2u >= ref_shown);
    AX_CHECK(late <= ref_late + 1u);
    AX_CHECK_RESULT(ax_runtime_destroy(rt), AX_OK);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    display_deinit(&d);
}

AX_TEST(surfaces_come_and_go_while_the_threads_run)
{
    static fake_display d;
    ax_world w;
    ax_runtime rt;
    ax_rt_desc rd;
    ax_rt_surface_desc sd;
    uint32_t i;
    REQUIRE_THREADS();
    display_init(&d, 4000000u, 4000000u);
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    memset(&rd, 0, sizeof rd);
    rd.world = w;
    rd.mode = AX_RUNTIME_THREADED;
    rd.presenter = &k_display;
    rd.presenter_self = &d;
    AX_REQUIRE(ax_rt_create(&rd, &rt) == AX_OK);
    memset(&sd, 0, sizeof sd);
    for (i = 0; i < 20; ++i) {
        ax_node root;
        uint32_t s;
        uint64_t before;
        AX_REQUIRE(!AX_HANDLE_IS_NULL(make_root(w, &root)));
        AX_REQUIRE(ax_rt_add_surface(rt, root, &sd, &s) == AX_OK);
        before = ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED);
        {
            uint64_t until = ax_time_monotonic_ns() + 2000 * MS;
            while (ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED) == before &&
                   ax_time_monotonic_ns() < until) {
                ax_thread_yield();
            }
        }
        AX_CHECK(ax_atomic_load_u64(&d.shown[s], AX_MO_RELAXED) > before);
        AX_REQUIRE(ax_rt_remove_surface(rt, s) == AX_OK);
        poll_all(w);
    }
    AX_CHECK_EQ_U(ax_atomic_load_u32(&d.detached, AX_MO_RELAXED), 20);
    AX_CHECK_RESULT(ax_runtime_destroy(rt), AX_OK);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    display_deinit(&d);
}
