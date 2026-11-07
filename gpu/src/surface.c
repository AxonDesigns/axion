/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_gpu_surface: a window's swapchain (decision 48).
 */
#include <stdio.h>
#include <string.h>

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/gpu/ax_gpu_surface.h>
#include <axion/platform/ax_surface_native.h>

#include "gpu_internal.h"

struct ax_gpu_surface {
    ax_gpu *gpu;
    ax_surface surface;
    ax_surface_native native;
    WGPUSurface wsurface;
    uint32_t surface_generation; /* device generation the WGPUSurface was made for */

    ax_gpu_pacing pacing;
    WGPUPresentMode present_mode;           /* asked for */
    WGPUPresentMode effective_present_mode; /* what the swapchain uses */
    bool present_mode_supported;
    WGPUTextureFormat format;
    WGPUTextureFormat view_format;
    WGPUCompositeAlphaMode alpha_mode;

    bool configured;
    uint32_t width, height;

    WGPUTexture texture; /* the frame in progress */
    WGPUTextureView view;

    ax_gpu_surface_stats stats;
};

static WGPUTextureFormat srgb_of(WGPUTextureFormat f)
{
    switch (f) {
    case WGPUTextureFormat_BGRA8Unorm:
        return WGPUTextureFormat_BGRA8UnormSrgb;
    case WGPUTextureFormat_RGBA8Unorm:
        return WGPUTextureFormat_RGBA8UnormSrgb;
    default:
        return f;
    }
}

static bool is_srgb(WGPUTextureFormat f)
{
    return f == WGPUTextureFormat_BGRA8UnormSrgb || f == WGPUTextureFormat_RGBA8UnormSrgb;
}

/*
 * The pacing used when the caller asks for AX_GPU_PACING_DEFAULT (decision 49): the compositor's,
 * on every driver measured. With FIFO (driver pacing) a resize drops to a third of the refresh
 * rate on KWin, because each frame that acknowledges a configure must first wait for the
 * swapchain's queue to drain (decision 48). Measured with NVIDIA 615.78 on KWin 6.8; lavapipe
 * (Mesa's WSI) behaves the same, but Mesa on real AMD or Intel hardware is still to be validated.
 * Per-driver choices go here: the adapter's vendor and driver are in ax_gpu_get_info.
 */
static ax_gpu_pacing default_pacing(ax_gpu *gpu)
{
    (void)gpu;
    return AX_GPU_PACING_COMPOSITOR;
}

/* The mode a swapchain ends up with: the one asked for when the surface lists it, else the next
 * one in the fallback order Immediate -> Mailbox -> FifoRelaxed -> Fifo (Dawn's; FIFO is always
 * supported). */
static WGPUPresentMode effective_present_mode(WGPUPresentMode want,
                                              const WGPUSurfaceCapabilities *caps)
{
    static const WGPUPresentMode order[] = {WGPUPresentMode_Immediate, WGPUPresentMode_Mailbox,
                                            WGPUPresentMode_FifoRelaxed, WGPUPresentMode_Fifo};
    size_t start = 0, i, j;
    while (start < 3 && order[start] != want) {
        start++;
    }
    for (i = start; i < 4; ++i) {
        for (j = 0; j < caps->presentModeCount; ++j) {
            if (caps->presentModes[j] == order[i]) {
                return order[i];
            }
        }
    }
    return WGPUPresentMode_Fifo;
}

/*
 * Whether commits of this swapchain can wait in a queue on the compositor's side, behind a FIFO
 * barrier or (later) a commit-timing target. While they can, a configure is not acknowledged
 * until the queue has drained (decision 48). Today that is FIFO and FIFO-relaxed: Mesa's
 * swapchains use wp_fifo_v1 for them. Adopting wp_commit_timing_v1 (decision 38, risk 6) adds
 * a case here.
 */
static bool commits_may_queue(const ax_gpu_surface *s)
{
    return s->effective_present_mode == WGPUPresentMode_Fifo ||
           s->effective_present_mode == WGPUPresentMode_FifoRelaxed;
}

static ax_result create_wsurface(ax_gpu_surface *s)
{
    WGPUSurfaceSourceWaylandSurface wayland = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    size_t i;

    wayland.display = s->native.u.wayland.display;
    wayland.surface = s->native.u.wayland.surface;
    desc.nextInChain = &wayland.chain;
    s->wsurface = wgpuInstanceCreateSurface(s->gpu->instance, &desc);
    if (s->wsurface == NULL) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_gpu_surface: cannot create a WebGPU surface");
    }
    s->surface_generation = s->gpu->generation;
    s->configured = false;

    if (wgpuSurfaceGetCapabilities(s->wsurface, s->gpu->adapter, &caps) != WGPUStatus_Success ||
        caps.formatCount == 0) {
        wgpuSurfaceRelease(s->wsurface);
        s->wsurface = NULL;
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_gpu_surface: the adapter cannot present to this surface");
    }

    /* sRGB: a format that is sRGB itself, else the first format viewed as sRGB. */
    s->format = caps.formats[0];
    for (i = 0; i < caps.formatCount; ++i) {
        if (is_srgb(caps.formats[i])) {
            s->format = caps.formats[i];
            break;
        }
    }
    s->view_format = srgb_of(s->format);

    /* Opaque for now; transparency comes with the Windowing API. */
    s->alpha_mode = WGPUCompositeAlphaMode_Auto;
    for (i = 0; i < caps.alphaModeCount; ++i) {
        if (caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque) {
            s->alpha_mode = WGPUCompositeAlphaMode_Opaque;
        }
    }

    s->effective_present_mode = effective_present_mode(s->present_mode, &caps);
    s->present_mode_supported = s->effective_present_mode == s->present_mode;
    if (!s->present_mode_supported) {
        fprintf(stderr, "ax_gpu_surface: present mode %d is not supported; using %d\n",
                (int)s->present_mode, (int)s->effective_present_mode);
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return AX_OK;
}

static void release_frame(ax_gpu_surface *s)
{
    if (s->view != NULL) {
        wgpuTextureViewRelease(s->view);
        s->view = NULL;
    }
    if (s->texture != NULL) {
        wgpuTextureRelease(s->texture);
        s->texture = NULL;
    }
}

static void configure(ax_gpu_surface *s, uint32_t width, uint32_t height)
{
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    uint64_t t0 = ax_time_monotonic_ns();
    uint64_t dt;
    bool resize = s->configured && (width != s->width || height != s->height);

    config.device = s->gpu->device;
    config.format = s->format;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.viewFormatCount = s->view_format != s->format ? 1 : 0;
    config.viewFormats = &s->view_format;
    config.alphaMode = s->alpha_mode;
    config.presentMode = s->present_mode;
    wgpuSurfaceConfigure(s->wsurface, &config);
    dt = ax_time_monotonic_ns() - t0;

    s->configured = true;
    s->width = width;
    s->height = height;
    if (resize) {
        s->stats.resizes++;
        s->stats.resize_ns_total += dt;
        if (dt > s->stats.resize_ns_max) {
            s->stats.resize_ns_max = dt;
        }
    }
}

ax_result ax_gpu_surface_create(ax_gpu *gpu, ax_surface surface, const ax_gpu_surface_desc *desc,
                                ax_gpu_surface **out_surface)
{
    ax_gpu_surface *s;
    ax_result r;

    AX_GPU_ASSERT_THREAD(gpu);
    AX_ASSERT(desc != NULL && desc->struct_size >= sizeof(ax_gpu_surface_desc));
    AX_ASSERT(out_surface != NULL);
    *out_surface = NULL;

    s = AX_MEM_NEW(gpu->allocator, ax_gpu_surface);
    if (s == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_gpu_surface: out of memory");
    }
    memset(s, 0, sizeof *s);
    s->gpu = gpu;
    s->surface = surface;
    s->stats.struct_size = sizeof s->stats;
    s->native.struct_size = sizeof s->native;
    r = ax_surface_get_native(surface, &s->native);
    if (r != AX_OK) {
        AX_MEM_DELETE(gpu->allocator, s);
        return r;
    }
    if (s->native.kind != AX_NATIVE_WAYLAND) {
        AX_MEM_DELETE(gpu->allocator, s);
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_gpu_surface: unsupported windowing system (kind %u)",
                            (unsigned)s->native.kind);
    }

    s->pacing = desc->pacing != AX_GPU_PACING_DEFAULT ? desc->pacing : default_pacing(gpu);
    s->present_mode = desc->present_mode;
    if (s->present_mode == WGPUPresentMode_Undefined) {
        s->present_mode =
            s->pacing == AX_GPU_PACING_DRIVER ? WGPUPresentMode_Fifo : WGPUPresentMode_Mailbox;
    }

    r = create_wsurface(s);
    if (r != AX_OK) {
        AX_MEM_DELETE(gpu->allocator, s);
        return r;
    }
    *out_surface = s;
    return AX_OK;
}

void ax_gpu_surface_destroy(ax_gpu_surface *s)
{
    if (s == NULL) {
        return;
    }
    AX_GPU_ASSERT_THREAD(s->gpu);
    release_frame(s);
    if (s->wsurface != NULL) {
        if (s->configured) {
            wgpuSurfaceUnconfigure(s->wsurface);
        }
        wgpuSurfaceRelease(s->wsurface);
    }
    AX_MEM_DELETE(s->gpu->allocator, s);
}

/* After a device loss (a new generation) or a lost surface, the surface is recreated from its
 * descriptor: a swapchain cannot move to another device. */
static ax_result ensure_wsurface(ax_gpu_surface *s)
{
    if (s->wsurface != NULL && s->surface_generation == s->gpu->generation) {
        return AX_OK;
    }
    if (s->wsurface != NULL) {
        wgpuSurfaceRelease(s->wsurface);
        s->wsurface = NULL;
    }
    s->stats.recreations++;
    return create_wsurface(s);
}

ax_result ax_gpu_surface_begin_frame(ax_gpu_surface *s, ax_gpu_frame *frame)
{
    WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
    WGPUTextureViewDescriptor vdesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    uint32_t w, h;
    uint64_t t0, dt;
    ax_result r;
    int attempt;

    AX_GPU_ASSERT_THREAD(s->gpu);
    AX_ASSERT(frame != NULL && frame->struct_size >= sizeof(ax_gpu_frame));
    AX_ASSERT_MSG(s->texture == NULL, "ax_gpu_surface_begin_frame: previous frame not presented");
    frame->texture = NULL;
    frame->view = NULL;
    frame->width = frame->height = 0;
    frame->resized = false;
    memset(&frame->target, 0, sizeof frame->target);
    frame->target.struct_size = sizeof frame->target;

    r = ax_surface_begin_frame(s->surface, &frame->target);
    if (r != AX_OK || !frame->target.ready) {
        return r;
    }
    if (frame->only_if_required && !frame->target.acks_configure &&
        !frame->target.commit_requested) {
        return AX_OK;
    }
    if (s->gpu->lost) {
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu_surface: the device is lost");
    }
    r = ensure_wsurface(s);
    if (r != AX_OK) {
        return r;
    }
    /* Never acknowledge a configure while older commits may still wait in the compositor's
     * queue (decision 48): drain it in a short slice, and if it does not drain, skip the frame
     * without acknowledging or committing, so the queue can only shrink, and retry next frame. */
    if (frame->target.acks_configure && commits_may_queue(s)) {
        bool applied = false;
        uint64_t slice = 4 * frame->target.refresh_interval_ns;
        slice = slice < 20000000u ? 20000000u : (slice > 50000000u ? 50000000u : slice);
        t0 = ax_time_monotonic_ns();
        (void)ax_surface_wait_commits_applied(s->surface, slice, &applied);
        dt = ax_time_monotonic_ns() - t0;
        s->stats.drain_ns_total += dt;
        if (dt > s->stats.drain_ns_max) {
            s->stats.drain_ns_max = dt;
        }
        if (!applied) {
            s->stats.deferred_acks++;
            return AX_OK;
        }
    }

    /* The swapchain takes the exact pixel size of the configure this frame acknowledges, before
     * the texture is acquired. */
    w = frame->target.pixel_width;
    h = frame->target.pixel_height;
    if (!s->configured || w != s->width || h != s->height) {
        frame->resized = s->configured;
        configure(s, w, h);
    }

    for (attempt = 0; attempt < 2; ++attempt) {
        t0 = ax_time_monotonic_ns();
        wgpuSurfaceGetCurrentTexture(s->wsurface, &st);
        dt = ax_time_monotonic_ns() - t0;
        s->stats.acquire_ns_total += dt;
        if (dt > s->stats.acquire_ns_max) {
            s->stats.acquire_ns_max = dt;
        }
        if (st.status == WGPUSurfaceGetCurrentTextureStatus_Outdated && attempt == 0) {
            if (st.texture != NULL) {
                wgpuTextureRelease(st.texture);
                st.texture = NULL;
            }
            configure(s, w, h);
            continue;
        }
        break;
    }

    switch (st.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        s->surface_generation = 0; /* recreate it next frame */
        s->stats.skipped_frames++;
        if (st.texture != NULL) {
            wgpuTextureRelease(st.texture);
        }
        return AX_OK;
    default: /* Timeout, Outdated twice, Error (a lost device shows up in ax_gpu_poll) */
        s->stats.skipped_frames++;
        if (st.texture != NULL) {
            wgpuTextureRelease(st.texture);
        }
        return AX_OK;
    }

    vdesc.format = s->view_format;
    vdesc.dimension = WGPUTextureViewDimension_2D;
    vdesc.mipLevelCount = 1;
    vdesc.arrayLayerCount = 1;
    s->texture = st.texture;
    s->view = wgpuTextureCreateView(st.texture, &vdesc);
    frame->texture = s->texture;
    frame->view = s->view;
    frame->width = w;
    frame->height = h;
    return AX_OK;
}

ax_result ax_gpu_surface_present(ax_gpu_surface *s, ax_gpu_frame *frame)
{
    ax_result r;
    WGPUStatus status;
    uint64_t t0, dt;

    AX_GPU_ASSERT_THREAD(s->gpu);
    AX_ASSERT(frame != NULL);
    if (frame->texture == NULL || frame->texture != s->texture) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_gpu_surface_present: no frame to present");
    }

    /* Acknowledge, set the viewport and ask for timing; the present's commit carries it all. */
    r = ax_surface_prepare_commit(s->surface, &frame->target);
    t0 = ax_time_monotonic_ns();
    status = wgpuSurfacePresent(s->wsurface);
    dt = ax_time_monotonic_ns() - t0;
    s->stats.present_ns_total += dt;
    if (dt > s->stats.present_ns_max) {
        s->stats.present_ns_max = dt;
    }
    if (status == WGPUStatus_Success) {
        s->stats.frames++;
    } else {
        s->stats.failed_presents++;
        if (r == AX_OK) {
            (void)ax_surface_commit_failed(s->surface);
        }
        r = ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu_surface_present: the present failed");
    }
    release_frame(s);
    frame->texture = NULL;
    frame->view = NULL;
    return r;
}

ax_result ax_gpu_surface_wait(ax_gpu_surface *s, bool animating, uint64_t timeout_ns,
                              ax_wake_reason *out_reason)
{
    ax_wake_reason reason = AX_WAKE_TIMEOUT;
    ax_result r;

    AX_GPU_ASSERT_THREAD(s->gpu);
    r = ax_gpu_poll(s->gpu);
    if (s->pacing == AX_GPU_PACING_DRIVER && animating) {
        /* The swapchain throttles; only run the surface's listeners (presentation feedback). */
        (void)ax_surface_wait_frame(s->surface, 0, &reason);
        if (reason == AX_WAKE_TIMEOUT) {
            reason = AX_WAKE_FRAME;
        }
    } else {
        ax_result w = ax_surface_wait_frame(s->surface, timeout_ns, &reason);
        if (r == AX_OK) {
            r = w;
        }
    }
    if (out_reason != NULL) {
        *out_reason = reason;
    }
    return r;
}

void ax_gpu_surface_get_info(ax_gpu_surface *s, ax_gpu_surface_info *out)
{
    AX_GPU_ASSERT_THREAD(s->gpu);
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_gpu_surface_info));
    out->pacing = s->pacing;
    out->present_mode = s->present_mode;
    out->present_mode_supported = s->present_mode_supported;
    out->effective_present_mode = s->effective_present_mode;
    out->format = s->format;
    out->view_format = s->view_format;
    out->width = s->width;
    out->height = s->height;
}

void ax_gpu_surface_get_stats(ax_gpu_surface *s, ax_gpu_surface_stats *out, bool reset)
{
    uint32_t size;
    AX_GPU_ASSERT_THREAD(s->gpu);
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_gpu_surface_stats));
    size = out->struct_size;
    *out = s->stats;
    out->struct_size = size;
    if (reset) {
        memset(&s->stats, 0, sizeof s->stats);
        s->stats.struct_size = sizeof s->stats;
    }
}
