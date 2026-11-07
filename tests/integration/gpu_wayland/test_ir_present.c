/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Render IR path against a headless weston, on lavapipe: a scene thread records snapshots
 * on request, a render thread presents them with ax_render_wgpu through ax_render_frame_step.
 * Checks the size handshake (decisions 54 and 57): a configure is acknowledged only with a
 * snapshot of its logical size; a late scene is waited for; a stalled one only up to the bound;
 * a hidden window acknowledges at once (decision 40); and frames keep coming, cleared, while a
 * lost device is recovered (decision 55).
 *
 * The render thread wraps the renderer's vtbl to see every frame it acknowledges.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/render/ax_render_frame.h"
#include "axion/render/ax_render_wgpu_surface.h"
#include "gpu_test_util.h"
#include "gpu_testing.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

/* ---- the scene thread -------------------------------------------------------------------------
 */

typedef struct scene {
    ax_ir_mailbox mailbox;
    ax_mutex mutex;
    ax_cond cond;
    ax_scene_request request;
    uint64_t requests;
    bool stop;
    uint64_t resize_latency_ns; /* sleep before recording a request for a new size */
    ax_atomic_u32 stalled;      /* records nothing while set */
    ax_thread thread;
} scene;

static void sleep_ns(ax_mutex *m, ax_cond *c, uint64_t ns)
{
    const uint64_t end = ax_time_monotonic_ns() + ns;
    while (ax_time_monotonic_ns() < end) {
        ax_cond_wait_timeout(c, m, end - ax_time_monotonic_ns());
    }
}

static int scene_main(void *arg)
{
    scene *sc = (scene *)arg;
    uint64_t seen = 0;
    float last_w = 0.0f, last_h = 0.0f;
    for (;;) {
        ax_scene_request q;
        ax_ir_builder *b;
        ax_mutex_lock(&sc->mutex);
        while (sc->requests == seen && !sc->stop) {
            ax_cond_wait_timeout(&sc->cond, &sc->mutex, 50u * 1000000u);
        }
        if (sc->stop) {
            ax_mutex_unlock(&sc->mutex);
            return 0;
        }
        q = sc->request;
        seen = sc->requests;
        if (ax_atomic_load_u32(&sc->stalled, AX_MO_ACQUIRE) != 0) {
            ax_mutex_unlock(&sc->mutex);
            continue;
        }
        if ((q.logical_width != last_w || q.logical_height != last_h) && last_w != 0.0f &&
            sc->resize_latency_ns != 0) {
            sleep_ns(&sc->mutex, &sc->cond, sc->resize_latency_ns);
        }
        ax_mutex_unlock(&sc->mutex);
        b = ax_ir_mailbox_begin(&sc->mailbox, q.logical_width, q.logical_height, q.frame_time_ns,
                                ax_color_make(0.1f, 0.1f, 0.12f, 1.0f));
        ax_ir_rect(b, ax_rect_make(8.5f, 8.5f, q.logical_width - 17.0f, 20.0f),
                   ax_color_make(1, 1, 1, 1));
        if (ax_ir_mailbox_publish(&sc->mailbox) != AX_OK) {
            return 1;
        }
        last_w = q.logical_width;
        last_h = q.logical_height;
    }
}

static void on_request(void *ctx, const ax_scene_request *q)
{
    scene *sc = (scene *)ctx;
    ax_mutex_lock(&sc->mutex);
    sc->request = *q;
    sc->requests++;
    ax_cond_signal(&sc->cond);
    ax_mutex_unlock(&sc->mutex);
}

/* ---- the render thread ------------------------------------------------------------------------
 */

typedef struct render {
    ax_surface surface;
    ax_window window;
    scene *sc;
    uint64_t max_wait_ns;
    uint32_t lose_device_at; /* frame with content, 0 = never */

    ax_atomic_u32 stop;
    ax_atomic_u32 state;      /* 0 starting, 1 running, 2 no adapter, 3 failed */
    ax_atomic_u32 frames;     /* frames presented (cleared or with content) */
    ax_atomic_u32 content;    /* frames presented with a snapshot drawn */
    ax_atomic_u32 acks;       /* frames that acknowledged a configure */
    ax_atomic_u32 violations; /* acknowledged with another size without ACCEPT_ANY_SIZE */
    ax_atomic_u32 forced;     /* stats mirrors, updated after every step */
    ax_atomic_u32 hidden;
    ax_atomic_u32 late;
    ax_atomic_u32 failures;
    ax_atomic_u32 cleared_after_loss;

    ax_render_frame_stats stats; /* final, read after join */
    uint32_t generation;
    uint32_t uncaptured_errors;
} render;

static const ax_renderer_vtbl *g_real;
static render *g_render; /* the one running render thread (tests run one at a time) */

static ax_result proxy_render(void *self, ax_surface surface, const ax_render_ir *ir,
                              ax_frame_info info)
{
    ax_result r = g_real->render(self, surface, ir, info);
    const ax_frame_result *res = info.result;
    if (r == AX_OK && res != NULL &&
        (res->status == AX_FRAME_PRESENTED || res->status == AX_FRAME_CLEARED) &&
        res->target.acks_configure) {
        ax_atomic_fetch_add_u32(&g_render->acks, 1, AX_MO_RELAXED);
        if (ir != NULL && (info.flags & AX_FRAME_ACCEPT_ANY_SIZE) == 0 &&
            (ir->logical_width != res->target.logical_width ||
             ir->logical_height != res->target.logical_height)) {
            ax_atomic_fetch_add_u32(&g_render->violations, 1, AX_MO_RELAXED);
        }
    }
    return r;
}

static int render_main(void *arg)
{
    render *p = (render *)arg;
    ax_renderer_vtbl proxy;
    ax_render_wgpu_desc wd;
    ax_renderer_desc rd;
    ax_render_frame_desc fd;
    ax_render_frame rf;
    void *self = NULL;
    uint32_t presented = 0, content = 0;
    bool lost = false;
    ax_result r;

    axt_gpu_pin_drivers();
    g_real = ax_render_wgpu_vtbl();
    g_render = p;
    proxy = *g_real;
    proxy.render = proxy_render;

    memset(&wd, 0, sizeof wd);
    wd.chain.type = AX_RENDERER_CHAIN_WGPU;
    wd.adapter = AX_GPU_ADAPTER_SOFTWARE;
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.next = &wd.chain;
    rd.initial_background = ax_color_make(0, 0, 0, 1);
    r = proxy.create(&rd, &self);
    if (r != AX_OK) {
        ax_atomic_store_u32(&p->state, r == AX_ERR_UNSUPPORTED ? 2u : 3u, AX_MO_RELEASE);
        return 0;
    }
    if (proxy.attach_surface(self, p->surface) != AX_OK) {
        proxy.destroy(self);
        ax_atomic_store_u32(&p->state, 3, AX_MO_RELEASE);
        return 0;
    }
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.vtbl = &proxy;
    fd.renderer = self;
    fd.surface = p->surface;
    fd.window = p->window;
    fd.mailbox = &p->sc->mailbox;
    fd.request = on_request;
    fd.request_ctx = p->sc;
    fd.max_wait_ns = p->max_wait_ns;
    ax_render_frame_init(&rf, &fd);
    ax_atomic_store_u32(&p->state, 1, AX_MO_RELEASE);

    while (ax_atomic_load_u32(&p->stop, AX_MO_ACQUIRE) == 0) {
        ax_frame_result res;
        ax_render_frame_stats st;
        r = ax_render_frame_step(&rf, true, 50u * 1000000u, &res);
        if (r != AX_OK && r != AX_ERR_DEVICE_LOST) {
            ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
            break;
        }
        if (r == AX_OK && (res.status == AX_FRAME_PRESENTED || res.status == AX_FRAME_CLEARED)) {
            presented++;
            ax_atomic_store_u32(&p->frames, presented, AX_MO_RELEASE);
            if (lost && res.status == AX_FRAME_CLEARED) {
                ax_atomic_fetch_add_u32(&p->cleared_after_loss, 1, AX_MO_RELAXED);
            }
            if (res.status == AX_FRAME_PRESENTED) {
                content++;
                ax_atomic_store_u32(&p->content, content, AX_MO_RELEASE);
            }
            if (content == p->lose_device_at && !lost) {
                ax__gpu_test_simulate_loss(ax_render_wgpu_gpu((ax_render_wgpu *)self));
                lost = true;
            }
        }
        st.struct_size = sizeof st;
        ax_render_frame_get_stats(&rf, &st, false);
        ax_atomic_store_u32(&p->forced, (uint32_t)st.forced_acks, AX_MO_RELEASE);
        ax_atomic_store_u32(&p->hidden, (uint32_t)st.hidden_acks, AX_MO_RELEASE);
        ax_atomic_store_u32(&p->late, (uint32_t)st.scene_late, AX_MO_RELEASE);
    }
    p->stats.struct_size = sizeof p->stats;
    ax_render_frame_get_stats(&rf, &p->stats, false);
    p->generation = ax_gpu_device_generation(ax_render_wgpu_gpu((ax_render_wgpu *)self));
    p->uncaptured_errors =
        ax_gpu_uncaptured_error_count(ax_render_wgpu_gpu((ax_render_wgpu *)self));
    fprintf(stderr,
            "render: %u frames, presented %llu, cleared %llu, scene late %llu, forced %llu, "
            "hidden %llu, wait max %.2f ms\n",
            presented, (unsigned long long)p->stats.presented, (unsigned long long)p->stats.cleared,
            (unsigned long long)p->stats.scene_late, (unsigned long long)p->stats.forced_acks,
            (unsigned long long)p->stats.hidden_acks, (double)p->stats.wait_ns_max / 1e6);
    proxy.detach_surface(self, p->surface);
    proxy.destroy(self);
    return 0;
}

/* ---- fixture ----------------------------------------------------------------------------------
 */

typedef struct fixture {
    axt_app t;
    ax_window window;
    scene sc;
    render p;
    ax_thread thread;
} fixture;

static bool started(void *ctx)
{
    return ax_atomic_load_u32(&((render *)ctx)->state, AX_MO_ACQUIRE) != 0;
}

typedef struct goal {
    ax_atomic_u32 *counter;
    uint32_t at_least;
    render *p;
} goal;

static bool reached(void *ctx)
{
    goal *g = (goal *)ctx;
    return ax_atomic_load_u32(g->counter, AX_MO_ACQUIRE) >= g->at_least ||
           ax_atomic_load_u32(&g->p->failures, AX_MO_ACQUIRE) != 0;
}

static bool pump_until_count(fixture *f, ax_atomic_u32 *counter, uint32_t at_least)
{
    goal g;
    g.counter = counter;
    g.at_least = at_least;
    g.p = &f->p;
    return axt_pump_until(f->t.app, reached, &g, AXT_SECONDS(20));
}

/* Frames with content: the first ones are cleared while the pipeline compiles (slow on lavapipe
 * under sanitizers). */
static bool pump_frames(fixture *f, uint32_t more)
{
    return pump_until_count(f, &f->p.content,
                            ax_atomic_load_u32(&f->p.content, AX_MO_ACQUIRE) + more);
}

/* 1 running, 0 skip (no software adapter), -1 failed (reported). */
static int fixture_start(fixture *f, uint64_t max_wait_ns, uint64_t resize_latency_ns,
                         uint32_t lose_device_at)
{
    ax_window_desc d = axt_window_desc(320, 240, AX_DECORATIONS_NONE);
    uint32_t state;
    memset(f, 0, sizeof *f);
    if (axt_app_create(&f->t, 0) != AX_OK || ax_window_create(f->t.app, &d, &f->window) != AX_OK ||
        ax_window_get_surface(f->window, &f->p.surface) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "app or window creation failed");
        return -1;
    }
    ax_ir_mailbox_init(&f->sc.mailbox, ax_default_allocator());
    ax_mutex_init(&f->sc.mutex);
    ax_cond_init(&f->sc.cond);
    ax_atomic_init_u32(&f->sc.stalled, 0);
    ax_atomic_init_u32(&f->p.content, 0);
    f->sc.resize_latency_ns = resize_latency_ns;
    f->p.window = f->window;
    f->p.sc = &f->sc;
    f->p.max_wait_ns = max_wait_ns;
    f->p.lose_device_at = lose_device_at;
    if (ax_thread_create(&f->sc.thread, scene_main, &f->sc) != AX_OK ||
        ax_thread_create(&f->thread, render_main, &f->p) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "thread creation failed");
        return -1;
    }
    axt_pump_until(f->t.app, started, &f->p, AXT_SECONDS(20));
    state = ax_atomic_load_u32(&f->p.state, AX_MO_ACQUIRE);
    if (state == 1) {
        return 1;
    }
    ax_thread_join(&f->thread);
    ax_mutex_lock(&f->sc.mutex);
    f->sc.stop = true;
    ax_cond_signal(&f->sc.cond);
    ax_mutex_unlock(&f->sc.mutex);
    ax_thread_join(&f->sc.thread);
    ax_window_destroy(f->window);
    axt_app_destroy(&f->t);
    if (state == 2) {
        const char *req = getenv("AX_REQUIRE_GPU_TESTS");
        if (req != NULL && strcmp(req, "1") == 0) {
            ax_test_fail(__FILE__, __LINE__, "AX_REQUIRE_GPU_TESTS=1 but no software adapter");
            return -1;
        }
        return 0;
    }
    ax_test_fail(__FILE__, __LINE__, "the render thread failed to start (state %u)", state);
    return -1;
}

static size_t fixture_stop(fixture *f)
{
    ax_atomic_store_u32(&f->p.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&f->thread);
    ax_mutex_lock(&f->sc.mutex);
    f->sc.stop = true;
    ax_cond_signal(&f->sc.cond);
    ax_mutex_unlock(&f->sc.mutex);
    ax_thread_join(&f->sc.thread);
    ax_cond_deinit(&f->sc.cond);
    ax_mutex_deinit(&f->sc.mutex);
    ax_ir_mailbox_deinit(&f->sc.mailbox);
    ax_window_destroy(f->window);
    return axt_app_destroy(&f->t);
}

#define START(f, max_wait, latency, lose_at)                                             \
    do {                                                                                 \
        int axt__s;                                                                      \
        AXT_REQUIRE_COMPOSITOR();                                                        \
        axt__s = fixture_start((f), (max_wait), (latency), (lose_at));                   \
        if (axt__s == 0) {                                                               \
            AX_SKIP("no software Vulkan adapter (lavapipe) for the presentation tests"); \
        }                                                                                \
        if (axt__s < 0) {                                                                \
            return;                                                                      \
        }                                                                                \
    } while (0)

static void check_common(fixture *f)
{
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f->p.failures, AX_MO_RELAXED), 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f->p.violations, AX_MO_RELAXED), 0);
    AX_CHECK_EQ_U(f->p.uncaptured_errors, 0);
    AX_CHECK_EQ_U(f->p.stats.counts.unknown_items, 0);
}

#define MS(n) ((uint64_t)(n) * 1000000u)

/* ---- tests ------------------------------------------------------------------------------------
 */

AX_TEST(snapshots_are_presented)
{
    fixture f;
    START(&f, 0, 0, 0);
    AX_CHECK(pump_frames(&f, 40));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_common(&f);
    AX_CHECK(f.p.stats.presented >= 30);
    AX_CHECK(f.p.stats.counts.instances >= 30); /* one rect per snapshot */
    AX_CHECK_EQ_U(f.p.stats.forced_acks, 0);
}

AX_TEST(resizes_wait_for_a_late_scene)
{
    /* The scene takes 5 ms to record a new size; the bound (500 ms) is never reached. */
    fixture f;
    START(&f, MS(500), MS(5), 0);
    AX_CHECK(pump_frames(&f, 10));
    ax__wl_test_set_maximized(f.window, true);
    AX_CHECK(pump_until_count(&f, &f.p.late, 1));
    AX_CHECK(pump_frames(&f, 10));
    ax__wl_test_set_maximized(f.window, false);
    AX_CHECK(pump_until_count(&f, &f.p.late, 2));
    AX_CHECK(pump_frames(&f, 10));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_common(&f);
    AX_CHECK(f.p.stats.scene_late >= 2);
    AX_CHECK_EQ_U(f.p.stats.forced_acks, 0);
    AX_CHECK_EQ_U(f.p.stats.hidden_acks, 0);
    AX_CHECK(f.p.stats.wait_ns_max >= MS(4) && f.p.stats.wait_ns_max < MS(500));
}

AX_TEST(a_stalled_scene_is_waited_for_only_up_to_the_bound)
{
    fixture f;
    START(&f, MS(30), 0, 0);
    AX_CHECK(pump_frames(&f, 10));
    ax_atomic_store_u32(&f.sc.stalled, 1, AX_MO_RELEASE);
    ax__wl_test_set_maximized(f.window, true);
    AX_CHECK(pump_until_count(&f, &f.p.forced, 1));
    AX_CHECK(pump_frames(&f, 5)); /* frames go on with the old snapshot */
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_common(&f);
    AX_CHECK(f.p.stats.forced_acks >= 1);
    AX_CHECK(f.p.stats.wait_ns_max >= MS(30) && f.p.stats.wait_ns_max < MS(1000));
}

/* Decision 40: a hidden window must keep acknowledging its configures. With the scene stalled and
 * a 10 s bound, the configure is still acknowledged at once. */
AX_TEST(a_hidden_window_acknowledges_at_once)
{
    fixture f;
    START(&f, MS(10000), 0, 0);
    AX_CHECK(pump_frames(&f, 10));
    ax_atomic_store_u32(&f.sc.stalled, 1, AX_MO_RELEASE);
    ax__wl_test_set_occluded(f.window, true);
    ax__wl_test_set_maximized(f.window, true);
    AX_CHECK(pump_until_count(&f, &f.p.hidden, 1));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_common(&f);
    AX_CHECK(f.p.stats.hidden_acks >= 1);
    AX_CHECK_EQ_U(f.p.stats.forced_acks, 0);
    AX_CHECK(f.p.stats.wait_ns_max < MS(1000));
}

AX_TEST(frames_are_cleared_while_a_lost_device_recovers)
{
    fixture f;
    START(&f, 0, 0, 10);
    AX_CHECK(pump_frames(&f, 30));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_common(&f);
    AX_CHECK_EQ_U(f.p.generation, 2);
    AX_CHECK(f.p.stats.presented >= 30);
#if defined(AX_WEBGPU_DAWN)
    /* Dawn reports the new pipeline only from a later poll: the first frames are cleared. */
    AX_CHECK(ax_atomic_load_u32(&f.p.cleared_after_loss, AX_MO_RELAXED) >= 1);
#endif
}
