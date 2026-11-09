/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The headless host (decision 85): a world, a runtime and a presenter of virtual surfaces.
 *
 * The presenter (ax_rt_presenter_vtbl) runs on the runtime's render side: the caller's thread in
 * the default single-threaded mode, the render thread with AX_HEADLESS_THREADS. It draws each new
 * snapshot into the surface's pixels with ax_render_soft (or ax_render_wgpu offscreen), then
 * swaps them, under a lock, with the pixels the app thread reads (capture, PNG, IR dump).
 *
 * Surface settings (size, scale, theme) live in a config record the app thread owns; the record
 * travels to the presenter as the surface's "native" handle, through the runtime's command ring,
 * so the presenter finds it at attach without racing the app thread.
 */
#include "axion/headless/ax_headless.h"

#include <string.h>

#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_thread.h"
#include "axion/headless/ax_png.h"
#include "axion/render/ax_render_soft.h"
#include "hl_internal.h"

#define HL_MAX AX_RUNTIME_MAX_SURFACES

/* App side: a surface's settings. Read by the presenter under the host's lock. */
typedef struct hl_config {
    bool used;
    ax_node root;
    uint32_t index; /* the runtime's surface index */
    ax_headless_surface_desc desc;
    hl_tree *tree; /* its semantic tree (decision 108), or NULL with AX_HEADLESS_NO_SEMANTICS */
} hl_config;

/* Render side: what the presenter keeps per surface (indexed by the runtime's surface index). */
typedef struct hl_surface {
    hl_config *cfg;
    bool live;
    uint64_t next_due; /* threaded mode: the next virtual vsync (monotonic clock) */
    uint64_t drawn_seq;
    uint32_t drawn_w, drawn_h;
    uint8_t *draw_px;
    size_t draw_cap;
    uint8_t *ir_draft; /* the snapshot drawn, serialized, until swapped */
    size_t ir_draft_size, ir_draft_cap;
    /* shown: under the lock */
    uint8_t *shown_px;
    size_t shown_cap;
    uint32_t shown_w, shown_h;
    uint64_t shown_seq;
    uint8_t *ir_blob;
    size_t ir_size, ir_cap;
    ax_atomic_u64 frames;
} hl_surface;

struct ax_headless {
    ax_allocator allocator;
    ax_headless_desc desc;
    bool threaded;
    uint64_t now;        /* the manual clock (single-threaded mode) */
    uint64_t next_vsync; /* the virtual display's next frame on it: frames keep their phase */
    ax_world world;
    ax_runtime rt;
    ax_mutex lock;
    hl_config cfg[HL_MAX];
    hl_surface s[HL_MAX];
    /* render side */
    ax_render_soft *soft;
    hl_gpu *gpu;
};

static uint64_t manual_clock(void *user)
{
    return ((ax_headless *)user)->now;
}

static uint32_t pixels(float logical, float scale)
{
    const float v = logical * scale + 0.5f;
    return v < 1.0f ? 1u : (uint32_t)v;
}

/* ---- the presenter (render side) ------------------------------------------------------------- */

static bool grow(const ax_allocator *a, uint8_t **p, size_t *cap, size_t need)
{
    uint8_t *n;
    if (*cap >= need) {
        return true;
    }
    n = (uint8_t *)ax_mem_alloc(a, need, 16);
    if (n == NULL) {
        return false;
    }
    if (*p != NULL) {
        ax_mem_free(a, *p, *cap);
    }
    *p = n;
    *cap = need;
    return true;
}

static void release(const ax_allocator *a, uint8_t **p, size_t *cap)
{
    if (*p != NULL) {
        ax_mem_free(a, *p, *cap);
    }
    *p = NULL;
    *cap = 0;
}

static void request_next(ax_headless *h, uint32_t i)
{
    hl_surface *s = &h->s[i];
    ax_rt_request q;
    memset(&q, 0, sizeof q);
    ax_mutex_lock(&h->lock);
    q.logical_width = s->cfg->desc.width;
    q.logical_height = s->cfg->desc.height;
    q.scale = s->cfg->desc.scale;
    ax_mutex_unlock(&h->lock);
    q.time_ns = s->next_due;
    q.refresh_ns = h->desc.refresh_ns;
    ax_rt_request_frame(h->rt, i, &q);
}

static ax_result hl_attach(void *self, ax_runtime rt, uint32_t i, uint64_t native, uint64_t window)
{
    ax_headless *h = (ax_headless *)self;
    hl_surface *s = &h->s[i];
    (void)window;
    (void)rt;
    if (h->desc.renderer == AX_HEADLESS_RENDER_WGPU) {
        if (h->gpu == NULL) {
            ax_result r = ax__hl_gpu_create(&h->allocator, &h->gpu);
            if (r != AX_OK) {
                return r;
            }
        }
    } else if (h->soft == NULL) {
        ax_render_soft_desc sd;
        ax_result r;
        memset(&sd, 0, sizeof sd);
        sd.jobs = ax_rt_jobs(h->rt);
        if (h->desc.struct_size >= offsetof(ax_headless_desc, blend) + sizeof(uint32_t)) {
            sd.blend = h->desc.blend;
        }
        r = ax_render_soft_create(&h->allocator, &sd, &h->soft);
        if (r != AX_OK) {
            return r;
        }
    }
    s->cfg = (hl_config *)(uintptr_t)native;
    s->live = true;
    s->drawn_seq = 0;
    s->next_due = ax_time_monotonic_ns() + h->desc.refresh_ns;
    if (h->threaded) {
        request_next(h, i);
    }
    return AX_OK;
}

static void hl_detach(void *self, uint32_t i)
{
    ax_headless *h = (ax_headless *)self;
    hl_surface *s = &h->s[i];
    s->live = false;
    release(&h->allocator, &s->draw_px, &s->draw_cap);
    release(&h->allocator, &s->ir_draft, &s->ir_draft_cap);
    ax_mutex_lock(&h->lock);
    release(&h->allocator, &s->shown_px, &s->shown_cap);
    release(&h->allocator, &s->ir_blob, &s->ir_cap);
    s->shown_w = s->shown_h = 0;
    s->shown_seq = 0;
    s->ir_size = 0;
    ax_mutex_unlock(&h->lock);
}

/* Threaded mode: waits for the virtual vsync of the earliest surface, yielding (timed waits wake
 * late on some systems, which would make the display itself miss refreshes). */
static ax_result hl_wait(void *self, const uint32_t *surfaces, uint32_t count, uint64_t timeout,
                         bool *due)
{
    ax_headless *h = (ax_headless *)self;
    uint64_t until = ax_time_monotonic_ns() + timeout, now;
    uint32_t i;
    for (i = 0; i < count; ++i) {
        if (h->s[surfaces[i]].next_due < until) {
            until = h->s[surfaces[i]].next_due;
        }
    }
    while (ax_time_monotonic_ns() < until) {
        ax_thread_yield();
    }
    now = ax_time_monotonic_ns();
    for (i = 0; i < count; ++i) {
        due[i] = now >= h->s[surfaces[i]].next_due;
    }
    return AX_OK;
}

static ax_result draw(ax_headless *h, hl_surface *s, const ax_render_ir *ir, uint32_t w,
                      uint32_t ht, float scale, ax_color background)
{
    if (h->desc.renderer == AX_HEADLESS_RENDER_WGPU) {
        return ax__hl_gpu_draw(h->gpu, ir, w, ht, scale, background, s->draw_px);
    }
    {
        ax_soft_target t;
        memset(&t, 0, sizeof t);
        t.struct_size = sizeof t;
        t.pixels = s->draw_px;
        t.width = w;
        t.height = ht;
        t.stride = (size_t)w * 4u;
        t.format = AX_SOFT_RGBA8;
        t.scale = scale;
        t.background = background;
        return ax_render_soft_draw_ir(h->soft, &t, ir, NULL);
    }
}

static ax_result hl_frame(void *self, uint32_t i)
{
    ax_headless *h = (ax_headless *)self;
    hl_surface *s = &h->s[i];
    const ax_render_ir *ir = ax_ir_mailbox_acquire(ax_rt_surface_mailbox(h->rt, i));
    float width, height, scale;
    ax_color background;
    uint32_t w, ht;
    ax_result r = AX_OK;

    ax_mutex_lock(&h->lock);
    width = s->cfg->desc.width;
    height = s->cfg->desc.height;
    scale = s->cfg->desc.scale;
    background = s->cfg->desc.background;
    ax_mutex_unlock(&h->lock);
    w = pixels(width, scale);
    ht = pixels(height, scale);

    if ((ir != NULL && ir->sequence != s->drawn_seq) || w != s->drawn_w || ht != s->drawn_h) {
        size_t ir_size = ir != NULL ? ax_ir_serialized_size(ir) : 0;
        if (!grow(&h->allocator, &s->draw_px, &s->draw_cap, (size_t)w * ht * 4u) ||
            (ir_size != 0 && !grow(&h->allocator, &s->ir_draft, &s->ir_draft_cap, ir_size))) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless: out of memory");
        }
        r = draw(h, s, ir, w, ht, scale, background);
        if (r != AX_OK) {
            return r;
        }
        s->ir_draft_size = 0;
        if (ir_size != 0 &&
            ax_ir_serialize(ir, s->ir_draft, s->ir_draft_cap, &s->ir_draft_size) != AX_OK) {
            s->ir_draft_size = 0;
        }
        s->drawn_seq = ir != NULL ? ir->sequence : 0;
        s->drawn_w = w;
        s->drawn_h = ht;
        /* Swap with what the app thread reads. */
        ax_mutex_lock(&h->lock);
        {
            uint8_t *px = s->shown_px, *blob = s->ir_blob;
            const size_t px_cap = s->shown_cap, blob_cap = s->ir_cap;
            s->shown_px = s->draw_px;
            s->shown_cap = s->draw_cap;
            s->draw_px = px;
            s->draw_cap = px_cap;
            s->ir_blob = s->ir_draft;
            s->ir_cap = s->ir_draft_cap;
            s->ir_size = s->ir_draft_size;
            s->ir_draft = blob;
            s->ir_draft_cap = blob_cap;
            s->shown_w = w;
            s->shown_h = ht;
            s->shown_seq = s->drawn_seq;
        }
        ax_mutex_unlock(&h->lock);
        if (ir != NULL) {
            ax_rt_snapshot_presented(h->rt, i, ir->sequence, ir->frame_time_ns, true);
        }
    }
    ax_atomic_fetch_add_u64(&s->frames, 1, AX_MO_RELEASE);
    if (h->threaded) {
        const uint64_t now = ax_time_monotonic_ns();
        s->next_due += h->desc.refresh_ns;
        if (s->next_due < now) {
            s->next_due = now; /* far behind: start again from now */
        }
        request_next(h, i);
    }
    return AX_OK;
}

static void hl_destroy_render(void *self)
{
    ax_headless *h = (ax_headless *)self;
    ax_render_soft_destroy(h->soft);
    h->soft = NULL;
    ax__hl_gpu_destroy(h->gpu);
    h->gpu = NULL;
}

static const ax_rt_presenter_vtbl k_presenter = {
    sizeof(ax_rt_presenter_vtbl),
    hl_attach,
    hl_detach,
    hl_wait,
    hl_frame,
    hl_destroy_render,
    NULL /* a virtual display: it presents every refresh (decision 95) */};

/* ---- creation -------------------------------------------------------------------------------- */

ax_headless_desc ax_headless_default_desc(void)
{
    ax_headless_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.renderer = AX_HEADLESS_RENDER_SOFT;
    d.start_time_ns = 1000000000ull;
    d.refresh_ns = 16666667ull;
    return d;
}

ax_headless_surface_desc ax_headless_default_surface_desc(void)
{
    ax_headless_surface_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.width = 390.0f;
    d.height = 844.0f;
    d.scale = 3.0f;
    d.theme = AX_HEADLESS_THEME_LIGHT;
    d.background = ax_color_make(0, 0, 0, 1);
    return d;
}

ax_result ax_headless_create(const ax_headless_desc *desc, ax_headless **out)
{
    ax_headless_desc d = desc != NULL ? *desc : ax_headless_default_desc();
    const ax_allocator *a;
    ax_headless *h;
    ax_world_desc wd;
    ax_rt_desc rd;
    ax_result r;

    *out = NULL;
    if (desc != NULL && desc->struct_size < sizeof(ax_headless_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_headless_create: struct_size must be >= %u",
                            (unsigned)sizeof(ax_headless_desc));
    }
    if (d.refresh_ns == 0 || d.refresh_ns > 1000000000ull ||
        (d.renderer != AX_HEADLESS_RENDER_SOFT && d.renderer != AX_HEADLESS_RENDER_WGPU)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_headless_create: renderer or refresh");
    }
    if ((d.flags & AX_HEADLESS_THREADS) != 0 && !ax_threads_supported()) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_headless_create: no threads in this build");
    }
    if (d.renderer == AX_HEADLESS_RENDER_WGPU) {
        r = ax__hl_gpu_probe();
        if (r != AX_OK) {
            return r;
        }
    }
    a = d.allocator != NULL ? d.allocator : ax_default_allocator();
    h = (ax_headless *)ax_mem_alloc(a, sizeof *h, AX_ALIGNOF(ax_headless));
    if (h == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless_create: out of memory");
    }
    memset(h, 0, sizeof *h);
    h->allocator = *a;
    h->desc = d;
    h->desc.allocator = NULL;
    h->threaded = (d.flags & AX_HEADLESS_THREADS) != 0;
    h->now = d.start_time_ns;
    h->next_vsync = d.start_time_ns + d.refresh_ns;
    ax_mutex_init(&h->lock);
    {
        uint32_t i;
        for (i = 0; i < HL_MAX; ++i) {
            ax_atomic_init_u64(&h->s[i].frames, 0);
        }
    }

    memset(&wd, 0, sizeof wd);
    wd.struct_size = sizeof wd;
    wd.allocator = &h->allocator;
    if (!h->threaded) {
        wd.clock = manual_clock;
        wd.clock_user = h;
    }
    r = ax_world_create(&wd, &h->world);
    if (r != AX_OK) {
        ax_mutex_deinit(&h->lock);
        ax_mem_free(a, h, sizeof *h);
        return r;
    }
    memset(&rd, 0, sizeof rd);
    rd.world = h->world;
    rd.mode = h->threaded ? AX_RUNTIME_THREADED : AX_RUNTIME_SINGLE_THREADED;
    rd.presenter = &k_presenter;
    rd.presenter_self = h;
    r = ax_rt_create(&rd, &h->rt);
    if (r != AX_OK) {
        (void)ax_world_destroy(h->world);
        ax_mutex_deinit(&h->lock);
        ax_mem_free(a, h, sizeof *h);
        return r;
    }
    *out = h;
    return AX_OK;
}

void ax_headless_destroy(ax_headless *h)
{
    ax_allocator a;
    uint32_t i;
    if (h == NULL) {
        return;
    }
    for (i = 0; i < HL_MAX; ++i) {
        if (h->cfg[i].used) {
            (void)ax_rt_remove_surface(h->rt, h->cfg[i].index);
        }
    }
    (void)ax_runtime_destroy(h->rt);
    /* Single-threaded mode: the render side's resources are freed here (the runtime's destroy
     * runs the presenter's destroy in threaded mode only on its thread). */
    hl_destroy_render(h);
    for (i = 0; i < HL_MAX; ++i) {
        hl_surface *s = &h->s[i];
        release(&h->allocator, &s->draw_px, &s->draw_cap);
        release(&h->allocator, &s->ir_draft, &s->ir_draft_cap);
        release(&h->allocator, &s->shown_px, &s->shown_cap);
        release(&h->allocator, &s->ir_blob, &s->ir_cap);
    }
    (void)ax_world_destroy(h->world);
    for (i = 0; i < HL_MAX; ++i) {
        ax__hl_tree_destroy(h->cfg[i].tree);
    }
    ax_mutex_deinit(&h->lock);
    a = h->allocator;
    ax_mem_free(&a, h, sizeof *h);
}

ax_world ax_headless_world(ax_headless *h)
{
    return h->world;
}

/* ---- surfaces -------------------------------------------------------------------------------- */

static hl_config *config_of(ax_headless *h, ax_node root)
{
    uint32_t i;
    for (i = 0; i < HL_MAX; ++i) {
        if (h->cfg[i].used && h->cfg[i].root.h == root.h) {
            return &h->cfg[i];
        }
    }
    return NULL;
}

hl_tree *ax__hl_tree_of(ax_headless *h, ax_node root)
{
    hl_config *c = h != NULL ? config_of(h, root) : NULL;
    return c != NULL ? c->tree : NULL;
}

static bool valid_size(float w, float ht, float scale)
{
    return w >= 1.0f && ht >= 1.0f && scale > 0.0f && w <= 16384.0f && ht <= 16384.0f &&
           scale <= 16.0f && w * scale <= 32768.0f && ht * scale <= 32768.0f;
}

ax_result ax_headless_add_surface(ax_headless *h, const ax_headless_surface_desc *desc,
                                  ax_node *out_root)
{
    hl_config *c = NULL;
    ax_rt_surface_desc sd;
    ax_txn *t;
    ax_node root;
    uint32_t i;
    ax_result r;
    if (desc == NULL || desc->struct_size < sizeof(ax_headless_surface_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_headless_add_surface: struct_size");
    }
    if (!valid_size(desc->width, desc->height, desc->scale)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_headless_add_surface: size or scale");
    }
    for (i = 0; i < HL_MAX && c == NULL; ++i) {
        if (!h->cfg[i].used) {
            c = &h->cfg[i];
        }
    }
    if (c == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless_add_surface: at most %u surfaces",
                            (unsigned)HL_MAX);
    }
    t = ax_txn_begin(h->world);
    if (t == NULL) {
        return ax_last_error()->code;
    }
    root = ax_txn_create(t, AX_NODE_VIEW);
    r = ax_txn_commit(t, NULL);
    if (r != AX_OK) {
        return r;
    }
    if ((h->desc.flags & AX_HEADLESS_NO_SEMANTICS) == 0 && c->tree == NULL) {
        c->tree = ax__hl_tree_create(&h->allocator, &h->lock);
        if (c->tree == NULL) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless_add_surface: out of memory");
        }
    }
    ax_mutex_lock(&h->lock);
    c->desc = *desc;
    c->root = root;
    ax_mutex_unlock(&h->lock);
    memset(&sd, 0, sizeof sd);
    sd.width = desc->width;
    sd.height = desc->height;
    sd.scale = desc->scale;
    sd.refresh_ns = h->desc.refresh_ns;
    sd.native_surface = (uint64_t)(uintptr_t)c;
    if (c->tree != NULL) {
        sd.a11y.update = ax__hl_tree_update;
        sd.a11y.user = c->tree;
    }
    r = ax_rt_add_surface(h->rt, root, &sd, &c->index);
    if (r != AX_OK) {
        return r;
    }
    c->used = true;
    if (c->tree != NULL) {
        /* An assistive technology reads every headless surface: the testing API's queries. */
        ax_world_a11y_activate(h->world, root, true);
    }
    *out_root = root;
    return AX_OK;
}

ax_result ax_headless_remove_surface(ax_headless *h, ax_node root)
{
    hl_config *c = config_of(h, root);
    ax_result r;
    if (c == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    r = ax_rt_remove_surface(h->rt, c->index);
    if (r == AX_OK) {
        c->used = false;
        ax__hl_tree_destroy(c->tree); /* the scene let its sink go with the surface */
        c->tree = NULL;
    }
    return r;
}

ax_result ax_headless_resize(ax_headless *h, ax_node root, float width, float height, float scale)
{
    hl_config *c = config_of(h, root);
    if (c == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    if (!valid_size(width, height, scale)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_headless_resize: size or scale");
    }
    ax_mutex_lock(&h->lock);
    c->desc.width = width;
    c->desc.height = height;
    c->desc.scale = scale;
    ax_mutex_unlock(&h->lock);
    if (!h->threaded) {
        ax_rt_set_surface_size(h->rt, c->index, width, height, scale);
    }
    return AX_OK;
}

ax_result ax_headless_get_surface_info(ax_headless *h, ax_node root, ax_headless_surface_info *out)
{
    hl_config *c = config_of(h, root);
    hl_surface *s;
    uint32_t size;
    if (c == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    s = &h->s[c->index];
    size = out->struct_size;
    memset(out, 0, sizeof *out);
    out->struct_size = size;
    ax_mutex_lock(&h->lock);
    out->width = c->desc.width;
    out->height = c->desc.height;
    out->scale = c->desc.scale;
    out->theme = c->desc.theme;
    out->reduce_motion = c->desc.reduce_motion;
    out->shown_ir = s->shown_seq;
    ax_mutex_unlock(&h->lock);
    out->pixel_width = pixels(out->width, out->scale);
    out->pixel_height = pixels(out->height, out->scale);
    out->frames = ax_atomic_load_u64(&s->frames, AX_MO_ACQUIRE);
    return AX_OK;
}

/* ---- time ------------------------------------------------------------------------------------ */

uint64_t ax_headless_now(ax_headless *h)
{
    return ax_world_now(h->world);
}

/* One frame on every surface, presented at the next vsync; the clock moves to it. Transactions
 * committed up to one refresh before it apply (decision 77's latch point). */
static ax_result frame_all(ax_headless *h)
{
    const uint64_t t = h->next_vsync;
    uint32_t i;
    for (i = 0; i < HL_MAX; ++i) {
        if (h->cfg[i].used) {
            ax_result r = ax_host_frame(h->rt, h->cfg[i].root, t);
            if (r != AX_OK) {
                return r;
            }
        }
    }
    h->now = t;
    h->next_vsync = t + h->desc.refresh_ns;
    return AX_OK;
}

static uint64_t min_frames(ax_headless *h)
{
    uint64_t m = UINT64_MAX;
    uint32_t i;
    for (i = 0; i < HL_MAX; ++i) {
        if (h->cfg[i].used) {
            const uint64_t f = ax_atomic_load_u64(&h->s[h->cfg[i].index].frames, AX_MO_ACQUIRE);
            m = f < m ? f : m;
        }
    }
    return m;
}

static void sleep_ns(uint64_t ns)
{
    ax_mutex m;
    ax_cond c;
    const uint64_t until = ax_time_monotonic_ns() + ns;
    ax_mutex_init(&m);
    ax_cond_init(&c);
    ax_mutex_lock(&m);
    while (ax_time_monotonic_ns() < until) {
        (void)ax_cond_wait_timeout(&c, &m, until - ax_time_monotonic_ns());
    }
    ax_mutex_unlock(&m);
    ax_cond_deinit(&c);
    ax_mutex_deinit(&m);
}

ax_result ax_headless_step(ax_headless *h, uint32_t count)
{
    uint32_t n;
    if (h->threaded) {
        const uint64_t start = min_frames(h);
        const uint64_t limit =
            ax_time_monotonic_ns() + 10000000000ull + (uint64_t)count * h->desc.refresh_ns * 4u;
        if (start == UINT64_MAX) {
            return AX_OK; /* no surface */
        }
        while (min_frames(h) < start + count) {
            if (ax_time_monotonic_ns() > limit) {
                return ax_set_error(AX_ERR_TIMEOUT, "ax_headless_step: frames did not come");
            }
            sleep_ns(h->desc.refresh_ns / 4u + 1u);
        }
        return AX_OK;
    }
    for (n = 0; n < count; ++n) {
        ax_result r = frame_all(h);
        if (r != AX_OK) {
            return r;
        }
    }
    return AX_OK;
}

ax_result ax_headless_advance_time(ax_headless *h, uint64_t ns)
{
    uint64_t target;
    if (h->threaded) {
        sleep_ns(ns);
        return AX_OK;
    }
    target = h->now + ns;
    while (h->next_vsync <= target) {
        ax_result r = frame_all(h);
        if (r != AX_OK) {
            return r;
        }
    }
    h->now = target;
    return AX_OK;
}

static bool settled(ax_headless *h)
{
    uint32_t i;
    if (ax_world_last_applied_seq(h->world) < ax_world_last_committed_seq(h->world)) {
        return false;
    }
    /* The semantic trees: activations and actions handled, applied changes sent (decision 108). */
    if (!ax_world_a11y_caught_up(h->world)) {
        return false;
    }
    /* Every surface drew the newest snapshot recorded for it. */
    for (i = 0; i < HL_MAX; ++i) {
        if (h->cfg[i].used) {
            const uint32_t idx = h->cfg[i].index;
            uint64_t shown;
            ax_mutex_lock(&h->lock);
            shown = h->s[idx].shown_seq;
            ax_mutex_unlock(&h->lock);
            if (shown < ax_ir_mailbox_published(ax_rt_surface_mailbox(h->rt, idx)) ||
                ax_rt_surface_animating(h->rt, idx)) {
                return false; /* not drawn yet, or still animating (decision 94) */
            }
        }
    }
    return true;
}

ax_result ax_headless_settle(ax_headless *h, uint32_t max_frames, uint32_t *out_frames)
{
    uint32_t frames = 0;
    if (h->threaded) {
        const uint64_t until = ax_time_monotonic_ns() + (uint64_t)max_frames * h->desc.refresh_ns;
        uint64_t start = min_frames(h);
        for (;;) {
            /* A frame after the condition holds, so what was applied is also drawn. */
            if (settled(h) && ax_headless_step(h, 1) == AX_OK && settled(h)) {
                break;
            }
            if (ax_time_monotonic_ns() > until) {
                return ax_set_error(AX_ERR_TIMEOUT,
                                    "ax_headless_settle: not settled in %u "
                                    "frames' time",
                                    max_frames);
            }
            sleep_ns(h->desc.refresh_ns / 4u + 1u);
        }
        if (out_frames != NULL) {
            const uint64_t now_frames = min_frames(h);
            *out_frames = start == UINT64_MAX ? 0u : (uint32_t)(now_frames - start);
        }
        return AX_OK;
    }
    while (!settled(h)) {
        ax_result r;
        if (frames == max_frames) {
            if (out_frames != NULL) {
                *out_frames = frames;
            }
            return ax_set_error(AX_ERR_TIMEOUT, "ax_headless_settle: not settled in %u frames",
                                max_frames);
        }
        r = frame_all(h);
        if (r != AX_OK) {
            return r;
        }
        frames++;
    }
    if (out_frames != NULL) {
        *out_frames = frames;
    }
    return AX_OK;
}

/* ---- snapshots ------------------------------------------------------------------------------- */

ax_result ax_headless_capture(ax_headless *h, ax_node root, ax_headless_image *out)
{
    hl_config *c = config_of(h, root);
    hl_surface *s;
    size_t size;
    memset(out, 0, sizeof *out);
    if (c == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    s = &h->s[c->index];
    ax_mutex_lock(&h->lock);
    if (s->shown_px == NULL || s->shown_w == 0) {
        ax_mutex_unlock(&h->lock);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_headless_capture: nothing drawn yet");
    }
    size = (size_t)s->shown_w * s->shown_h * 4u;
    out->rgba = (uint8_t *)ax_mem_alloc(&h->allocator, size, 16);
    if (out->rgba != NULL) {
        memcpy(out->rgba, s->shown_px, size);
        out->width = s->shown_w;
        out->height = s->shown_h;
        out->stride = (size_t)s->shown_w * 4u;
    }
    ax_mutex_unlock(&h->lock);
    if (out->rgba == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless_capture: out of memory");
    }
    return AX_OK;
}

void ax_headless_image_free(ax_headless *h, ax_headless_image *image)
{
    if (image->rgba != NULL) {
        ax_mem_free(&h->allocator, image->rgba, image->stride * image->height);
    }
    memset(image, 0, sizeof *image);
}

ax_result ax_headless_write_png(ax_headless *h, ax_node root, const char *path)
{
    ax_headless_image img;
    ax_result r = ax_headless_capture(h, root, &img);
    if (r != AX_OK) {
        return r;
    }
    r = ax_png_write_file(path, img.rgba, img.width, img.height, img.stride, AX_PNG_PREMULTIPLIED);
    ax_headless_image_free(h, &img);
    return r;
}

ax_result ax_headless_save_ir(ax_headless *h, ax_node root, uint8_t **out_data, size_t *out_size)
{
    hl_config *c = config_of(h, root);
    hl_surface *s;
    *out_data = NULL;
    *out_size = 0;
    if (c == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    s = &h->s[c->index];
    ax_mutex_lock(&h->lock);
    if (s->ir_size == 0) {
        ax_mutex_unlock(&h->lock);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_headless: no snapshot drawn yet");
    }
    *out_data = (uint8_t *)ax_mem_alloc(&h->allocator, s->ir_size, 16);
    if (*out_data != NULL) {
        memcpy(*out_data, s->ir_blob, s->ir_size);
        *out_size = s->ir_size;
    }
    ax_mutex_unlock(&h->lock);
    return *out_data != NULL ? AX_OK
                             : ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless: out of memory");
}

void ax_headless_free(ax_headless *h, void *data, size_t size)
{
    if (data != NULL) {
        ax_mem_free(&h->allocator, data, size);
    }
}

ax_result ax_headless_dump_ir(ax_headless *h, ax_node root, ax_ir_write_fn write, void *ctx)
{
    uint8_t *data;
    size_t size;
    ax_arena arena;
    const ax_render_ir *ir;
    ax_result r = ax_headless_save_ir(h, root, &data, &size);
    if (r != AX_OK) {
        return r;
    }
    ax_arena_init(&arena, &h->allocator, 0);
    r = ax_ir_deserialize(data, size, &arena, &ir);
    if (r == AX_OK) {
        ax_ir_dump(ir, write, ctx);
    }
    ax_arena_deinit(&arena);
    ax_headless_free(h, data, size);
    return r;
}
