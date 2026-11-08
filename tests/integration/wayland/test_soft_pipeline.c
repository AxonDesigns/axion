/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The M1 pipeline presenting with ax_render_soft into wl_shm buffers (decision 84), against a
 * headless weston, without a GPU: the tree is presented and on_presented reports it, a maximize is
 * answered with a snapshot of the new size, and a runtime with no renderer pinned falls back to
 * the software renderer when no GPU renderer can be created (its test runs without a Vulkan
 * driver). Plus the CPU presentation interface's argument checks.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_runtime_window.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/platform/ax_present_cpu.h"
#include "axion/render/ax_render_soft_surface.h"
#include "axion/runtime/ax_runtime_platform.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

#define MS 1000000ull

typedef enum app_task { TASK_PRESENT = 0, TASK_RESIZE = 1 } app_task;

typedef struct app_ctx {
    ax_app app;
    ax_window window;
    app_task task;
    bool pin_soft; /* else the default chain, which must fall back */
    ax_atomic_u32 done, phase, go;
    uint32_t presented_status;
    float resized_w, resized_h;
    bool used_soft;
    ax_rt_window_counters after;
    char failure[160];
} app_ctx;

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static void slide(void *user, ax_world world, ax_node root, uint64_t t, float w, float h)
{
    ax_scene *scene = ax_world_scene(world);
    ax_node bar = ax_node_store_last_child(&scene->store, root);
    ax_value v;
    (void)user;
    (void)w;
    (void)h;
    if (!AX_HANDLE_IS_NULL(bar)) {
        v.dimension = pt((float)((t / MS) * 400u / 1000u % 300u));
        (void)ax_scene_set(scene, bar, AX_PROP_LEFT, AX_VALUE_DIMENSION, &v);
    }
}

static void on_presented(void *user, uint64_t seq, ax_presented_status status, uint64_t t)
{
    app_ctx *c = (app_ctx *)user;
    (void)seq;
    (void)t;
    c->presented_status = (uint32_t)status;
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
            *out = e;
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
    ax_renderer_desc rd;
    ax_node root, card, bar;
    ax_txn *t;
    uint64_t seq = 0;
    ax_rgba bg = {0.02f, 0.02f, 0.03f, 1}, white = {1, 1, 1, 1}, accent = {0.1f, 0.2f, 1, 1};

    if (ax_world_create(NULL, &world) != AX_OK) {
        snprintf(c->failure, sizeof c->failure, "world");
        goto out;
    }
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.initial_background = ax_color_make(0, 0, 0, 1);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.world = world;
    memset(&pd, 0, sizeof pd);
    pd.app = c->app;
    pd.renderer = c->pin_soft ? ax_render_soft_vtbl() : NULL;
    pd.renderer_desc = &rd;
    pd.driver = slide;
    pd.max_wait_ns = c->task == TASK_RESIZE ? 50 * MS : 0;
    if (ax_rt_create_for_app(&d, &pd, &rt) != AX_OK ||
        ax_runtime_add_window(rt, c->window, &root) != AX_OK) {
        snprintf(c->failure, sizeof c->failure, "runtime: %.*s", (int)ax_last_error()->message.len,
                 ax_last_error()->message.ptr);
        ax_world_destroy(world);
        goto out;
    }
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
    c->used_soft = ax_rt_renderer_vtbl(rt) == ax_render_soft_vtbl();
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
            ax_thread_yield();
        } while ((c->after.shown_width != c->resized_w || c->after.shown_height != c->resized_h) &&
                 ax_time_monotonic_ns() < until);
    } else {
        uint64_t until = ax_time_monotonic_ns() + 300 * MS;
        while (ax_time_monotonic_ns() < until) {
            ax_thread_yield();
        }
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

static bool flag_set(void *ctx)
{
    return ax_atomic_load_u32((ax_atomic_u32 *)ctx, AX_MO_ACQUIRE) != 0;
}

static bool run(app_ctx *c, app_task task, bool pin_soft)
{
    axt_app t;
    ax_window_desc wdesc = axt_window_desc(320, 200, AX_DECORATIONS_NONE);
    ax_thread thread;
    memset(c, 0, sizeof *c);
    c->task = task;
    c->pin_soft = pin_soft;
    ax_atomic_init_u32(&c->done, 0);
    ax_atomic_init_u32(&c->phase, 0);
    ax_atomic_init_u32(&c->go, 0);
    if (axt_app_create(&t, 0) != AX_OK || ax_window_create(t.app, &wdesc, &c->window) != AX_OK) {
        return false;
    }
    c->app = t.app;
    if (ax_thread_create(&thread, app_main, c) != AX_OK) {
        return false;
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
    return true;
}

#define RUN(c, task, pin)                                                     \
    do {                                                                      \
        AXT_REQUIRE_COMPOSITOR();                                             \
        AX_REQUIRE(run(c, task, pin));                                        \
        if ((c)->failure[0] != '\0') {                                        \
            ax_test_fail(__FILE__, __LINE__, "app thread: %s", (c)->failure); \
            return;                                                           \
        }                                                                     \
    } while (0)

AX_TEST(the_tree_is_presented_by_the_software_renderer)
{
    static app_ctx c;
    RUN(&c, TASK_PRESENT, true);
    printf("frames %llu, snapshots shown %llu, presented %llu\n",
           (unsigned long long)c.after.frames, (unsigned long long)c.after.snapshots_shown,
           (unsigned long long)c.after.presented);
    AX_CHECK(c.used_soft);
    AX_CHECK_EQ_U(c.presented_status, AX_PRESENTED);
    AX_CHECK(c.after.frames > 1);
    AX_CHECK(c.after.snapshots_shown > 1);
    AX_CHECK(c.after.shown_width == 320.0f && c.after.shown_height == 200.0f);
}

AX_TEST(a_resize_is_answered_with_a_snapshot_of_the_new_size)
{
    static app_ctx c;
    RUN(&c, TASK_RESIZE, true);
    printf("resized to %gx%g: shown %gx%g, scene late %llu, forced %llu\n", (double)c.resized_w,
           (double)c.resized_h, (double)c.after.shown_width, (double)c.after.shown_height,
           (unsigned long long)c.after.scene_late, (unsigned long long)c.after.forced_acks);
    AX_CHECK(c.resized_w > 320.0f && c.resized_h > 200.0f);
    AX_CHECK(c.after.shown_width == c.resized_w && c.after.shown_height == c.resized_h);
    AX_CHECK_EQ_U(c.after.forced_acks, 0);
}

/* Run without a Vulkan driver (CMakeLists.txt sets the environment): the GPU renderer cannot be
 * created, and windows are presented anyway. */
AX_TEST(without_a_usable_gpu_the_runtime_falls_back_to_the_software_renderer)
{
    static app_ctx c;
    RUN(&c, TASK_PRESENT, false);
    AX_CHECK(c.used_soft);
    AX_CHECK_EQ_U(c.presented_status, AX_PRESENTED);
    AX_CHECK(c.after.frames > 1);
}

AX_TEST(cpu_presentation_argument_errors)
{
    axt_app t;
    ax_window_desc wdesc = axt_window_desc(64, 64, AX_DECORATIONS_NONE);
    ax_window window;
    ax_surface surface;
    ax_frame_target frame;
    ax_cpu_buffer buf;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &wdesc, &window) == AX_OK);
    AX_REQUIRE(ax_window_get_surface(window, &surface) == AX_OK);
    memset(&frame, 0, sizeof frame);
    frame.struct_size = sizeof frame;
    memset(&buf, 0, sizeof buf);
    buf.struct_size = sizeof buf;
    /* A frame that is not ready. */
    AX_CHECK_RESULT(ax_surface_cpu_acquire(surface, &frame, &buf), AX_ERR_UNSUPPORTED);
    /* A buffer that is not the surface's. */
    frame.ready = true;
    frame.pixel_width = frame.pixel_height = 64;
    buf.width = buf.height = 64;
    buf.slot = 9;
    AX_CHECK_RESULT(ax_surface_cpu_present(surface, &frame, &buf), AX_ERR_INVALID_ARGUMENT);
    buf.struct_size = 4;
    AX_CHECK_RESULT(ax_surface_cpu_acquire(surface, &frame, &buf), AX_ERR_ABI_MISMATCH);
    ax_surface_cpu_release(surface); /* nothing to free: harmless */
    ax_window_destroy(window);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}
