/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The threaded mode (decision 78): one scene thread and one render thread per runtime.
 *
 * Scene thread: takes the world's scene role and runs ax_rt_scene_step whenever something wakes
 * it (a frame request or a presentation report from the render thread, a command from the app
 * thread, a platform event) and at least every 100 ms. It never waits for the app thread: the only
 * things it waits on are its own wake-up and its condition variable.
 *
 * Render thread: takes the surfaces' commands, waits for frames on every surface at once
 * (presenter->wait) and runs presenter->frame for each one that is due. The frame step asks the
 * scene for the next frame and, on a configure of another size, waits a bounded time for the
 * scene's snapshot (decision 57); it never waits for the app thread either.
 */
#include <string.h>

#include "rt_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/core/ax_world_sdk.h>

#define SCENE_IDLE_NS  100000000u /* the scene's longest sleep */
#define RENDER_IDLE_NS 50000000u  /* the render thread's longest wait for frames */

static int scene_main(void *arg)
{
    rt_impl *r = (rt_impl *)arg;
    ax_runtime rt;
    rt.h = ax_handle_make(r->slot, r->generation);
    ax_world_bind_scene_thread(r->desc.world);
    ax_world_set_jobs(r->desc.world, r->jobs); /* layout's boundaries as frame jobs */
    ax_atomic_store_u64(&r->scene_tid, ax_thread_current_id(), AX_MO_RELEASE);
    ax_atomic_store_u32(&r->scene_bound, 1, AX_MO_RELEASE);
    while (ax_atomic_load_u32(&r->stop, AX_MO_ACQUIRE) == 0) {
        uint64_t gen = ax_atomic_load_u64(&r->scene_gen, AX_MO_SEQ_CST);
        (void)ax_rt_scene_step(rt);
        /* Sleep until woken (Dekker handshake with ax__rt_wake_scene). */
        ax_mutex_lock(&r->scene_lock);
        ax_atomic_store_u32(&r->scene_sleeping, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&r->scene_gen, AX_MO_SEQ_CST) == gen &&
            ax_atomic_load_u32(&r->stop, AX_MO_SEQ_CST) == 0) {
            ax_cond_wait_timeout(&r->scene_cond, &r->scene_lock, SCENE_IDLE_NS);
        }
        ax_atomic_store_u32(&r->scene_sleeping, 0, AX_MO_SEQ_CST);
        ax_mutex_unlock(&r->scene_lock);
    }
    return 0;
}

static int render_main(void *arg)
{
    rt_impl *r = (rt_impl *)arg;
    const ax_rt_presenter_vtbl *p = r->desc.presenter;
    const uint64_t wait_ns = r->desc.render_wait_ns != 0 ? r->desc.render_wait_ns : RENDER_IDLE_NS;
    uint32_t live[AX_RUNTIME_MAX_SURFACES];
    bool due[AX_RUNTIME_MAX_SURFACES];
    uint32_t i;

    while (ax_atomic_load_u32(&r->stop, AX_MO_ACQUIRE) == 0) {
        uint64_t gen = ax_atomic_load_u64(&r->render_gen, AX_MO_SEQ_CST);
        uint32_t n = 0;
        ax__rt_render_commands(r);
        for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
            if (r->surfaces[i].render_live) {
                live[n++] = i;
            }
        }
        if (n == 0 || p == NULL || p->wait == NULL) {
            /* Nothing to present: wait for a command. */
            ax_mutex_lock(&r->render_lock);
            if (ax_atomic_load_u64(&r->render_gen, AX_MO_SEQ_CST) == gen &&
                ax_atomic_load_u32(&r->stop, AX_MO_SEQ_CST) == 0) {
                ax_cond_wait_timeout(&r->render_cond, &r->render_lock, wait_ns);
            }
            ax_mutex_unlock(&r->render_lock);
            continue;
        }
        memset(due, 0, sizeof due);
        if (p->wait(r->desc.presenter_self, live, n, wait_ns, due) != AX_OK) {
            continue; /* the presenter recovers (a lost device) or the surface goes away */
        }
        for (i = 0; i < n; ++i) {
            if (due[i]) {
                (void)p->frame(r->desc.presenter_self, live[i]);
            }
        }
    }
    /* Let every surface go, then the presenter itself, on this thread. */
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        rt_surface *s = &r->surfaces[i];
        if (s->render_live && p != NULL) {
            p->detach(r->desc.presenter_self, i);
        }
        s->render_live = false;
    }
    if (p != NULL && p->destroy != NULL) {
        p->destroy(r->desc.presenter_self);
    }
    return 0;
}

ax_result ax__rt_start_threads(rt_impl *r)
{
    if (ax_thread_create(&r->scene_thread, scene_main, r) != AX_OK) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create: no scene thread");
    }
    if (ax_thread_create(&r->render_thread, render_main, r) != AX_OK) {
        ax_atomic_store_u32(&r->stop, 1, AX_MO_RELEASE);
        ax__rt_wake_scene(r);
        ax_thread_join(&r->scene_thread);
        ax_world_bind_scene_thread(r->desc.world);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create: no render thread");
    }
    /* The world's scene role has moved before the app thread goes on. */
    while (ax_atomic_load_u32(&r->scene_bound, AX_MO_ACQUIRE) == 0) {
        ax_thread_yield();
    }
    r->threads_running = true;
    return AX_OK;
}

void ax__rt_stop_threads(rt_impl *r)
{
    ax_atomic_store_u32(&r->stop, 1, AX_MO_SEQ_CST);
    ax__rt_wake_render(r);
    ax_thread_join(&r->render_thread);
    ax__rt_wake_scene(r);
    ax_thread_join(&r->scene_thread);
    r->threads_running = false;
    /* The scene role comes back to the app thread, which destroys the world later. */
    ax_world_bind_scene_thread(r->desc.world);
}
