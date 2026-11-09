/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft as an ax_renderer_vtbl: window surfaces through the platform's CPU buffers
 * (decision 84). Built with the platform module only.
 */
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_present_cpu.h"
#include "axion/render/ax_render_soft_surface.h"
#include "soft_internal.h"

#define SOFT_MAX_SURFACES 8

typedef struct soft_surface {
    ax_surface surface;
    uint64_t attach_ns;
    ax_render_soft_surface_stats stats;
} soft_surface;

/* Window state hangs off the renderer through this side table: ax_render_soft itself needs no
 * platform. One table per renderer, found by its address. */
typedef struct soft_windows {
    ax_render_soft *r;
    ax_color background;
    soft_surface s[SOFT_MAX_SURFACES];
    uint32_t count;
} soft_windows;

static soft_windows *windows_of(ax_render_soft *r)
{
    return (soft_windows *)ax__render_soft_user(r);
}

static soft_surface *find(soft_windows *ws, ax_surface surface)
{
    uint32_t i;
    for (i = 0; i < ws->count; ++i) {
        if (ws->s[i].surface.h == surface.h) {
            return &ws->s[i];
        }
    }
    return NULL;
}

static ax_result vt_create(const ax_renderer_desc *desc, void **out_self)
{
    const ax_renderer_chain *c;
    const ax_render_soft_desc *sd = NULL;
    ax_render_soft *r = NULL;
    soft_windows *ws;
    ax_result res;
    AX_ASSERT(desc != NULL && desc->struct_size >= sizeof(ax_renderer_desc));
    *out_self = NULL;
    for (c = desc->next; c != NULL; c = c->next) {
        if (c->type == AX_RENDERER_CHAIN_SOFT) {
            sd = (const ax_render_soft_desc *)(const void *)c;
        }
    }
    res = ax_render_soft_create(desc->allocator, sd, &r);
    if (res != AX_OK) {
        return res;
    }
    ws = (soft_windows *)ax__render_soft_alloc_user(r, sizeof *ws);
    if (ws == NULL) {
        ax_render_soft_destroy(r);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_soft: out of memory");
    }
    memset(ws, 0, sizeof *ws);
    ws->r = r;
    ws->background = desc->initial_background;
    *out_self = r;
    return AX_OK;
}

static void vt_destroy(void *self)
{
    ax_render_soft *r = (ax_render_soft *)self;
    soft_windows *ws = windows_of(r);
    uint32_t i;
    for (i = 0; ws != NULL && i < ws->count; ++i) {
        ax_surface_cpu_release(ws->s[i].surface);
    }
    ax_render_soft_destroy(r);
}

static ax_result vt_attach(void *self, ax_surface surface)
{
    soft_windows *ws = windows_of((ax_render_soft *)self);
    soft_surface *s;
    if (find(ws, surface) != NULL) {
        return AX_OK;
    }
    if (ws->count == SOFT_MAX_SURFACES) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_render_soft: at most %d surfaces",
                            SOFT_MAX_SURFACES);
    }
    s = &ws->s[ws->count++];
    memset(s, 0, sizeof *s);
    s->surface = surface;
    s->attach_ns = ax_time_monotonic_ns();
    s->stats.struct_size = sizeof s->stats;
    return AX_OK;
}

static void vt_detach(void *self, ax_surface surface)
{
    soft_windows *ws = windows_of((ax_render_soft *)self);
    soft_surface *s = find(ws, surface);
    if (s == NULL) {
        return;
    }
    ax_surface_cpu_release(surface);
    *s = ws->s[--ws->count];
}

static ax_result render_frame(void *self, ax_surface surface, const ax_render_ir *ir,
                              ax_frame_info info)
{
    ax_render_soft *r = (ax_render_soft *)self;
    soft_windows *ws = windows_of(r);
    soft_surface *s = find(ws, surface);
    ax_frame_result local;
    ax_frame_result *out = info.result != NULL ? info.result : &local;
    ax_frame_target t;
    ax_cpu_buffer buf;
    ax_soft_target st;
    ax_result res;

    if (s == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_render_soft: surface not attached");
    }
    AX_ASSERT(info.struct_size >= sizeof(ax_frame_info));
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->status = AX_FRAME_NOTHING;

    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    res = ax_surface_begin_frame(surface, &t);
    out->target = t;
    if (res != AX_OK || !t.ready) {
        return res;
    }
    if ((info.flags & AX_FRAME_ONLY_IF_REQUIRED) != 0 && !t.acks_configure && !t.commit_requested) {
        ax_surface_rest(surface); /* nothing to draw: frames stop (decision 95) */
        return AX_OK;
    }
    /* A configure is acknowledged only with content recorded for its logical size, so content
     * is never stretched (decision 54). */
    if (t.acks_configure && ir != NULL && (info.flags & AX_FRAME_ACCEPT_ANY_SIZE) == 0 &&
        (ir->logical_width != t.logical_width || ir->logical_height != t.logical_height)) {
        s->stats.content_mismatches++;
        out->status = AX_FRAME_CONTENT_MISMATCH;
        return AX_OK;
    }
    memset(&buf, 0, sizeof buf);
    buf.struct_size = sizeof buf;
    res = ax_surface_cpu_acquire(surface, &t, &buf);
    if (res == AX_ERR_TIMEOUT) {
        s->stats.buffers_busy++; /* the compositor still holds every buffer: next frame */
        return AX_OK;
    }
    if (res != AX_OK) {
        return res;
    }
    if (t.acks_configure && ir != NULL &&
        (ir->logical_width != t.logical_width || ir->logical_height != t.logical_height)) {
        s->stats.accepted_any_size++;
    }
    memset(&st, 0, sizeof st);
    st.struct_size = sizeof st;
    st.pixels = buf.pixels;
    st.width = buf.width;
    st.height = buf.height;
    st.stride = buf.stride;
    st.format = AX_SOFT_BGRA8;
    st.scale = t.scale;
    st.background = ws->background;
    res = ax_render_soft_draw_ir(r, &st, ir, &out->counts);
    if (res != AX_OK) {
        /* Still present: the configure must be answered. Fill with the background. */
        (void)ax_render_soft_draw_ir(r, &st, NULL, NULL);
    }
    res = ax_surface_cpu_present(surface, &t, &buf);
    if (res != AX_OK) {
        return res;
    }
    out->status = ir != NULL ? AX_FRAME_PRESENTED : AX_FRAME_CLEARED;
    s->stats.frames++;
    if (s->stats.attach_to_first_frame_ns == 0) {
        s->stats.attach_to_first_frame_ns = ax_time_monotonic_ns() - s->attach_ns;
    }
    return AX_OK;
}

/* Every frame, committed or not, ends the presenter's frame (decision 95): a frame that drew
 * nothing must not keep the window from being destroyed. */
static ax_result vt_render(void *self, ax_surface surface, const ax_render_ir *ir,
                           ax_frame_info info)
{
    ax_result r = render_frame(self, surface, ir, info);
    ax_surface_end_frame(surface);
    return r;
}

static void vt_query_caps(void *self, ax_renderer_caps *out)
{
    uint32_t size;
    (void)self;
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_renderer_caps));
    size = out->struct_size;
    memset(out, 0, sizeof *out);
    out->struct_size = size;
    out->max_texture_size = 32768;
    out->ir_version_major = AX_IR_VERSION_MAJOR;
    out->ir_version_minor = AX_IR_VERSION_MINOR;
}

static ax_result vt_wait_frame(void *self, ax_surface surface, bool animating, uint64_t timeout_ns,
                               ax_wake_reason *out_reason)
{
    (void)self;
    (void)animating; /* the compositor's frame callbacks pace every frame */
    return ax_surface_wait_frame(surface, timeout_ns, out_reason);
}

static ax_result vt_wait_frames(void *self, const ax_surface *surfaces, uint32_t count,
                                bool animating, uint64_t timeout_ns, ax_wake_reason *out_reasons)
{
    (void)self;
    (void)animating;
    return ax_surface_wait_frames(surfaces, count, timeout_ns, out_reasons);
}

static const ax_renderer_vtbl k_vtbl = {
    sizeof(ax_renderer_vtbl),
    vt_create,
    vt_destroy,
    vt_attach,
    vt_detach,
    vt_render,
    vt_query_caps,
    vt_wait_frame,
    vt_wait_frames,
};

const ax_renderer_vtbl *ax_render_soft_vtbl(void)
{
    return &k_vtbl;
}

void ax_render_soft_get_surface_stats(ax_render_soft *r, ax_surface surface,
                                      ax_render_soft_surface_stats *out)
{
    soft_windows *ws = windows_of(r);
    soft_surface *s = ws != NULL ? find(ws, surface) : NULL;
    uint32_t size;
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_render_soft_surface_stats));
    size = out->struct_size;
    if (s == NULL) {
        memset(out, 0, sizeof *out);
    } else {
        *out = s->stats;
    }
    out->struct_size = size;
}
