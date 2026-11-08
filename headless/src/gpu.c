/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The headless host's GPU renderer: ax_render_wgpu drawing into an offscreen texture, read back
 * into memory (decision 85). Everything runs on the thread that created the hl_gpu.
 */
#include <stdlib.h>
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_thread.h"
#include "axion/render/ax_render_wgpu.h"
#include "hl_internal.h"

#define FORMAT WGPUTextureFormat_RGBA8Unorm

struct hl_gpu {
    ax_allocator allocator;
    ax_gpu *gpu;
    ax_render_wgpu *r;
};

static ax_gpu_desc gpu_desc(void)
{
    ax_gpu_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.adapter = AX_GPU_ADAPTER_DEFAULT;
    return d;
}

ax_result ax__hl_gpu_probe(void)
{
    ax_gpu_desc d = gpu_desc();
    ax_gpu *g = NULL;
    ax_result r = ax_gpu_create(&d, &g);
    if (r == AX_OK) {
        ax_gpu_destroy(g);
    }
    return r;
}

ax_result ax__hl_gpu_create(const ax_allocator *allocator, hl_gpu **out)
{
    ax_gpu_desc d = gpu_desc();
    ax_render_wgpu_desc wd;
    hl_gpu *g;
    ax_result r;
    uint64_t until;
    *out = NULL;
    g = (hl_gpu *)ax_mem_alloc(allocator, sizeof *g, AX_ALIGNOF(hl_gpu));
    if (g == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless: out of memory");
    }
    memset(g, 0, sizeof *g);
    g->allocator = *allocator;
    d.allocator = allocator;
    r = ax_gpu_create(&d, &g->gpu);
    if (r != AX_OK) {
        ax_mem_free(allocator, g, sizeof *g);
        return r;
    }
    memset(&wd, 0, sizeof wd);
    wd.gpu = g->gpu;
    r = ax_render_wgpu_create(allocator, &wd, &g->r);
    if (r != AX_OK) {
        ax_gpu_destroy(g->gpu);
        ax_mem_free(allocator, g, sizeof *g);
        return r;
    }
    /* Wait for the pipeline here, once: a headless frame must hold the content, never a clear. */
    ax_render_wgpu_prepare(g->r, FORMAT);
    until = ax_time_monotonic_ns() + 30000000000ull;
    while (!ax_render_wgpu_ready(g->r, FORMAT) && ax_time_monotonic_ns() < until) {
        (void)ax_gpu_poll(g->gpu);
        ax_thread_yield();
    }
    if (!ax_render_wgpu_ready(g->r, FORMAT)) {
        ax__hl_gpu_destroy(g);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_headless: the GPU pipeline is not ready");
    }
    *out = g;
    return AX_OK;
}

void ax__hl_gpu_destroy(hl_gpu *g)
{
    ax_allocator a;
    if (g == NULL) {
        return;
    }
    ax_render_wgpu_destroy(g->r);
    ax_gpu_destroy(g->gpu);
    a = g->allocator;
    ax_mem_free(&a, g, sizeof *g);
}

typedef struct map_result {
    bool done;
    WGPUMapAsyncStatus status;
} map_result;

static void on_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1,
                      void *userdata2)
{
    map_result *m = (map_result *)userdata1;
    (void)message;
    (void)userdata2;
    m->done = true;
    m->status = status;
}

ax_result ax__hl_gpu_draw(hl_gpu *g, const ax_render_ir *ir, uint32_t w, uint32_t h, float scale,
                          ax_color background, uint8_t *rgba)
{
    WGPUDevice device = ax_gpu_device(g->gpu);
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    WGPUExtent3D extent;
    WGPUBufferMapCallbackInfo cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    map_result mapped = {false, WGPUMapAsyncStatus_Error};
    const uint32_t row = (w * 4u + 255u) & ~255u; /* copies need 256-byte rows */
    ax_render_wgpu_draw draw;
    WGPUTexture tex;
    WGPUTextureView view;
    WGPUBuffer buf;
    WGPUCommandEncoder enc;
    WGPUCommandBuffer cmds;
    ax_frame_status status = AX_FRAME_NOTHING;
    ax_result r;
    uint32_t y;

    extent.width = w;
    extent.height = h;
    extent.depthOrArrayLayers = 1;
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.size = extent;
    td.format = FORMAT;
    tex = wgpuDeviceCreateTexture(device, &td);
    view = wgpuTextureCreateView(tex, NULL);
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = (uint64_t)row * h;
    buf = wgpuDeviceCreateBuffer(device, &bd);

    memset(&draw, 0, sizeof draw);
    draw.struct_size = sizeof draw;
    draw.target = view;
    draw.format = FORMAT;
    draw.width = w;
    draw.height = h;
    draw.scale = scale;
    draw.background = background;
    r = ax_render_wgpu_draw_ir(g->r, &draw, ir, &status, NULL);
    if (r == AX_OK) {
        enc = wgpuDeviceCreateCommandEncoder(device, NULL);
        src.texture = tex;
        dst.buffer = buf;
        dst.layout.bytesPerRow = row;
        dst.layout.rowsPerImage = h;
        wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &extent);
        cmds = wgpuCommandEncoderFinish(enc, NULL);
        wgpuQueueSubmit(ax_gpu_queue(g->gpu), 1, &cmds);
        wgpuCommandBufferRelease(cmds);
        wgpuCommandEncoderRelease(enc);
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.callback = on_mapped;
        cb.userdata1 = &mapped;
        if (!ax_gpu_wait(g->gpu, wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, (size_t)bd.size, cb),
                         &mapped.done, 30000000000ull) ||
            mapped.status != WGPUMapAsyncStatus_Success) {
            r = ax_set_error(AX_ERR_DEVICE_LOST, "ax_headless: reading the frame back failed");
        } else {
            const uint8_t *p = (const uint8_t *)wgpuBufferGetConstMappedRange(buf, 0, bd.size);
            for (y = 0; y < h; ++y) {
                memcpy(rgba + (size_t)y * w * 4u, p + (size_t)y * row, (size_t)w * 4u);
            }
            wgpuBufferUnmap(buf);
        }
    }
    wgpuBufferRelease(buf);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(tex);
    return r;
}
