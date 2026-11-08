/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The runtime: surfaces, the scene stage (ticks and the presentation watermark) and
 * single-threaded frames. See ax_runtime_sdk.h for the order of a tick and decision 77.
 */
#include <string.h>

#include "rt_internal.h"

#include <axion/ax_error.h>
#include <axion/ax_txn.h>
#include <axion/base/ax_assert.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_frame.h>
#include <axion/core/ax_world_sdk.h>

/* One runtime per world: as many as worlds can exist (AX_MAX_WORLDS, the headless host's
 * instances). */
#define AX_MAX_RUNTIMES AX_MAX_WORLDS
#define CMD_CAPACITY    128u

static ax_atomic_ptr g_runtimes[AX_MAX_RUNTIMES];
static ax_atomic_u32 g_runtime_generation = AX_ATOMIC_INIT(1);

rt_impl *ax__rt_from_handle(ax_runtime rt)
{
    uint32_t slot = ax_handle_slot(rt.h);
    rt_impl *r;
    if (rt.h == 0 || slot >= AX_MAX_RUNTIMES) {
        return NULL;
    }
    r = (rt_impl *)ax_atomic_load_ptr(&g_runtimes[slot], AX_MO_ACQUIRE);
    if (r == NULL || r->generation != ax_handle_generation(rt.h)) {
        return NULL;
    }
    return r;
}

static ax_runtime handle_of(const rt_impl *r)
{
    ax_runtime h;
    h.h = ax_handle_make(r->slot, r->generation);
    return h;
}

/* ---- Waking ---------------------------------------------------------------------------------- */

void ax__rt_wake_scene(rt_impl *r)
{
    /* Dekker handshake with the scene thread's wait (threads.c): bump, then check the flag. */
    ax_atomic_fetch_add_u64(&r->scene_gen, 1, AX_MO_SEQ_CST);
    if (ax_atomic_load_u32(&r->scene_sleeping, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&r->scene_lock);
        ax_cond_signal(&r->scene_cond);
        ax_mutex_unlock(&r->scene_lock);
    }
}

void ax__rt_wake_render(rt_impl *r)
{
    ax_mutex_lock(&r->render_lock);
    ax_atomic_fetch_add_u64(&r->render_gen, 1, AX_MO_SEQ_CST);
    ax_cond_signal(&r->render_cond);
    ax_mutex_unlock(&r->render_lock);
}

void ax__rt_signal_ack(rt_impl *r)
{
    ax_mutex_lock(&r->ack_lock);
    ax_cond_broadcast(&r->ack_cond);
    ax_mutex_unlock(&r->ack_lock);
}

/* ---- Lifecycle ------------------------------------------------------------------------------- */

static void free_runtime(rt_impl *r)
{
    ax_allocator a = r->allocator;
    if (r->jobs != NULL) {
        if (ax_world_is_valid(r->desc.world)) {
            ax_world_set_jobs(r->desc.world, NULL); /* the scene role is the caller's again */
        }
        ax_jobs_destroy(r->jobs);
    }
    ax_spsc_ring_deinit(&r->scene_cmds);
    ax_spsc_ring_deinit(&r->render_cmds);
    if (r->surfaces != NULL) {
        uint32_t i;
        for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
            ax_ir_mailbox_deinit(&r->surfaces[i].mailbox);
        }
        ax_mem_free(&a, r->surfaces, sizeof(rt_surface) * AX_RUNTIME_MAX_SURFACES);
    }
    ax_cond_deinit(&r->scene_cond);
    ax_mutex_deinit(&r->scene_lock);
    ax_cond_deinit(&r->ack_cond);
    ax_mutex_deinit(&r->ack_lock);
    ax_cond_deinit(&r->render_cond);
    ax_mutex_deinit(&r->render_lock);
    ax_mem_free(&a, r, sizeof *r);
}

static void on_commit(void *user);

ax_result ax_rt_create(const ax_rt_desc *desc, ax_runtime *out_runtime)
{
    const ax_allocator *a = ax_default_allocator();
    ax_jobs_desc jd;
    rt_impl *r;
    uint32_t slot, i;
    ax_result res;

    AX_ASSERT(desc != NULL && out_runtime != NULL);
    out_runtime->h = 0;
    if (!ax_world_is_valid(desc->world)) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_runtime_create: invalid world");
    }
    if (desc->mode != AX_RUNTIME_THREADED && desc->mode != AX_RUNTIME_SINGLE_THREADED) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_runtime_create: unknown mode %u",
                            (unsigned)desc->mode);
    }
    if (desc->mode == AX_RUNTIME_THREADED && !ax_threads_supported()) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_runtime_create: threaded mode needs threads; use "
                            "AX_RUNTIME_SINGLE_THREADED");
    }
    if (desc->presenter != NULL && desc->presenter->struct_size < sizeof(ax_rt_presenter_vtbl)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_runtime_create: presenter vtbl too small");
    }
    r = (rt_impl *)ax_mem_alloc(a, sizeof *r, AX_ALIGNOF(rt_impl));
    if (r == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create: out of memory");
    }
    memset(r, 0, sizeof *r);
    r->allocator = *a;
    r->desc = *desc;
    ax_mutex_init(&r->scene_lock);
    ax_cond_init(&r->scene_cond);
    ax_mutex_init(&r->ack_lock);
    ax_cond_init(&r->ack_cond);
    ax_mutex_init(&r->render_lock);
    ax_cond_init(&r->render_cond);
    ax_atomic_init_u64(&r->render_gen, 0);
    ax_atomic_init_u64(&r->scene_gen, 0);
    ax_atomic_init_u32(&r->scene_sleeping, 0);
    ax_atomic_init_u32(&r->stop, 0);
    ax_atomic_init_u32(&r->scene_bound, 0);

    r->surfaces = (rt_surface *)ax_mem_alloc(a, sizeof(rt_surface) * AX_RUNTIME_MAX_SURFACES,
                                             AX_ALIGNOF(rt_surface));
    if (r->surfaces == NULL ||
        ax_spsc_ring_init(&r->scene_cmds, a, CMD_CAPACITY, sizeof(rt_cmd)) != AX_OK ||
        ax_spsc_ring_init(&r->render_cmds, a, CMD_CAPACITY, sizeof(rt_cmd)) != AX_OK) {
        free_runtime(r);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create: out of memory");
    }
    memset(r->surfaces, 0, sizeof(rt_surface) * AX_RUNTIME_MAX_SURFACES);
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        ax_ir_mailbox_init(&r->surfaces[i].mailbox, &r->allocator);
    }

    jd = ax_jobs_default_desc();
    jd.allocator = &r->allocator;
    jd.mode = desc->mode == AX_RUNTIME_SINGLE_THREADED ? AX_JOBS_INLINE : jd.mode;
    if (ax_jobs_create(&jd, &r->jobs) != AX_OK) {
        free_runtime(r);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create: no job system");
    }

    r->generation = ax_atomic_fetch_add_u32(&g_runtime_generation, 1, AX_MO_RELAXED);
    if (r->generation == 0) {
        r->generation = ax_atomic_fetch_add_u32(&g_runtime_generation, 1, AX_MO_RELAXED);
    }
    for (slot = 0; slot < AX_MAX_RUNTIMES; ++slot) {
        void *expected = NULL;
        if (ax_atomic_compare_exchange_ptr(&g_runtimes[slot], &expected, r, AX_MO_ACQ_REL,
                                           AX_MO_RELAXED)) {
            break;
        }
    }
    if (slot == AX_MAX_RUNTIMES) {
        free_runtime(r);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create: at most %u runtimes",
                            AX_MAX_RUNTIMES);
    }
    r->slot = slot;
    *out_runtime = handle_of(r);

    if (desc->mode != AX_RUNTIME_THREADED) {
        ax_world_set_jobs(desc->world, r->jobs); /* inline: the caller is the scene thread */
    } else {
        ax_world_set_commit_hook(desc->world, on_commit, r); /* wakes resting surfaces */
        res = ax__rt_start_threads(r);
        if (res != AX_OK) {
            ax_atomic_store_ptr(&g_runtimes[slot], NULL, AX_MO_RELEASE);
            free_runtime(r);
            out_runtime->h = 0;
            return res;
        }
    }
    return AX_OK;
}

ax_result ax_runtime_create(const ax_runtime_desc *desc, ax_runtime *out_runtime)
{
    ax_rt_desc d;
    AX_ASSERT(out_runtime != NULL);
    out_runtime->h = 0;
    if (desc == NULL || desc->struct_size < sizeof(ax_runtime_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH,
                            "ax_runtime_create: ax_runtime_desc.struct_size must be >= %u",
                            (unsigned)sizeof(ax_runtime_desc));
    }
    memset(&d, 0, sizeof d);
    d.world = desc->world;
    d.mode = (ax_runtime_mode)desc->mode;
    return ax_rt_create(&d, out_runtime);
}

/* A commit while surfaces rest without frames (decision 95): each one asks for a frame. App
 * thread: the surfaces it added are its own to read. */
static void on_commit(void *user)
{
    rt_impl *r = (rt_impl *)user;
    const ax_rt_presenter_vtbl *p = r->desc.presenter;
    uint32_t i;
    if (p == NULL || p->struct_size < sizeof(ax_rt_presenter_vtbl) || p->wake == NULL) {
        return;
    }
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        const rt_surface *s = &r->surfaces[i];
        if (s->app_used && !s->removing) {
            p->wake(r->desc.presenter_self, i, s->native);
        }
    }
}

bool ax_rt_surface_animating(ax_runtime rt, uint32_t surface)
{
    rt_impl *r = ax__rt_from_handle(rt);
    if (r == NULL || surface >= AX_RUNTIME_MAX_SURFACES) {
        return false;
    }
    return ax_atomic_load_u32(&r->surfaces[surface].animating, AX_MO_ACQUIRE) != 0;
}

ax_result ax_runtime_destroy(ax_runtime runtime)
{
    rt_impl *r = ax__rt_from_handle(runtime);
    uint32_t i;
    if (r == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_runtime_destroy: invalid runtime");
    }
    if (r->desc.mode == AX_RUNTIME_THREADED && ax_world_is_valid(r->desc.world)) {
        ax_world_set_commit_hook(r->desc.world, NULL, NULL);
    }
    if (r->threads_running) {
        ax__rt_stop_threads(r); /* detaches every surface on the render thread */
        for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
            if (r->surfaces[i].scene_live) { /* the scene role is back on this thread */
                ax_world_remove_root(r->desc.world, r->surfaces[i].root);
            }
        }
    } else {
        for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
            rt_surface *s = &r->surfaces[i];
            if (s->app_used) {
                if (s->scene_live) {
                    ax_world_remove_root(r->desc.world, s->root);
                }
                if (s->render_live && r->desc.presenter != NULL) {
                    r->desc.presenter->detach(r->desc.presenter_self, i);
                }
            }
        }
        if (r->desc.presenter != NULL && r->desc.presenter->destroy != NULL) {
            r->desc.presenter->destroy(r->desc.presenter_self);
        }
    }
    if (r->desc.cleanup != NULL) {
        r->desc.cleanup(r->desc.cleanup_user);
    }
    ax_atomic_store_ptr(&g_runtimes[r->slot], NULL, AX_MO_RELEASE);
    free_runtime(r);
    return AX_OK;
}

ax_job_system *ax_rt_jobs(ax_runtime rt)
{
    rt_impl *r = ax__rt_from_handle(rt);
    return r != NULL ? r->jobs : NULL;
}

/* ---- Surfaces (app thread) ------------------------------------------------------------------- */

static rt_surface *surface_at(rt_impl *r, uint32_t i)
{
    return r != NULL && i < AX_RUNTIME_MAX_SURFACES ? &r->surfaces[i] : NULL;
}

/* App thread: queues a command; false when the ring is full. */
static bool push_cmd(ax_spsc_ring *ring, uint32_t kind, uint32_t surface)
{
    rt_cmd c;
    c.kind = kind;
    c.surface = surface;
    return ax_spsc_ring_push(ring, &c);
}

ax_result ax_rt_add_surface(ax_runtime rt, ax_node root, const ax_rt_surface_desc *desc,
                            uint32_t *out_surface)
{
    rt_impl *r = ax__rt_from_handle(rt);
    uint32_t i;
    rt_surface *s = NULL;
    if (out_surface != NULL) {
        *out_surface = UINT32_MAX;
    }
    if (r == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_rt_add_surface: invalid runtime");
    }
    if (AX_HANDLE_IS_NULL(root) || desc == NULL || ax_rt_surface_of_root(rt, root) != UINT32_MAX) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_rt_add_surface: a root node not attached yet is required");
    }
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        if (!r->surfaces[i].app_used) {
            s = &r->surfaces[i];
            break;
        }
    }
    if (s == NULL || ax_spsc_ring_count(&r->scene_cmds) + 1u >= CMD_CAPACITY ||
        ax_spsc_ring_count(&r->render_cmds) + 1u >= CMD_CAPACITY) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_rt_add_surface: at most %u surfaces",
                            AX_RUNTIME_MAX_SURFACES);
    }
    /* Everything the other threads read is set before the commands publish the slot. */
    s->root = root;
    s->native = desc->native_surface;
    s->window = desc->window;
    s->host_refresh_ns = desc->refresh_ns;
    s->host_width = desc->width;
    s->host_height = desc->height;
    s->host_scale = desc->scale > 0.0f ? desc->scale : 1.0f;
    ax_tb_init(&s->req_tb);
    memset(s->req, 0, sizeof s->req);
    ax_atomic_store_u64(&s->shown_ir, 0, AX_MO_RELAXED);
    ax_atomic_store_u64(&s->shown_ns, 0, AX_MO_RELAXED);
    ax_atomic_store_u32(&s->shown_world_clock, 0, AX_MO_RELAXED);
    ax_atomic_store_u32(&s->animating, 0, AX_MO_RELAXED);
    ax_atomic_store_u32(&s->scene_done, 0, AX_MO_RELAXED);
    ax_atomic_store_u32(&s->render_done, 0, AX_MO_RELAXED);
    s->app_used = true;
    s->removing = false;
    (void)push_cmd(&r->scene_cmds, RT_CMD_ADD, i);
    (void)push_cmd(&r->render_cmds, RT_CMD_ADD, i);
    if (r->desc.mode == AX_RUNTIME_SINGLE_THREADED) {
        ax__rt_render_commands(r);
    } else {
        ax__rt_wake_scene(r);
        ax__rt_wake_render(r);
    }
    if (out_surface != NULL) {
        *out_surface = i;
    }
    return AX_OK;
}

static bool both_done(rt_surface *s)
{
    return ax_atomic_load_u32(&s->scene_done, AX_MO_ACQUIRE) != 0 &&
           ax_atomic_load_u32(&s->render_done, AX_MO_ACQUIRE) != 0;
}

ax_result ax_rt_remove_surface(ax_runtime rt, uint32_t surface)
{
    rt_impl *r = ax__rt_from_handle(rt);
    rt_surface *s = surface_at(r, surface);
    if (s == NULL || !s->app_used) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_rt_remove_surface: no such surface");
    }
    if (!s->removing) { /* a retry after a timeout queues nothing new: it waits again */
        while (!push_cmd(&r->scene_cmds, RT_CMD_REMOVE, surface)) {
            ax_thread_yield(); /* the scene drains commands at every pass */
        }
        while (!push_cmd(&r->render_cmds, RT_CMD_REMOVE, surface)) {
            ax_thread_yield();
        }
        s->removing = true;
    }
    if (r->desc.mode == AX_RUNTIME_SINGLE_THREADED || !r->threads_running) {
        (void)ax_rt_scene_step(rt);
        ax__rt_render_commands(r);
    } else {
        uint64_t until = ax_time_monotonic_ns() + 1000000000u;
        ax__rt_wake_scene(r);
        ax__rt_wake_render(r);
        ax_mutex_lock(&r->ack_lock);
        while (!both_done(s) && ax_time_monotonic_ns() < until) {
            ax_cond_wait_timeout(&r->ack_cond, &r->ack_lock, 10000000u);
        }
        ax_mutex_unlock(&r->ack_lock);
        if (!both_done(s)) {
            /* Still attached: the render thread may be presenting it. The caller retries. */
            return ax_set_error(AX_ERR_TIMEOUT,
                                "ax_rt_remove_surface: the runtime's threads did not let the "
                                "surface go within 1 s; it is still in use, call again");
        }
    }
    /* Neither side uses the mailbox any more: start the slot's next surface without the old
     * snapshots. */
    ax_ir_mailbox_deinit(&s->mailbox);
    ax_ir_mailbox_init(&s->mailbox, &r->allocator);
    s->app_used = false;
    s->removing = false;
    s->root.h = 0;
    return AX_OK;
}

uint32_t ax_rt_surface_of_root(ax_runtime rt, ax_node root)
{
    rt_impl *r = ax__rt_from_handle(rt);
    uint32_t i;
    for (i = 0; r != NULL && i < AX_RUNTIME_MAX_SURFACES; ++i) {
        if (r->surfaces[i].app_used && r->surfaces[i].root.h == root.h) {
            return i;
        }
    }
    return UINT32_MAX;
}

void ax_rt_set_surface_size(ax_runtime rt, uint32_t surface, float width, float height, float scale)
{
    rt_surface *s = surface_at(ax__rt_from_handle(rt), surface);
    if (s != NULL) {
        s->host_width = width;
        s->host_height = height;
        s->host_scale = scale > 0.0f ? scale : 1.0f;
    }
}

ax_ir_mailbox *ax_rt_surface_mailbox(ax_runtime rt, uint32_t surface)
{
    rt_surface *s = surface_at(ax__rt_from_handle(rt), surface);
    return s != NULL ? &s->mailbox : NULL;
}

uint64_t ax_rt_surface_native(ax_runtime rt, uint32_t surface)
{
    rt_surface *s = surface_at(ax__rt_from_handle(rt), surface);
    return s != NULL ? s->native : 0;
}

uint64_t ax_rt_surface_window(ax_runtime rt, uint32_t surface)
{
    rt_surface *s = surface_at(ax__rt_from_handle(rt), surface);
    return s != NULL ? s->window : 0;
}

ax_node ax_rt_surface_root(ax_runtime rt, uint32_t surface)
{
    rt_surface *s = surface_at(ax__rt_from_handle(rt), surface);
    ax_node none = AX_HANDLE_NULL_INIT;
    return s != NULL ? s->root : none;
}

/* ---- Render side ----------------------------------------------------------------------------- */

void ax__rt_render_commands(rt_impl *r)
{
    rt_cmd c;
    const ax_rt_presenter_vtbl *p = r->desc.presenter;
    while (ax_spsc_ring_pop(&r->render_cmds, &c)) {
        rt_surface *s = &r->surfaces[c.surface];
        if (c.kind == RT_CMD_ADD) {
            s->render_live = true;
            if (p != NULL && p->attach(r->desc.presenter_self, handle_of(r), c.surface, s->native,
                                       s->window) != AX_OK) {
                s->render_live = false; /* nothing presents it; the scene still records */
            }
        } else {
            if (s->render_live && p != NULL) {
                p->detach(r->desc.presenter_self, c.surface);
            }
            s->render_live = false;
            ax_atomic_store_u32(&s->render_done, 1, AX_MO_RELEASE);
            ax__rt_signal_ack(r);
        }
    }
}

void ax_rt_request_frame(ax_runtime rt, uint32_t surface, const ax_rt_request *request)
{
    rt_impl *r = ax__rt_from_handle(rt);
    rt_surface *s = surface_at(r, surface);
    if (s == NULL || request == NULL) {
        return;
    }
    if (r->desc.mode == AX_RUNTIME_SINGLE_THREADED && !request->resize &&
        !request->time_is_world_clock) {
        return; /* host frames set the time */
    }
    s->req[ax_tb_write_index(&s->req_tb)] = *request;
    ax_tb_publish(&s->req_tb);
    ax__rt_wake_scene(r);
}

void ax_rt_snapshot_presented(ax_runtime rt, uint32_t surface, uint64_t ir_sequence,
                              uint64_t present_ns, bool world_clock)
{
    rt_impl *r = ax__rt_from_handle(rt);
    rt_surface *s = surface_at(r, surface);
    if (s == NULL || ir_sequence <= ax_atomic_load_u64(&s->shown_ir, AX_MO_RELAXED)) {
        return;
    }
    /* The time and the clock first; the sequence publishes them (release). */
    ax_atomic_store_u64(&s->shown_ns, present_ns, AX_MO_RELAXED);
    ax_atomic_store_u32(&s->shown_world_clock, world_clock ? 1u : 0u, AX_MO_RELAXED);
    ax_atomic_store_u64(&s->shown_ir, ir_sequence, AX_MO_RELEASE);
    ax__rt_wake_scene(r);
}

/* ---- Scene side ------------------------------------------------------------------------------ */

static void scene_commands(rt_impl *r)
{
    rt_cmd c;
    while (ax_spsc_ring_pop(&r->scene_cmds, &c)) {
        rt_surface *s = &r->surfaces[c.surface];
        if (c.kind == RT_CMD_ADD) {
            s->scene_live = true;
            s->visible = true;
            s->last_time = 0;
            s->published_ir = ax_ir_mailbox_published(&s->mailbox);
            memset(s->snap_txn, 0, sizeof s->snap_txn);
            s->needs_txn = 0;
            s->shown_txn = 0;
            s->shown_time = 0;
            s->seen_ir = s->published_ir;
        } else {
            if (s->scene_live) {
                ax_world_remove_root(r->desc.world, s->root);
            }
            s->scene_live = false;
            ax_atomic_store_u32(&s->scene_done, 1, AX_MO_RELEASE);
            ax__rt_signal_ack(r);
        }
    }
}

static ax_color root_background(ax_world world, ax_node root)
{
    ax_value v;
    ax_color c = ax_color_make(0.0f, 0.0f, 0.0f, 1.0f);
    if (ax_scene_get(ax_world_scene(world), root, AX_PROP_BACKGROUND_COLOR, &v) == AX_OK) {
        c = v.color;
    }
    return c;
}

static void tick(rt_impl *r, uint32_t index, const ax_rt_request *q)
{
    rt_surface *s = &r->surfaces[index];
    ax_world world = r->desc.world;
    ax_scene *scene = ax_world_scene(world);
    uint64_t t =
        q->time_is_world_clock ? q->time_ns : ax_world_time_from_monotonic(world, q->time_ns);
    uint64_t deadline;
    uint64_t changed_seq = 0;

    /* 1. The frame's time on the world's clock, never backwards for this surface. */
    if (t < s->last_time) {
        t = s->last_time;
    }
    s->last_time = t;
    deadline = t > q->refresh_ns ? t - q->refresh_ns : 0;

    /* 2. Transactions committed up to the frame's latch point. */
    r->counters.applied += ax_world_apply_pending(world, deadline);
    r->counters.ticks++;
    if (q->resize) {
        r->counters.resize_ticks++;
    }
    if (!ax_node_store_is_live(&scene->store, s->root)) {
        return; /* the root's transaction has not applied yet */
    }
    /* 3. The root's viewport. */
    if (ax_world_set_root_viewport(world, s->root, q->logical_width, q->logical_height,
                                   q->scale > 0.0f ? q->scale : 1.0f) != AX_OK) {
        return; /* out of memory, or the root has gained a parent: nothing to record */
    }
    /* 4. Layout and implicit animations (sampled at the frame's time), then recording when this
     * surface's content changed. */
    (void)ax_world_position(world);
    (void)ax_world_animate(world, t);
    ax_atomic_store_u32(&s->animating, ax_world_root_animating(world, s->root) ? 1u : 0u,
                        AX_MO_RELEASE);
    ax_world_record_collect(world);
    if (ax_world_root_changed(world, s->root, &changed_seq)) {
        ax_ir_builder *b = ax_ir_mailbox_begin(&s->mailbox, q->logical_width, q->logical_height, t,
                                               root_background(world, s->root));
        if (ax_world_record_root(world, s->root, b) == AX_OK &&
            ax_ir_mailbox_publish(&s->mailbox) == AX_OK) {
            uint64_t ir = ax_ir_mailbox_published(&s->mailbox);
            s->published_ir = ir;
            s->snap_txn[ir % RT_SNAP_HISTORY] = ax_world_last_applied_seq(world);
            if (changed_seq > s->needs_txn) {
                s->needs_txn = changed_seq;
            }
            r->counters.snapshots++;
        }
    }
    /* 6. The mirror, after layout. 7. The flags, consumed. */
    (void)ax_world_publish_mirror(world);
    ax_scene_clear_dirty(scene);
}

/* What reached the screen, and the watermark for ax_txn_on_presented (decision 75): every
 * visible surface whose content changed must have shown a snapshot that includes the change. */
static void presentation(rt_impl *r)
{
    ax_world world = r->desc.world;
    uint64_t applied = ax_world_last_applied_seq(world);
    uint64_t until = applied;
    uint64_t time = 0;
    uint32_t i;
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        rt_surface *s = &r->surfaces[i];
        uint64_t ir;
        if (!s->scene_live) {
            continue; /* a closed window holds nothing back */
        }
        ir = ax_atomic_load_u64(&s->shown_ir, AX_MO_ACQUIRE);
        if (ir > s->seen_ir) {
            uint64_t ns = ax_atomic_load_u64(&s->shown_ns, AX_MO_RELAXED);
            bool world_clock = ax_atomic_load_u32(&s->shown_world_clock, AX_MO_RELAXED) != 0;
            s->seen_ir = ir;
            if (ir <= s->published_ir && s->published_ir - ir < RT_SNAP_HISTORY &&
                s->snap_txn[ir % RT_SNAP_HISTORY] > s->shown_txn) {
                s->shown_txn = s->snap_txn[ir % RT_SNAP_HISTORY];
            }
            s->shown_time = world_clock ? ns : ax_world_time_from_monotonic(world, ns);
        }
        if (s->visible && s->needs_txn > s->shown_txn && s->shown_txn < until) {
            until = s->shown_txn;
        }
        if (s->shown_time > time) {
            time = s->shown_time;
        }
    }
    if (time == 0) {
        time = ax_world_now(world);
    }
    ax_world_report_presented(world, until, time);
    if (until > r->counters.presented_until) {
        r->counters.presented_until = until;
    }
    (void)ax_world_flush_events(world);
}

/* Answers ax_measure_sync: an off-cycle layout of everything committed, published to the mirror.
 * The dirty flags stay for the next tick, which records what changed (decision 92). */
static void measure(rt_impl *r)
{
    ax_world world = r->desc.world;
    uint64_t want = ax_atomic_load_u64(&r->measure_req, AX_MO_ACQUIRE);
    if (want <= ax_atomic_load_u64(&r->measure_done, AX_MO_RELAXED)) {
        return;
    }
    r->counters.applied += ax_world_apply_pending(world, UINT64_MAX);
    (void)ax_world_position(world);
    (void)ax_world_publish_mirror(world);
    r->counters.measure_ticks++;
    ax_atomic_store_u64(&r->measure_done, want, AX_MO_RELEASE);
    ax__rt_signal_ack(r);
}

uint32_t ax_rt_scene_step(ax_runtime rt)
{
    rt_impl *r = ax__rt_from_handle(rt);
    uint32_t i, ticks = 0;
    if (r == NULL) {
        return 0;
    }
    r->in_step = true;
    scene_commands(r);
    measure(r);
    if (r->desc.pump_events != NULL) {
        r->desc.pump_events(r->desc.pump_user, rt);
    }
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        rt_surface *s = &r->surfaces[i];
        if (s->scene_live && ax_tb_has_new(&s->req_tb)) {
            ax_rt_request q = s->req[ax_tb_acquire(&s->req_tb)];
            tick(r, i, &q);
            ticks++;
        }
    }
    presentation(r);
    r->in_step = false;
    return ticks;
}

void ax_rt_set_surface_visible(ax_runtime rt, uint32_t surface, bool visible)
{
    rt_surface *s = surface_at(ax__rt_from_handle(rt), surface);
    if (s != NULL) {
        s->visible = visible;
    }
}

void ax_rt_get_counters(ax_runtime rt, ax_rt_counters *out)
{
    rt_impl *r = ax__rt_from_handle(rt);
    AX_ASSERT(out != NULL);
    memset(out, 0, sizeof *out);
    if (r != NULL) {
        *out = r->counters;
        out->measure_syncs = ax_atomic_load_u64(&r->measure_syncs, AX_MO_RELAXED);
        out->measure_sync_ns = ax_atomic_load_u64(&r->measure_sync_ns, AX_MO_RELAXED);
    }
}

/* ---- Synchronous measurement ----------------------------------------------------------------- */

#define MEASURE_TIMEOUT_NS 1000000000ull

ax_result ax_measure_sync(ax_runtime runtime, ax_node node, ax_rectf *out_frame)
{
    rt_impl *r = ax__rt_from_handle(runtime);
    ax_world world;
    uint64_t want, start;
    ax_result res;
    if (out_frame != NULL) {
        memset(out_frame, 0, sizeof *out_frame);
    }
    if (r == NULL || out_frame == NULL) {
        return ax_set_error(r == NULL ? AX_ERR_INVALID_HANDLE : AX_ERR_INVALID_ARGUMENT,
                            "ax_measure_sync: invalid runtime or NULL frame");
    }
    world = r->desc.world;
    if ((!r->threads_running && r->in_step) || ax_jobs_current_worker(r->jobs) != UINT32_MAX ||
        (r->threads_running &&
         ax_thread_current_id() == ax_atomic_load_u64(&r->scene_tid, AX_MO_ACQUIRE))) {
        return ax_set_error(AX_ERR_WRONG_THREAD,
                            "ax_measure_sync: called from the scene's own work; it would wait for "
                            "itself");
    }
    start = ax_time_monotonic_ns();
    want = ax_world_last_committed_seq(world);
    if (want == 0) {
        want = 1; /* nothing committed: still lay out what the scene has */
    }
    if (ax_atomic_load_u64(&r->measure_req, AX_MO_RELAXED) < want) {
        ax_atomic_store_u64(&r->measure_req, want, AX_MO_RELEASE);
    }
    if (!r->threads_running) {
        (void)ax_rt_scene_step(runtime); /* single-threaded: the caller is the scene */
    } else {
        uint64_t deadline = start + MEASURE_TIMEOUT_NS;
        ax__rt_wake_scene(r);
        ax_mutex_lock(&r->ack_lock);
        while (ax_atomic_load_u64(&r->measure_done, AX_MO_ACQUIRE) < want) {
            uint64_t now = ax_time_monotonic_ns();
            if (now >= deadline) {
                break;
            }
            (void)ax_cond_wait_timeout(&r->ack_cond, &r->ack_lock, deadline - now);
        }
        ax_mutex_unlock(&r->ack_lock);
        if (ax_atomic_load_u64(&r->measure_done, AX_MO_ACQUIRE) < want) {
            return ax_set_error(AX_ERR_TIMEOUT,
                                "ax_measure_sync: the scene thread did not answer within 1 s");
        }
    }
    (void)ax_world_mirror_sync(world);
    ax_atomic_fetch_add_u64(&r->measure_syncs, 1, AX_MO_RELAXED);
    ax_atomic_fetch_add_u64(&r->measure_sync_ns, ax_time_monotonic_ns() - start, AX_MO_RELAXED);
    res = ax_mirror_get_frame(world, node, out_frame);
    return res;
}

/* ---- Single-threaded frames ------------------------------------------------------------------ */

ax_result ax_host_frame(ax_runtime runtime, ax_node root, uint64_t target_time_ns)
{
    rt_impl *r = ax__rt_from_handle(runtime);
    uint32_t i;
    rt_surface *s;
    ax_rt_request q;
    int attempt;
    if (r == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_host_frame: invalid runtime");
    }
    if (r->desc.mode != AX_RUNTIME_SINGLE_THREADED) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_host_frame: the runtime runs its own threads");
    }
    i = ax_rt_surface_of_root(runtime, root);
    if (i == UINT32_MAX) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_host_frame: the root has no surface");
    }
    s = &r->surfaces[i];
    memset(&q, 0, sizeof q);
    q.logical_width = s->host_width;
    q.logical_height = s->host_height;
    q.scale = s->host_scale;
    q.time_ns = target_time_ns;
    q.refresh_ns = s->host_refresh_ns;
    q.time_is_world_clock = true;
    ax_rt_request_frame(runtime, i, &q);
    ax__rt_render_commands(r);
    (void)ax_rt_scene_step(runtime);
    /* The presenter may ask for another size (a configure): record it and present again, once
     * or twice, as the render thread's handshake does. */
    for (attempt = 0; attempt < 3 && r->desc.presenter != NULL && s->render_live; ++attempt) {
        ax_result res = r->desc.presenter->frame(r->desc.presenter_self, i);
        if (res != AX_OK) {
            return res;
        }
        if (!ax_tb_has_new(&s->req_tb)) {
            break;
        }
        (void)ax_rt_scene_step(runtime);
    }
    (void)ax_rt_scene_step(runtime); /* what was presented */
    return AX_OK;
}
