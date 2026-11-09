/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The platform presenter (decision 78): the render thread presents windows with a renderer and
 * decision 57's frame step (ax_render_frame), and the scene thread forwards the app's window
 * events to the world.
 *
 * Presentation reports: each commit that presented a snapshot is remembered with the snapshot's
 * mailbox sequence (ax_surface_prepared_commit, decision 74); when the platform reports a newer
 * commit on screen (ax_surface_get_presented_commit), the newest snapshot at or before it goes to
 * the scene (ax_rt_snapshot_presented), which turns it into ax_txn_on_presented callbacks.
 */
#include <string.h>

#include <axion/text/ax_text.h>

#include "rt_internal.h"

#include <axion/ax_error.h>
#include <axion/ax_event.h>
#include <axion/ax_txn.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_world_sdk.h>
#include <axion/platform/ax_event_notify.h>
#include <axion/platform/ax_present.h>
#include <axion/render/ax_render_frame.h>
#include <axion/render/ax_render_soft_surface.h>
#include <axion/runtime/ax_runtime_platform.h>
#if defined(AX_RUNTIME_HAVE_WGPU)
#    include <axion/render/ax_render_wgpu_surface.h>
#endif

#define COMMIT_MAP 16u

typedef struct plat plat;

typedef struct plat_surface {
    plat *p;
    uint32_t index;
    /* render thread */
    bool used;
    ax_surface surface;
    ax_window window;
    ax_render_frame rf;
    ax_wake_reason reason;
    uint64_t map_commit[COMMIT_MAP];
    uint64_t map_ir[COMMIT_MAP];
    uint32_t map_next;
    uint64_t reported_commit;
    uint64_t reported_ir;
    /* counters, read from any thread */
    uint64_t window_handle; /* set by the app thread before the surface is added */
    ax_atomic_u64 frames, shown, presented, missed, scene_late, forced, hidden, refresh;
    ax_atomic_u64 rests, rest_ns;
    ax_atomic_u32 shown_w, shown_h; /* float bits */
} plat_surface;

struct plat {
    ax_allocator allocator;
    ax_app app;
    ax_atomic_u64 rt; /* for the event hook on the main thread */
    const ax_renderer_vtbl *vt;
    ax_renderer_desc rdesc;
    void *renderer;
    bool renderer_failed;
    bool fallback_soft;       /* no renderer was pinned: fall back to ax_render_soft */
    ax_atomic_ptr active_vt;  /* the renderer created, for any thread (ax_rt_renderer_vtbl) */
    ax_render_soft_desc soft; /* chained for ax_render_soft: tiles on the runtime's jobs */
    uint64_t max_wait_ns;
    bool notify_set; /* scene thread */
    plat_surface s[AX_RUNTIME_MAX_SURFACES];
};

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float bits_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static ax_runtime rt_of(plat *p)
{
    ax_runtime rt;
    rt.h = ax_atomic_load_u64(&p->rt, AX_MO_ACQUIRE);
    return rt;
}

/* ---- Render thread --------------------------------------------------------------------------- */

/* decision 57's frame step asks the scene for a frame: forward it as a runtime request. */
static void on_request(void *ctx, const ax_scene_request *q)
{
    plat_surface *ps = (plat_surface *)ctx;
    ax_rt_request r;
    memset(&r, 0, sizeof r);
    r.logical_width = q->logical_width;
    r.logical_height = q->logical_height;
    r.scale = q->scale;
    r.time_ns = q->frame_time_ns;
    r.refresh_ns = q->refresh_interval_ns;
    r.resize = q->resize;
    ax_rt_request_frame(rt_of(ps->p), ps->index, &r);
}

/* Chains ax_render_soft's options first: the caller's own, if its renderer desc chains them, with
 * the tiles on the runtime's job system when it names none. */
static void chain_soft(plat *p, ax_runtime rt)
{
    const ax_renderer_chain *c;
    memset(&p->soft, 0, sizeof p->soft);
    for (c = p->rdesc.next; c != NULL; c = c->next) {
        if (c->type == AX_RENDERER_CHAIN_SOFT) {
            p->soft = *(const ax_render_soft_desc *)(const void *)c;
            if (p->soft.jobs != NULL) {
                return; /* the caller's, as it is */
            }
            break;
        }
    }
    p->soft.chain.type = AX_RENDERER_CHAIN_SOFT;
    p->soft.chain.next = p->rdesc.next;
    p->soft.jobs = ax_rt_jobs(rt);
    p->rdesc.next = &p->soft.chain;
}

static ax_result pl_attach(void *self, ax_runtime rt, uint32_t i, uint64_t native, uint64_t window)
{
    plat *p = (plat *)self;
    plat_surface *ps = &p->s[i];
    ax_render_frame_desc fd;
    ax_result r;
    ax_atomic_store_u64(&p->rt, rt.h, AX_MO_RELEASE);
    if (p->vt == NULL || p->renderer_failed) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_runtime: no renderer to present windows");
    }
    if (p->renderer == NULL) {
        if (p->vt == ax_render_soft_vtbl()) {
            chain_soft(p, rt);
        }
        r = p->vt->create(&p->rdesc, &p->renderer);
        if (r != AX_OK && p->fallback_soft && p->vt != ax_render_soft_vtbl()) {
            /* The fallback chain's last resort: no usable GPU, draw on the CPU (decision 84). */
            p->vt = ax_render_soft_vtbl();
            p->rdesc.next = NULL;
            chain_soft(p, rt);
            r = p->vt->create(&p->rdesc, &p->renderer);
        }
        if (r != AX_OK) {
            p->renderer_failed = true;
            p->renderer = NULL;
            return r;
        }
        ax_atomic_store_ptr(&p->active_vt, (void *)(uintptr_t)p->vt, AX_MO_RELEASE);
    }
    ps->surface.h = native;
    ps->window.h = window;
    r = p->vt->attach_surface(p->renderer, ps->surface);
    if (r != AX_OK) {
        return r;
    }
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.vtbl = p->vt;
    fd.renderer = p->renderer;
    fd.surface = ps->surface;
    fd.window = ps->window;
    fd.mailbox = ax_rt_surface_mailbox(rt, i);
    fd.request = on_request;
    fd.request_ctx = ps;
    fd.max_wait_ns = p->max_wait_ns;
    ax_render_frame_init(&ps->rf, &fd);
    ps->reason = AX_WAKE_CONFIGURE;
    ps->map_next = 0;
    memset(ps->map_commit, 0, sizeof ps->map_commit);
    ps->reported_commit = 0;
    ps->reported_ir = 0;
    ps->used = true;
    return AX_OK;
}

static void pl_detach(void *self, uint32_t i)
{
    plat *p = (plat *)self;
    plat_surface *ps = &p->s[i];
    if (ps->used) {
        p->vt->detach_surface(p->renderer, ps->surface);
        ps->used = false;
    }
}

/* The newest commit on screen, mapped back to the snapshot it drew. */
static void report_presented(plat *p, plat_surface *ps)
{
    uint64_t id = 0, at = 0, best_commit = 0, best_ir = 0;
    uint32_t k;
    if (ax_surface_get_presented_commit(ps->surface, &id, &at) != AX_OK ||
        id <= ps->reported_commit) {
        return;
    }
    ps->reported_commit = id;
    for (k = 0; k < COMMIT_MAP; ++k) {
        if (ps->map_commit[k] != 0 && ps->map_commit[k] <= id && ps->map_commit[k] > best_commit) {
            best_commit = ps->map_commit[k];
            best_ir = ps->map_ir[k];
        }
    }
    if (best_ir > ps->reported_ir) {
        ps->reported_ir = best_ir;
        ax_atomic_fetch_add_u64(&ps->shown, 1, AX_MO_RELAXED);
        ax_rt_snapshot_presented(rt_of(p), ps->index, best_ir, at, false);
    }
}

static ax_result pl_wait(void *self, const uint32_t *surfaces, uint32_t count, uint64_t timeout,
                         bool *due)
{
    plat *p = (plat *)self;
    ax_surface handles[AX_RUNTIME_MAX_SURFACES];
    ax_wake_reason reasons[AX_RUNTIME_MAX_SURFACES];
    uint32_t i, n = 0;
    ax_result r;
    for (i = 0; i < count; ++i) {
        if (p->s[surfaces[i]].used) {
            handles[n++] = p->s[surfaces[i]].surface;
        }
    }
    if (n == 0 || p->vt->struct_size < sizeof(ax_renderer_vtbl) || p->vt->wait_frames == NULL) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_runtime: the renderer cannot wait on surfaces");
    }
    {
        bool animating = false;
        for (i = 0; i < count; ++i) {
            animating = animating || ax_rt_surface_animating(rt_of(p), surfaces[i]);
        }
        r = p->vt->wait_frames(p->renderer, handles, n, animating, timeout, reasons);
    }
    n = 0;
    for (i = 0; i < count; ++i) {
        plat_surface *ps = &p->s[surfaces[i]];
        due[i] = false;
        if (!ps->used) {
            continue;
        }
        ps->reason = r == AX_OK ? reasons[n] : AX_WAKE_TIMEOUT;
        due[i] = r == AX_OK && reasons[n] != AX_WAKE_TIMEOUT;
        n++;
        report_presented(p, ps); /* feedback dispatched while waiting */
    }
    return r == AX_ERR_DEVICE_LOST ? AX_OK : r;
}

static ax_result pl_frame(void *self, uint32_t i)
{
    plat *p = (plat *)self;
    plat_surface *ps = &p->s[i];
    ax_frame_result res;
    ax_present_stats st;
    ax_result r;
    if (!ps->used) {
        return AX_OK;
    }
    memset(&res, 0, sizeof res);
    res.struct_size = sizeof res;
    /* Every frame while something animates; otherwise only frames with new content (decision
     * 95): the surface rests without frames until a commit wakes it (pl_wake). */
    r = ax_render_frame_present(&ps->rf, ps->reason, ax_rt_surface_animating(rt_of(p), i), &res);
    if (r == AX_ERR_DEVICE_LOST) {
        return AX_OK; /* recovered by a later frame */
    }
    if (r != AX_OK) {
        return r;
    }
    if (res.status == AX_FRAME_PRESENTED && ps->rf.last_sequence != 0) {
        uint64_t commit = ax_surface_prepared_commit(ps->surface);
        ps->map_commit[ps->map_next] = commit;
        ps->map_ir[ps->map_next] = ps->rf.last_sequence;
        ps->map_next = (ps->map_next + 1u) % COMMIT_MAP;
        ax_atomic_fetch_add_u64(&ps->frames, 1, AX_MO_RELAXED);
        ax_atomic_store_u32(&ps->shown_w, float_bits(res.target.logical_width), AX_MO_RELAXED);
        ax_atomic_store_u32(&ps->shown_h, float_bits(res.target.logical_height), AX_MO_RELAXED);
    }
    ax_atomic_store_u64(&ps->scene_late, ps->rf.stats.scene_late, AX_MO_RELAXED);
    ax_atomic_store_u64(&ps->forced, ps->rf.stats.forced_acks, AX_MO_RELAXED);
    ax_atomic_store_u64(&ps->hidden, ps->rf.stats.hidden_acks, AX_MO_RELAXED);
    st.struct_size = sizeof st;
    if (ax_surface_get_present_stats(ps->surface, &st, false) == AX_OK) {
        ax_atomic_store_u64(&ps->presented, st.presented, AX_MO_RELAXED);
        ax_atomic_store_u64(&ps->missed, st.missed_vblanks, AX_MO_RELAXED);
        ax_atomic_store_u64(&ps->rests, st.rests, AX_MO_RELAXED);
        ax_atomic_store_u64(&ps->rest_ns, st.rest_ns, AX_MO_RELAXED);
        ax_atomic_store_u64(&ps->refresh, st.refresh_interval_ns, AX_MO_RELAXED);
    }
    report_presented(p, ps);
    return AX_OK;
}

static void pl_wake(void *self, uint32_t i, uint64_t native)
{
    ax_surface s;
    (void)self;
    (void)i;
    s.h = native;
    ax_surface_request_commit(s); /* its waits return AX_WAKE_COMMIT_REQUESTED */
}

static void pl_destroy(void *self)
{
    plat *p = (plat *)self;
    if (p->renderer != NULL) {
        p->vt->destroy(p->renderer);
        p->renderer = NULL;
    }
}

static const ax_rt_presenter_vtbl k_platform = {
    sizeof(ax_rt_presenter_vtbl), pl_attach, pl_detach, pl_wait, pl_frame, pl_destroy, pl_wake};

/* ---- Scene thread: the app's events ---------------------------------------------------------- */

static void on_events_queued(void *user)
{
    plat *p = (plat *)user;
    rt_impl *r = ax__rt_from_handle(rt_of(p));
    if (r != NULL) {
        ax__rt_wake_scene(r);
    }
}

static uint32_t surface_of_window(ax_runtime rt, uint64_t window)
{
    uint32_t i;
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        if (window != 0 && ax_rt_surface_window(rt, i) == window &&
            !AX_HANDLE_IS_NULL(ax_rt_surface_root(rt, i))) {
            return i;
        }
    }
    return UINT32_MAX;
}

/* The world's event types are the platform's, value for value (ax_world.h). */
_Static_assert((int)AX_WORLD_EVENT_NONE == (int)AX_EVENT_NONE, "event types differ");
_Static_assert((int)AX_WORLD_EVENT_CLOSE_REQUESTED == (int)AX_EVENT_CLOSE_REQUESTED,
               "event types differ");
_Static_assert((int)AX_WORLD_EVENT_RESIZED == (int)AX_EVENT_RESIZED, "event types differ");
_Static_assert((int)AX_WORLD_EVENT_SCALE_CHANGED == (int)AX_EVENT_SCALE_CHANGED,
               "event types differ");
_Static_assert((int)AX_WORLD_EVENT_FOCUS == (int)AX_EVENT_FOCUS, "event types differ");
_Static_assert((int)AX_WORLD_EVENT_OCCLUSION == (int)AX_EVENT_OCCLUSION, "event types differ");

/*
 * Copies a platform event's payload into a world event. The switch names every ax_event_type and
 * has no default: a type added to the platform without a case here is a -Wswitch error (part of
 * -Wall, which builds with -Werror), so no event (M2's input among them) is forwarded without its
 * data. Returns false for AX_EVENT_NONE and for a value outside the enum.
 */
static bool copy_event_data(const ax_event *e, ax_world_event *we)
{
    switch ((ax_event_type)e->type) {
    case AX_EVENT_NONE:
        return false;
    case AX_EVENT_CLOSE_REQUESTED:
        return true; /* no payload */
    case AX_EVENT_RESIZED:
        we->data.resized.width = e->data.resized.width;
        we->data.resized.height = e->data.resized.height;
        we->data.resized.pixel_width = e->data.resized.pixel_width;
        we->data.resized.pixel_height = e->data.resized.pixel_height;
        we->data.resized.scale = e->data.resized.scale;
        return true;
    case AX_EVENT_SCALE_CHANGED:
        we->data.scale_changed.scale = e->data.scale_changed.scale;
        return true;
    case AX_EVENT_FOCUS:
        we->data.focus.focused = e->data.focus.focused;
        return true;
    case AX_EVENT_OCCLUSION:
        we->data.occlusion.occluded = e->data.occlusion.occluded;
        return true;
    }
    return false; /* not a known type: never forwarded */
}

/*
 * Every event of the app's windows is forwarded, with the window's root when the runtime has that
 * window and root 0 otherwise (a window the app never added). Occlusion also sets the surface's
 * visibility for ax_txn_on_presented.
 */
static void pump_events(void *user, ax_runtime rt)
{
    plat *p = (plat *)user;
    rt_impl *r = ax__rt_from_handle(rt);
    ax_world world = r != NULL ? r->desc.world : (ax_world){0};
    ax_event e;
    ax_atomic_store_u64(&p->rt, rt.h, AX_MO_RELEASE);
    if (!p->notify_set) {
        (void)ax_app_set_event_notify(p->app, on_events_queued, p);
        p->notify_set = true;
    }
    for (;;) {
        ax_world_event we;
        uint32_t s;
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        if (ax_app_poll_event(p->app, &e) != AX_OK || e.type == AX_EVENT_NONE) {
            break;
        }
        memset(&we, 0, sizeof we);
        we.struct_size = sizeof we;
        if (!copy_event_data(&e, &we)) {
            continue;
        }
        s = surface_of_window(rt, e.window.h);
        we.type = e.type;
        we.window = e.window.h;
        we.root = s != UINT32_MAX && r != NULL ? r->surfaces[s].root.h : 0;
        we.time_ns = ax_world_time_from_monotonic(world, e.time_ns);
        if (e.type == AX_EVENT_OCCLUSION && s != UINT32_MAX) {
            ax_rt_set_surface_visible(rt, s, !e.data.occlusion.occluded);
        }
        (void)ax_world_push_event(world, &we);
    }
}

static void cleanup(void *user)
{
    plat *p = (plat *)user;
    ax_allocator a = p->allocator;
    (void)ax_app_set_event_notify(p->app, NULL, NULL); /* waits for a call in progress */
    ax_mem_free(&a, p, sizeof *p);
}

/* ---- App thread ------------------------------------------------------------------------------ */

ax_result ax_rt_create_for_app(const ax_runtime_desc *desc, const ax_rt_platform_desc *pd,
                               ax_runtime *out_runtime)
{
    const ax_allocator *a = ax_default_allocator();
    ax_rt_desc d;
    plat *p;
    uint32_t i;
    ax_result r;
    AX_ASSERT(out_runtime != NULL && pd != NULL);
    out_runtime->h = 0;
    if (desc == NULL || desc->struct_size < sizeof(ax_runtime_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH,
                            "ax_runtime_create_for_app: ax_runtime_desc.struct_size must be >= %u",
                            (unsigned)sizeof(ax_runtime_desc));
    }
    if (desc->mode != AX_RUNTIME_THREADED) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_runtime_create_for_app: windows need the threaded mode");
    }
    if (ax_app_caps(pd->app) == 0 && ax_app_backend(pd->app) == AX_BACKEND_NONE) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_runtime_create_for_app: invalid app");
    }
    p = (plat *)ax_mem_alloc(a, sizeof *p, AX_ALIGNOF(plat));
    if (p == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_runtime_create_for_app: out of memory");
    }
    memset(p, 0, sizeof *p);
    p->allocator = *a;
    p->app = pd->app;
    ax_atomic_init_u64(&p->rt, 0);
    p->vt = pd->renderer;
    p->fallback_soft = pd->renderer == NULL;
    ax_atomic_init_ptr(&p->active_vt, NULL);
#if defined(AX_RUNTIME_HAVE_WGPU)
    if (p->vt == NULL) {
        p->vt = ax_render_wgpu_vtbl();
    }
#endif
    if (p->vt == NULL) {
        p->vt = ax_render_soft_vtbl();
    }
    if (pd->renderer_desc != NULL) {
        p->rdesc = *pd->renderer_desc;
    } else {
        p->rdesc.struct_size = sizeof p->rdesc;
    }
    p->max_wait_ns = pd->max_wait_ns;
    for (i = 0; i < AX_RUNTIME_MAX_SURFACES; ++i) {
        plat_surface *ps = &p->s[i];
        ps->p = p;
        ps->index = i;
        ax_atomic_init_u64(&ps->frames, 0);
        ax_atomic_init_u64(&ps->shown, 0);
        ax_atomic_init_u64(&ps->presented, 0);
        ax_atomic_init_u64(&ps->missed, 0);
        ax_atomic_init_u64(&ps->rests, 0);
        ax_atomic_init_u64(&ps->rest_ns, 0);
        ax_atomic_init_u64(&ps->scene_late, 0);
        ax_atomic_init_u64(&ps->forced, 0);
        ax_atomic_init_u64(&ps->hidden, 0);
        ax_atomic_init_u64(&ps->refresh, 0);
        ax_atomic_init_u32(&ps->shown_w, 0);
        ax_atomic_init_u32(&ps->shown_h, 0);
    }
    /* Windows show the system's fonts (decision 98): a text system with fontconfig where it
     * exists (the bundled set after it), unless the app gave the world one. The world owns it. */
    if (ax_world_peek_text_system(desc->world) == NULL) {
        ax_font_source *fc = NULL;
        ax_text_system *ts = NULL;
        ax_text_system_desc td;
        memset(&td, 0, sizeof td);
        if (ax_font_source_fontconfig_create(NULL, &fc) == AX_OK) {
            td.sources = &fc;
            td.source_count = 1;
            td.own_sources = true;
        }
        if (ax_text_system_create(&td, &ts) == AX_OK) {
            ax_world_adopt_text_system(desc->world, ts);
        } else {
            ax_font_source_destroy(fc);
        }
    }
    memset(&d, 0, sizeof d);
    d.world = desc->world;
    d.mode = AX_RUNTIME_THREADED;
    d.presenter = &k_platform;
    d.presenter_self = p;
    d.pump_events = pump_events;
    d.pump_user = p;
    d.cleanup = cleanup;
    d.cleanup_user = p;
    r = ax_rt_create(&d, out_runtime);
    if (r != AX_OK) {
        ax_mem_free(a, p, sizeof *p);
    }
    return r;
}

ax_result ax_runtime_create_for_app(const ax_runtime_desc *desc, ax_app app,
                                    ax_runtime *out_runtime)
{
    ax_rt_platform_desc pd;
    memset(&pd, 0, sizeof pd);
    pd.app = app;
    return ax_rt_create_for_app(desc, &pd, out_runtime);
}

static plat *plat_of(rt_impl *r)
{
    return r != NULL && r->desc.presenter == &k_platform ? (plat *)r->desc.presenter_self : NULL;
}

ax_result ax_runtime_add_window(ax_runtime runtime, ax_window window, ax_node *out_root)
{
    rt_impl *r = ax__rt_from_handle(runtime);
    plat *p = plat_of(r);
    ax_surface surface;
    ax_rt_surface_desc sd;
    ax_txn *t;
    ax_node root;
    uint32_t index;
    ax_result res;
    AX_ASSERT(out_root != NULL);
    out_root->h = 0;
    if (p == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE,
                            "ax_runtime_add_window: not a runtime made for an app");
    }
    if (ax_window_get_surface(window, &surface) != AX_OK) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_runtime_add_window: invalid window");
    }
    if (surface_of_window(runtime, window.h) != UINT32_MAX) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_runtime_add_window: already added");
    }
    t = ax_txn_begin(r->desc.world);
    if (t == NULL) {
        return ax_last_error()->code;
    }
    root = ax_txn_create(t, AX_NODE_VIEW);
    res = ax_txn_commit(t, NULL);
    if (res != AX_OK) {
        return res;
    }
    memset(&sd, 0, sizeof sd);
    sd.native_surface = surface.h;
    sd.window = window.h;
    res = ax_rt_add_surface(runtime, root, &sd, &index);
    if (res != AX_OK) {
        return res;
    }
    p->s[index].window_handle = window.h;
    *out_root = root;
    return AX_OK;
}

ax_result ax_runtime_remove_window(ax_runtime runtime, ax_window window)
{
    uint32_t s = surface_of_window(runtime, window.h);
    if (ax__rt_from_handle(runtime) == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_runtime_remove_window: invalid runtime");
    }
    if (s == UINT32_MAX) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_runtime_remove_window: not added");
    }
    return ax_rt_remove_surface(runtime, s);
}

ax_result ax_rt_get_window_counters(ax_runtime rt, ax_window window, ax_rt_window_counters *out)
{
    rt_impl *r = ax__rt_from_handle(rt);
    plat *p = plat_of(r);
    uint32_t s;
    plat_surface *ps;
    AX_ASSERT(out != NULL);
    memset(out, 0, sizeof *out);
    if (p == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_rt_get_window_counters: invalid runtime");
    }
    s = surface_of_window(rt, window.h);
    if (s == UINT32_MAX) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_rt_get_window_counters: no such window");
    }
    ps = &p->s[s];
    out->frames = ax_atomic_load_u64(&ps->frames, AX_MO_RELAXED);
    out->snapshots_shown = ax_atomic_load_u64(&ps->shown, AX_MO_RELAXED);
    out->presented = ax_atomic_load_u64(&ps->presented, AX_MO_RELAXED);
    out->missed_vblanks = ax_atomic_load_u64(&ps->missed, AX_MO_RELAXED);
    out->rests = ax_atomic_load_u64(&ps->rests, AX_MO_RELAXED);
    out->rest_ns = ax_atomic_load_u64(&ps->rest_ns, AX_MO_RELAXED);
    out->scene_late = ax_atomic_load_u64(&ps->scene_late, AX_MO_RELAXED);
    out->forced_acks = ax_atomic_load_u64(&ps->forced, AX_MO_RELAXED);
    out->hidden_acks = ax_atomic_load_u64(&ps->hidden, AX_MO_RELAXED);
    out->refresh_ns = ax_atomic_load_u64(&ps->refresh, AX_MO_RELAXED);
    out->shown_width = bits_float(ax_atomic_load_u32(&ps->shown_w, AX_MO_RELAXED));
    out->shown_height = bits_float(ax_atomic_load_u32(&ps->shown_h, AX_MO_RELAXED));
    return AX_OK;
}

const ax_renderer_vtbl *ax_rt_renderer_vtbl(ax_runtime rt)
{
    plat *p = plat_of(ax__rt_from_handle(rt));
    return p != NULL ? (const ax_renderer_vtbl *)ax_atomic_load_ptr(&p->active_vt, AX_MO_ACQUIRE)
                     : NULL;
}

void *ax_rt_renderer(ax_runtime rt)
{
    plat *p = plat_of(ax__rt_from_handle(rt));
    return p != NULL ? p->renderer : NULL;
}
