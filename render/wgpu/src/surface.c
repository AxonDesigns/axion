/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_wgpu as an ax_renderer_vtbl: window surfaces through ax_gpu_surface.
 */
#include <string.h>

#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/base/ax_thread.h>
#include <axion/render/ax_render_wgpu_surface.h>

#include "rw_internal.h"

struct rw_surface {
    ax_surface surface;
    ax_gpu_surface *gs;
    WGPUTextureFormat format;
    uint64_t attach_ns;
    ax_render_wgpu_surface_stats stats;
};

static rw_surface *find_surface(ax_render_wgpu *r, ax_surface surface)
{
    uint32_t i;
    for (i = 0; i < r->surface_count; ++i) {
        if (r->surfaces[i].surface.h == surface.h) {
            return &r->surfaces[i];
        }
    }
    return NULL;
}

void ax__render_wgpu_destroy_surfaces(ax_render_wgpu *r)
{
    uint32_t i;
    for (i = 0; i < r->surface_count; ++i) {
        ax_gpu_surface_destroy(r->surfaces[i].gs);
    }
    if (r->surfaces != NULL) {
        ax_mem_free(r->allocator, r->surfaces, sizeof(rw_surface) * RW_MAX_SURFACES);
    }
    r->surfaces = NULL;
    r->surface_count = 0;
}

static ax_result vt_create(const ax_renderer_desc *desc, void **out_self)
{
    const ax_renderer_chain *c;
    const ax_render_wgpu_desc *wd = NULL;
    ax_render_wgpu *r = NULL;
    ax_result res;
    AX_ASSERT(desc != NULL && desc->struct_size >= sizeof(ax_renderer_desc));
    *out_self = NULL;
    for (c = desc->next; c != NULL; c = c->next) {
        if (c->type == AX_RENDERER_CHAIN_WGPU) {
            wd = (const ax_render_wgpu_desc *)(const void *)c;
        }
    }
    res = ax_render_wgpu_create(desc->allocator, wd, &r);
    if (res != AX_OK) {
        return res;
    }
    r->initial_background = desc->initial_background;
    *out_self = r;
    return AX_OK;
}

static void vt_destroy(void *self)
{
    ax_render_wgpu_destroy((ax_render_wgpu *)self);
}

static ax_result vt_attach(void *self, ax_surface surface)
{
    ax_render_wgpu *r = (ax_render_wgpu *)self;
    ax_gpu_surface_desc sd;
    ax_gpu_surface_info info;
    rw_surface *s;
    ax_result res;

    if (find_surface(r, surface) != NULL) {
        return AX_OK;
    }
    if (r->surfaces == NULL) {
        r->surfaces = (rw_surface *)ax_mem_alloc(r->allocator, sizeof(rw_surface) * RW_MAX_SURFACES,
                                                 AX_ALIGNOF(rw_surface));
        if (r->surfaces == NULL) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_wgpu: out of memory");
        }
    }
    if (r->surface_count == RW_MAX_SURFACES) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_render_wgpu: at most %d surfaces",
                            RW_MAX_SURFACES);
    }
    s = &r->surfaces[r->surface_count];
    memset(s, 0, sizeof *s);
    memset(&sd, 0, sizeof sd);
    sd.struct_size = sizeof sd;
    sd.pacing = (ax_gpu_pacing)r->pacing;
    sd.present_mode = (WGPUPresentMode)r->present_mode;
    sd.srgb_view = r->linear_blend;
    res = ax_gpu_surface_create(r->gpu, surface, &sd, &s->gs);
    if (res != AX_OK) {
        return res;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    ax_gpu_surface_get_info(s->gs, &info);
    s->surface = surface;
    s->format = r->linear_blend ? info.srgb_view_format : info.view_format;
    s->attach_ns = ax_time_monotonic_ns();
    s->stats.struct_size = sizeof s->stats;
    r->surface_count++;
    /* Compile now, without waiting: the first frames are cleared until it is ready. */
    ax_render_wgpu_prepare(r, s->format);
    return AX_OK;
}

static void vt_detach(void *self, ax_surface surface)
{
    ax_render_wgpu *r = (ax_render_wgpu *)self;
    rw_surface *s = find_surface(r, surface);
    if (s == NULL) {
        return;
    }
    ax_gpu_surface_destroy(s->gs);
    *s = r->surfaces[--r->surface_count];
}

static ax_result render_frame(void *self, ax_surface surface, const ax_render_ir *ir,
                              ax_frame_info info)
{
    ax_render_wgpu *r = (ax_render_wgpu *)self;
    rw_surface *s = find_surface(r, surface);
    ax_frame_result local;
    ax_frame_result *out = info.result != NULL ? info.result : &local;
    ax_render_wgpu_draw draw;
    ax_gpu_frame f;
    ax_frame_status status = AX_FRAME_NOTHING;
    WGPUTextureView srgb_view = NULL;
    ax_result res;

    if (s == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_render_wgpu: surface not attached");
    }
    AX_ASSERT(info.struct_size >= sizeof(ax_frame_info));
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->status = AX_FRAME_NOTHING;

    memset(&f, 0, sizeof f);
    f.struct_size = sizeof f;
    f.only_if_required = (info.flags & AX_FRAME_ONLY_IF_REQUIRED) != 0;
    if (ir != NULL && (info.flags & AX_FRAME_ACCEPT_ANY_SIZE) == 0) {
        f.content_logical_width = ir->logical_width;
        f.content_logical_height = ir->logical_height;
    }
    res = ax_gpu_surface_begin_frame(s->gs, &f);
    out->target = f.target;
    if (res != AX_OK) {
        return res;
    }
    if (f.content_mismatch) {
        s->stats.content_mismatches++;
        out->status = AX_FRAME_CONTENT_MISMATCH;
        return AX_OK;
    }
    if (f.texture == NULL) {
        return AX_OK;
    }
    if (f.target.acks_configure && ir != NULL &&
        (ir->logical_width != f.target.logical_width ||
         ir->logical_height != f.target.logical_height)) {
        s->stats.accepted_any_size++;
    }

    memset(&draw, 0, sizeof draw);
    draw.struct_size = sizeof draw;
    draw.target = f.view;
    if (r->linear_blend) {
        /* Diagnostic: blend in linear light through the sRGB view of the same texture. */
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        vd.format = s->format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        srgb_view = wgpuTextureCreateView(f.texture, &vd);
        draw.target = srgb_view;
    }
    draw.format = s->format;
    draw.width = f.width;
    draw.height = f.height;
    draw.scale = f.target.scale;
    draw.background = r->initial_background;
    res = ax_render_wgpu_draw_ir(r, &draw, ir, &status, &out->counts);
    if (srgb_view != NULL) {
        wgpuTextureViewRelease(srgb_view);
    }
    if (res != AX_OK) {
        /* Still present (the clear went out, or nothing): the configure must be answered. */
        status = AX_FRAME_CLEARED;
    }
    res = ax_gpu_surface_present(s->gs, &f);
    if (res != AX_OK) {
        return res;
    }
    out->status = status;
    if (s->stats.attach_to_first_frame_ns == 0) {
        s->stats.attach_to_first_frame_ns = ax_time_monotonic_ns() - s->attach_ns;
    }
    if (status == AX_FRAME_PRESENTED && s->stats.attach_to_first_content_ns == 0) {
        s->stats.attach_to_first_content_ns = ax_time_monotonic_ns() - s->attach_ns;
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
    ax_render_wgpu *r = (ax_render_wgpu *)self;
    WGPULimits limits = WGPU_LIMITS_INIT;
    uint32_t size;
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_renderer_caps));
    size = out->struct_size;
    memset(out, 0, sizeof *out);
    out->struct_size = size;
    if (wgpuDeviceGetLimits(ax_gpu_device(r->gpu), &limits) == WGPUStatus_Success) {
        out->max_texture_size = limits.maxTextureDimension2D;
    }
    out->flags = AX_RENDERER_CAP_COMPUTE;
    out->ir_version_major = AX_IR_VERSION_MAJOR;
    out->ir_version_minor = AX_IR_VERSION_MINOR;
}

static ax_result vt_wait_frame(void *self, ax_surface surface, bool animating, uint64_t timeout_ns,
                               ax_wake_reason *out_reason)
{
    ax_render_wgpu *r = (ax_render_wgpu *)self;
    rw_surface *s = find_surface(r, surface);
    if (s == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_render_wgpu: surface not attached");
    }
    return ax_gpu_surface_wait(s->gs, animating, timeout_ns, out_reason);
}

static ax_result vt_wait_frames(void *self, const ax_surface *surfaces, uint32_t count,
                                bool animating, uint64_t timeout_ns, ax_wake_reason *out_reasons)
{
    ax_render_wgpu *r = (ax_render_wgpu *)self;
    ax_gpu_surface *gs[AX_SURFACE_WAIT_MAX];
    uint32_t i;
    if (surfaces == NULL || count == 0 || count > AX_SURFACE_WAIT_MAX) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_render_wgpu: 1 to %u surfaces",
                            (unsigned)AX_SURFACE_WAIT_MAX);
    }
    for (i = 0; i < count; ++i) {
        rw_surface *s = find_surface(r, surfaces[i]);
        if (s == NULL) {
            return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_render_wgpu: surface not attached");
        }
        gs[i] = s->gs;
    }
    return ax_gpu_surfaces_wait(gs, count, animating, timeout_ns, out_reasons);
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

const ax_renderer_vtbl *ax_render_wgpu_vtbl(void)
{
    return &k_vtbl;
}

ax_gpu_surface *ax_render_wgpu_gpu_surface(ax_render_wgpu *r, ax_surface surface)
{
    rw_surface *s = find_surface(r, surface);
    return s != NULL ? s->gs : NULL;
}

void ax_render_wgpu_get_surface_stats(ax_render_wgpu *r, ax_surface surface,
                                      ax_render_wgpu_surface_stats *out)
{
    rw_surface *s = find_surface(r, surface);
    uint32_t size;
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_render_wgpu_surface_stats));
    size = out->struct_size;
    if (s == NULL) {
        memset(out, 0, sizeof *out);
    } else {
        *out = s->stats;
    }
    out->struct_size = size;
}
