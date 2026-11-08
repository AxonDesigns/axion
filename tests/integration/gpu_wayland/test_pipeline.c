/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The threaded pipeline against a headless weston, on lavapipe (decision 78): an app thread
 * builds a node tree in a world, the runtime's scene thread lays it out and records it, its render
 * thread presents it with ax_render_wgpu. The test thread is the main thread and pumps.
 *   - the tree is presented, and ax_txn_on_presented says so;
 *   - a resize is answered by the size handshake with a snapshot of the new size;
 *   - with the app thread blocked for 500 ms, every refresh still presents a new snapshot.
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
#include "axion/render/ax_render_wgpu.h"
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
    ax_rt_window_counters before, after;
    char failure[160];
} app_ctx;

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* The scene driver: a bar moving at 400 units a second, so every frame has new content. */
static void slide(void *user, ax_world world, ax_node root, uint64_t t, float w, float h)
{
    (void)w;
    (void)h;
    ax_scene *scene = ax_world_scene(world);
    ax_node bar = ax_node_store_last_child(&scene->store, root);
    ax_value v;
    (void)user;
    if (!AX_HANDLE_IS_NULL(bar)) {
        v.dimension = pt((float)((t / MS) * 400u / 1000u % 300u));
        (void)ax_scene_set(scene, bar, AX_PROP_LEFT, AX_VALUE_DIMENSION, &v);
    }
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
    pd.driver = slide;
    pd.max_wait_ns = c->task == TASK_RESIZE ? 50 * MS : 0; /* generous on loaded runners */
    if (ax_rt_create_for_app(&d, &pd, &rt) != AX_OK ||
        ax_runtime_add_window(rt, c->window, &root) != AX_OK) {
        snprintf(c->failure, sizeof c->failure, "runtime: %.*s", (int)ax_last_error()->message.len,
                 ax_last_error()->message.ptr);
        ax_world_destroy(world);
        goto out;
    }

    /* The tree: a background, a card and the moving bar (the driver moves the last child). */
    t = ax_txn_begin(world);
    card = ax_txn_create(t, AX_NODE_VIEW);
    bar = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, bg);
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
        spin_for(500 * MS); /* settle at the display's rate */
        (void)ax_rt_get_window_counters(rt, c->window, &c->before);
        spin_for(500 * MS); /* blocked: no polling, no commits */
        (void)ax_rt_get_window_counters(rt, c->window, &c->after);
    } else {
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

AX_TEST(a_blocked_app_thread_loses_no_frame)
{
    static app_ctx c;
    uint64_t presented, shown, missed, expected;
    RUN_OR_SKIP(&c, TASK_BLOCK, 320, 200);
    presented = c.after.presented - c.before.presented;
    shown = c.after.snapshots_shown - c.before.snapshots_shown;
    missed = c.after.missed_vblanks - c.before.missed_vblanks;
    expected = c.after.refresh_ns != 0 ? 500 * MS / c.after.refresh_ns : 0;
    printf("blocked 500 ms at %.2f Hz: %llu presented (expected %llu), %llu new snapshots, %llu "
           "missed vblanks\n",
           c.after.refresh_ns != 0 ? 1e9 / (double)c.after.refresh_ns : 0.0,
           (unsigned long long)presented, (unsigned long long)expected, (unsigned long long)shown,
           (unsigned long long)missed);
    AX_REQUIRE(expected > 0);
    AX_CHECK(presented + 1u >= expected);
    AX_CHECK(shown + 1u >= expected);
    AX_CHECK_EQ_U(missed, 0);
}
