/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The threaded pipeline against a headless weston, on lavapipe (decision 78): an app thread
 * builds a node tree in a world, the runtime's scene thread lays it out and records it, its render
 * thread presents it with ax_render_wgpu. The test thread is the main thread and pumps.
 *   - the tree is presented, and ax_txn_on_presented says so;
 *   - a resize is answered by the size handshake with a snapshot of the new size;
 *   - with the app thread blocked for 500 ms, no frame is lost compared with 500 ms unblocked.
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_runtime_window.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/render/ax_render_frame.h"
#include "axion/render/ax_render_wgpu.h"
#include "axion/render/ax_render_wgpu_surface.h"
#include "axion/runtime/ax_runtime_platform.h"
#include "gpu_test_util.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

#define MS 1000000ull

/* ---- The app thread -------------------------------------------------------------------------- */

typedef enum app_task { TASK_PRESENT = 0, TASK_RESIZE = 1, TASK_BLOCK = 2 } app_task;

typedef struct app_ctx {
    ax_app app;
    ax_window window;
    app_task task;
    ax_atomic_u32 done;  /* 1 when the app thread finished */
    ax_atomic_u32 phase; /* task-specific progress the main thread waits on */
    ax_atomic_u32 go;    /* the main thread's go-ahead (resize) */
    /* results, read after done */
    uint32_t presented_status;
    uint64_t presented_time;
    float resized_w, resized_h;
    ax_rt_window_counters before, after;           /* around the blocked window */
    ax_rt_window_counters ref_before, ref_after;   /* around the reference window */
    ax_rt_window_counters rest_before, rest_after; /* 400 ms at rest (TASK_PRESENT) */
    char failure[160];
} app_ctx;

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* A motion that lasts: the bar slides 280 points over 30 s, linearly (an implicit animation the
 * scene runs every frame without the app thread), so every frame has new content. */
static void start_sliding(ax_world world, ax_node bar)
{
    ax_txn *t = ax_txn_begin(world);
    ax_transition tr;
    ax_transform2d m = {1, 0, 0, 1, 280, 0};
    memset(&tr, 0, sizeof tr);
    tr.struct_size = sizeof tr;
    tr.kind = AX_TRANSITION_TIMING;
    tr.duration = 30.0f; /* linear: an all-zero curve */
    ax_txn_set_transition(t, bar, AX_PROP_TRANSFORM, &tr);
    ax_txn_set_transform(t, bar, AX_PROP_TRANSFORM, m);
    (void)ax_txn_commit(t, NULL);
}

static void on_presented(void *user, uint64_t seq, ax_presented_status status, uint64_t t)
{
    app_ctx *c = (app_ctx *)user;
    (void)seq;
    c->presented_status = (uint32_t)status;
    c->presented_time = t;
}

static void spin_for(uint64_t ns)
{
    uint64_t until = ax_time_monotonic_ns() + ns;
    while (ax_time_monotonic_ns() < until) {
        ax_thread_yield();
    }
}

/* Blocks the calling thread without using the CPU (as a thread stopped in a GC pause or a
 * system call would), so the comparison with the reference window is about the block alone. */
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

static bool wait_world_event(ax_world w, uint32_t type, ax_world_event *out, uint64_t timeout)
{
    uint64_t until = ax_time_monotonic_ns() + timeout;
    while (ax_time_monotonic_ns() < until) {
        ax_world_event e;
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        (void)ax_world_wait_event(w, &e, 20 * MS);
        if (e.type == type) {
            if (out != NULL) {
                *out = e;
            }
            return true;
        }
    }
    return false;
}

static int app_main(void *arg)
{
    app_ctx *c = (app_ctx *)arg;
    ax_world world;
    ax_runtime rt;
    ax_runtime_desc d;
    ax_rt_platform_desc pd;
    ax_render_wgpu_desc wd;
    ax_renderer_desc rd;
    ax_node root, card, bar;
    ax_txn *t;
    uint64_t seq = 0;
    ax_rgba bg = {0.02f, 0.02f, 0.03f, 1}, white = {1, 1, 1, 1}, accent = {0.1f, 0.2f, 1, 1};

    if (ax_world_create(NULL, &world) != AX_OK) {
        snprintf(c->failure, sizeof c->failure, "world");
        goto out;
    }
    memset(&wd, 0, sizeof wd);
    wd.chain.type = AX_RENDERER_CHAIN_WGPU;
    wd.adapter = AX_GPU_ADAPTER_SOFTWARE;
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.next = &wd.chain;
    rd.initial_background = ax_color_make(0, 0, 0, 1);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.world = world;
    memset(&pd, 0, sizeof pd);
    pd.app = c->app;
    pd.renderer_desc = &rd;
    pd.max_wait_ns = c->task == TASK_RESIZE ? 50 * MS : 0; /* generous on loaded runners */
    if (ax_rt_create_for_app(&d, &pd, &rt) != AX_OK ||
        ax_runtime_add_window(rt, c->window, &root) != AX_OK) {
        snprintf(c->failure, sizeof c->failure, "runtime: %.*s", (int)ax_last_error()->message.len,
                 ax_last_error()->message.ptr);
        ax_world_destroy(world);
        goto out;
    }

    /* The tree: a background, a card and the bar that slides once the tree is shown. */
    t = ax_txn_begin(world);
    card = ax_txn_create(t, AX_NODE_VIEW);
    bar = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, bg);
    ax_txn_set_u32(t, card, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_u32(t, bar, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_dimension(t, card, AX_PROP_LEFT, pt(16));
    ax_txn_set_dimension(t, card, AX_PROP_TOP, pt(16));
    ax_txn_set_dimension(t, card, AX_PROP_WIDTH, pt(120));
    ax_txn_set_dimension(t, card, AX_PROP_HEIGHT, pt(60));
    ax_txn_set_color(t, card, AX_PROP_BACKGROUND_COLOR, white);
    ax_txn_set_f32(t, card, AX_PROP_CORNER_RADIUS, 12.0f);
    ax_txn_set_dimension(t, bar, AX_PROP_TOP, pt(100));
    ax_txn_set_dimension(t, bar, AX_PROP_WIDTH, pt(24));
    ax_txn_set_dimension(t, bar, AX_PROP_HEIGHT, pt(24));
    ax_txn_set_color(t, bar, AX_PROP_BACKGROUND_COLOR, accent);
    ax_txn_append_child(t, root, card);
    ax_txn_append_child(t, root, bar);
    if (ax_txn_commit(t, &seq) != AX_OK ||
        ax_txn_on_presented(world, seq, on_presented, c) != AX_OK) {
        snprintf(c->failure, sizeof c->failure, "commit");
        goto stop;
    }
    {
        uint64_t until = ax_time_monotonic_ns() + 20000 * MS;
        while (c->presented_status == 0 && ax_time_monotonic_ns() < until) {
            ax_world_event e;
            memset(&e, 0, sizeof e);
            e.struct_size = sizeof e;
            (void)ax_world_wait_event(world, &e, 20 * MS);
        }
    }
    if (c->task == TASK_PRESENT) {
        /* Nothing moves: the window rests without frames (decision 95). */
        idle_for(world, 300 * MS);
        (void)ax_rt_get_window_counters(rt, c->window, &c->rest_before);
        idle_for(world, 400 * MS);
        (void)ax_rt_get_window_counters(rt, c->window, &c->rest_after);
    }
    start_sliding(world, bar);
    ax_atomic_store_u32(&c->phase, 1, AX_MO_RELEASE);

    if (c->task == TASK_RESIZE) {
        ax_world_event e;
        uint64_t until;
        while (ax_atomic_load_u32(&c->go, AX_MO_ACQUIRE) == 0) {
            ax_thread_yield();
        }
        if (!wait_world_event(world, AX_WORLD_EVENT_RESIZED, &e, 10000 * MS) || e.root != root.h) {
            snprintf(c->failure, sizeof c->failure, "no RESIZED event for the root");
            goto stop;
        }
        c->resized_w = e.data.resized.width;
        c->resized_h = e.data.resized.height;
        until = ax_time_monotonic_ns() + 10000 * MS;
        do {
            (void)ax_rt_get_window_counters(rt, c->window, &c->after);
            spin_for(5 * MS);
        } while ((c->after.shown_width != c->resized_w || c->after.shown_height != c->resized_h) &&
                 ax_time_monotonic_ns() < until);
    } else if (c->task == TASK_BLOCK) {
        idle_for(world, 500 * MS); /* settle at the display's rate */
        (void)ax_rt_get_window_counters(rt, c->window, &c->ref_before);
        idle_for(world, 500 * MS); /* the reference: the app thread waits for events */
        (void)ax_rt_get_window_counters(rt, c->window, &c->ref_after);
        (void)ax_rt_get_window_counters(rt, c->window, &c->before);
        sleep_for(500 * MS); /* blocked: no polling, no commits */
        (void)ax_rt_get_window_counters(rt, c->window, &c->after);
    } else {
        idle_for(world, 200 * MS); /* sliding */
        (void)ax_rt_get_window_counters(rt, c->window, &c->after);
    }

stop:
    (void)ax_runtime_remove_window(rt, c->window);
    (void)ax_runtime_destroy(rt);
    (void)ax_world_destroy(world);
out:
    ax_atomic_store_u32(&c->done, 1, AX_MO_RELEASE);
    return 0;
}

/* ---- Fixture --------------------------------------------------------------------------------- */

static bool flag_set(void *ctx)
{
    return ax_atomic_load_u32((ax_atomic_u32 *)ctx, AX_MO_ACQUIRE) != 0;
}

/* 1 ran, 0 skip (no software adapter). */
static int run(app_ctx *c, app_task task, float w, float h)
{
    axt_app t;
    ax_window_desc wdesc = axt_window_desc(w, h, AX_DECORATIONS_NONE);
    ax_thread thread;
    ax_gpu *probe = NULL;
    int have = axt_gpu_create(NULL, &probe);
    if (have <= 0) {
        return have;
    }
    ax_gpu_destroy(probe);
    memset(c, 0, sizeof *c);
    c->task = task;
    ax_atomic_init_u32(&c->done, 0);
    ax_atomic_init_u32(&c->phase, 0);
    ax_atomic_init_u32(&c->go, 0);
    if (axt_app_create(&t, 0) != AX_OK || ax_window_create(t.app, &wdesc, &c->window) != AX_OK) {
        return -1;
    }
    c->app = t.app;
    if (ax_thread_create(&thread, app_main, c) != AX_OK) {
        return -1;
    }
    if (task == TASK_RESIZE) {
        axt_pump_until(t.app, flag_set, &c->phase, AXT_SECONDS(30));
        ax__wl_test_set_maximized(c->window, true);
        ax_atomic_store_u32(&c->go, 1, AX_MO_RELEASE);
    }
    axt_pump_until(t.app, flag_set, &c->done, AXT_SECONDS(60));
    ax_thread_join(&thread);
    ax_window_destroy(c->window);
    if (axt_app_destroy(&t) != 0) {
        snprintf(c->failure, sizeof c->failure, "leaks in the app");
    }
    return 1;
}

#define RUN_OR_SKIP(c, task, w, h)                                                       \
    do {                                                                                 \
        int r_;                                                                          \
        AXT_REQUIRE_COMPOSITOR();                                                        \
        r_ = run(c, task, w, h);                                                         \
        if (r_ == 0) {                                                                   \
            AX_SKIP("no software Vulkan adapter (lavapipe) for the presentation tests"); \
        }                                                                                \
        AX_REQUIRE(r_ == 1);                                                             \
        if ((c)->failure[0] != '\0') {                                                   \
            ax_test_fail(__FILE__, __LINE__, "app thread: %s", (c)->failure);            \
            return;                                                                      \
        }                                                                                \
    } while (0)

/* ---- The M0 loop, without a scene thread of the runtime (the baseline) ------------------------
 */

/* A scene thread as in M0 (examples/m0_window/ir.c, test_ir_present.c): it records each request
 * into a mailbox, here a background, a card and a moving bar, like the pipeline's tree. */
typedef struct m0_scene {
    ax_ir_mailbox mailbox;
    ax_mutex mutex;
    ax_cond cond;
    ax_scene_request request;
    uint64_t requests;
    bool stop;
} m0_scene;

static int m0_scene_main(void *arg)
{
    m0_scene *sc = (m0_scene *)arg;
    uint64_t seen = 0;
    for (;;) {
        ax_scene_request q;
        ax_ir_builder *b;
        const float r[4] = {12, 12, 12, 12};
        ax_mutex_lock(&sc->mutex);
        while (sc->requests == seen && !sc->stop) {
            ax_cond_wait_timeout(&sc->cond, &sc->mutex, 50 * MS);
        }
        if (sc->stop) {
            ax_mutex_unlock(&sc->mutex);
            return 0;
        }
        q = sc->request;
        seen = sc->requests;
        ax_mutex_unlock(&sc->mutex);
        b = ax_ir_mailbox_begin(&sc->mailbox, q.logical_width, q.logical_height, q.frame_time_ns,
                                ax_color_make(0.02f, 0.02f, 0.03f, 1));
        ax_ir_rect(b, ax_rect_make(0, 0, q.logical_width, q.logical_height),
                   ax_color_make(0.02f, 0.02f, 0.03f, 1));
        ax_ir_rounded_rect(b, ax_rect_make(16, 16, 120, 60), r, ax_color_make(1, 1, 1, 1));
        ax_ir_rect(b,
                   ax_rect_make((float)((q.frame_time_ns / MS) * 400u / 1000u % 300u), 100, 24, 24),
                   ax_color_make(0.1f, 0.2f, 1, 1));
        if (ax_ir_mailbox_publish(&sc->mailbox) != AX_OK) {
            return 1;
        }
    }
}

static void m0_on_request(void *ctx, const ax_scene_request *q)
{
    m0_scene *sc = (m0_scene *)ctx;
    ax_mutex_lock(&sc->mutex);
    sc->request = *q;
    sc->requests++;
    ax_cond_signal(&sc->cond);
    ax_mutex_unlock(&sc->mutex);
}

typedef struct m0_render {
    ax_surface surface;
    ax_window window;
    m0_scene *sc;
    ax_atomic_u32 done;
    int state;              /* 1 measured, 0 no adapter, -1 failed */
    ax_present_stats stats; /* over the measured 500 ms */
} m0_render;

/* M0's render thread: ax_render_frame_step in a loop; 500 ms to settle, 500 ms measured. */
static int m0_render_main(void *arg)
{
    m0_render *p = (m0_render *)arg;
    const ax_renderer_vtbl *vt = ax_render_wgpu_vtbl();
    ax_render_wgpu_desc wd;
    ax_renderer_desc rd;
    ax_render_frame_desc fd;
    ax_render_frame rf;
    void *self = NULL;
    uint64_t start, measure_from = 0;
    ax_result r;

    memset(&wd, 0, sizeof wd);
    wd.chain.type = AX_RENDERER_CHAIN_WGPU;
    wd.adapter = AX_GPU_ADAPTER_SOFTWARE;
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.next = &wd.chain;
    rd.initial_background = ax_color_make(0, 0, 0, 1);
    r = vt->create(&rd, &self);
    if (r != AX_OK || vt->attach_surface(self, p->surface) != AX_OK) {
        p->state = r == AX_ERR_UNSUPPORTED ? 0 : -1;
        if (self != NULL) {
            vt->destroy(self);
        }
        ax_atomic_store_u32(&p->done, 1, AX_MO_RELEASE);
        return 0;
    }
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.vtbl = vt;
    fd.renderer = self;
    fd.surface = p->surface;
    fd.window = p->window;
    fd.mailbox = &p->sc->mailbox;
    fd.request = m0_on_request;
    fd.request_ctx = p->sc;
    ax_render_frame_init(&rf, &fd);
    start = ax_time_monotonic_ns();
    p->state = -1;
    for (;;) {
        uint64_t now = ax_time_monotonic_ns();
        r = ax_render_frame_step(&rf, true, 50 * MS, NULL);
        if (r != AX_OK && r != AX_ERR_DEVICE_LOST) {
            break;
        }
        if (measure_from == 0 && now - start >= 500 * MS) {
            p->stats.struct_size = sizeof p->stats;
            (void)ax_surface_get_present_stats(p->surface, &p->stats, true); /* reset */
            measure_from = now;
        } else if (measure_from != 0 && now - measure_from >= 500 * MS) {
            p->stats.struct_size = sizeof p->stats;
            (void)ax_surface_get_present_stats(p->surface, &p->stats, false);
            p->state = 1;
            break;
        }
    }
    vt->detach_surface(self, p->surface);
    vt->destroy(self);
    ax_atomic_store_u32(&p->done, 1, AX_MO_RELEASE);
    return 0;
}

/* 1 measured, 0 skip (no adapter), -1 failed. */
static int m0_baseline(ax_present_stats *out)
{
    static m0_scene sc;
    static m0_render p;
    axt_app t;
    ax_window_desc wdesc = axt_window_desc(320, 200, AX_DECORATIONS_NONE);
    ax_thread scene_t, render_t;
    ax_gpu *probe = NULL;
    int have = axt_gpu_create(NULL, &probe);
    if (have <= 0) {
        return have;
    }
    ax_gpu_destroy(probe);
    memset(&sc, 0, sizeof sc);
    memset(&p, 0, sizeof p);
    ax_ir_mailbox_init(&sc.mailbox, ax_default_allocator());
    ax_mutex_init(&sc.mutex);
    ax_cond_init(&sc.cond);
    ax_atomic_init_u32(&p.done, 0);
    p.sc = &sc;
    if (axt_app_create(&t, 0) != AX_OK || ax_window_create(t.app, &wdesc, &p.window) != AX_OK ||
        ax_window_get_surface(p.window, &p.surface) != AX_OK ||
        ax_thread_create(&scene_t, m0_scene_main, &sc) != AX_OK ||
        ax_thread_create(&render_t, m0_render_main, &p) != AX_OK) {
        return -1;
    }
    axt_pump_until(t.app, flag_set, &p.done, AXT_SECONDS(30));
    ax_thread_join(&render_t);
    ax_mutex_lock(&sc.mutex);
    sc.stop = true;
    ax_cond_signal(&sc.cond);
    ax_mutex_unlock(&sc.mutex);
    ax_thread_join(&scene_t);
    ax_window_destroy(p.window);
    (void)axt_app_destroy(&t);
    ax_cond_deinit(&sc.cond);
    ax_mutex_deinit(&sc.mutex);
    ax_ir_mailbox_deinit(&sc.mailbox);
    *out = p.stats;
    return p.state;
}

/* ---- Tests ----------------------------------------------------------------------------------- */

AX_TEST(the_tree_is_presented)
{
    static app_ctx c;
    RUN_OR_SKIP(&c, TASK_PRESENT, 320, 200);
    AX_CHECK_EQ_U(c.presented_status, AX_PRESENTED);
    AX_CHECK(c.presented_time > 0);
    AX_CHECK(c.after.frames > 0);
    AX_CHECK(c.after.snapshots_shown > 0);
    AX_CHECK(c.after.shown_width == 320.0f && c.after.shown_height == 200.0f);
    /* At rest, nothing presented (decision 95); then the sliding bar presents again. */
    printf("at rest: %llu presented in 400 ms\n",
           (unsigned long long)(c.rest_after.presented - c.rest_before.presented));
    AX_CHECK_EQ_U(c.rest_after.presented - c.rest_before.presented, 0);
    AX_CHECK(c.after.presented > c.rest_after.presented);
}

AX_TEST(a_resize_is_answered_with_a_snapshot_of_the_new_size)
{
    static app_ctx c;
    RUN_OR_SKIP(&c, TASK_RESIZE, 320, 200);
    printf("resized to %gx%g: shown %gx%g, scene late %llu, forced %llu\n", (double)c.resized_w,
           (double)c.resized_h, (double)c.after.shown_width, (double)c.after.shown_height,
           (unsigned long long)c.after.scene_late, (unsigned long long)c.after.forced_acks);
    AX_CHECK(c.resized_w > 320.0f && c.resized_h > 200.0f);
    AX_CHECK(c.after.shown_width == c.resized_w && c.after.shown_height == c.resized_h);
    AX_CHECK_EQ_U(c.after.forced_acks, 0); /* the scene answered within the bound */
}

/*
 * The criterion is relative: blocking the app thread must lose no frame compared with the same
 * window not blocked, on the same machine. A software renderer on a loaded CI runner may not reach
 * every refresh at all (weston 13 at 80 Hz on GitHub's runners presents every other one); that
 * shows in both windows and is not the block's doing. On the owner's desktop and with weston 15 in
 * CI both windows present every refresh.
 */
AX_TEST(a_blocked_app_thread_loses_no_frame)
{
    static app_ctx c;
    uint64_t presented, shown, missed, ref_presented, ref_shown, ref_missed;
    RUN_OR_SKIP(&c, TASK_BLOCK, 320, 200);
    presented = c.after.presented - c.before.presented;
    shown = c.after.snapshots_shown - c.before.snapshots_shown;
    missed = c.after.missed_vblanks - c.before.missed_vblanks;
    ref_presented = c.ref_after.presented - c.ref_before.presented;
    ref_shown = c.ref_after.snapshots_shown - c.ref_before.snapshots_shown;
    ref_missed = c.ref_after.missed_vblanks - c.ref_before.missed_vblanks;
    printf("500 ms at %.2f Hz: blocked: %llu presented, %llu new snapshots, %llu missed vblanks; "
           "not blocked: %llu, %llu, %llu\n",
           c.after.refresh_ns != 0 ? 1e9 / (double)c.after.refresh_ns : 0.0,
           (unsigned long long)presented, (unsigned long long)shown, (unsigned long long)missed,
           (unsigned long long)ref_presented, (unsigned long long)ref_shown,
           (unsigned long long)ref_missed);
    AX_REQUIRE(ref_presented >= 5); /* the pipeline runs at all */
    /* Two frames of slack in the counts: each window's two edges fall anywhere within a frame
     * (two refreshes where the runner presents every other one). A block that held the pipeline
     * back would lose the whole window, about 30 frames. Missed refreshes get the same: where
     * every other refresh is missed, they follow the presentations one for one. */
    AX_CHECK(presented + 2u >= ref_presented);
    AX_CHECK(shown + 2u >= ref_shown);
    AX_CHECK(missed <= ref_missed + 2u);
}

/*
 * The baseline for the frame rate (the owner's review): M0's loop (ax_render_frame_step with a
 * scene thread that records on request) and the M1 pipeline, 500 ms each on the same machine and
 * compositor. The pipeline must present as often and miss no more refreshes, with the slack of
 * the test above. Where the machine reaches every refresh both do; where it does not (GitHub's
 * Ubuntu runners), this shows whether the pipeline adds latency of its own.
 */
AX_TEST(the_pipeline_presents_as_often_as_the_m0_loop)
{
    static app_ctx c;
    ax_present_stats m0;
    uint64_t ref_presented, ref_missed;
    int r;
    AXT_REQUIRE_COMPOSITOR();
    memset(&m0, 0, sizeof m0);
    r = m0_baseline(&m0);
    if (r == 0) {
        AX_SKIP("no software Vulkan adapter (lavapipe) for the presentation tests");
    }
    AX_REQUIRE(r == 1);
    RUN_OR_SKIP(&c, TASK_BLOCK, 320, 200);
    ref_presented = c.ref_after.presented - c.ref_before.presented;
    ref_missed = c.ref_after.missed_vblanks - c.ref_before.missed_vblanks;
    printf("500 ms at %.2f Hz: M0 loop: %llu presented, %llu missed vblanks; M1 pipeline: %llu "
           "presented, %llu missed vblanks\n",
           m0.refresh_interval_ns != 0 ? 1e9 / (double)m0.refresh_interval_ns : 0.0,
           (unsigned long long)m0.presented, (unsigned long long)m0.missed_vblanks,
           (unsigned long long)ref_presented, (unsigned long long)ref_missed);
    AX_REQUIRE(m0.presented >= 5);
    AX_CHECK(ref_presented + 2u >= m0.presented);
    AX_CHECK(ref_missed <= m0.missed_vblanks + 2u);
}
