/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window, GPU renderer (--renderer=gpu): the same picture as shm.c, drawn through ax_gpu into
 * the window's swapchain. The pipeline below lives only in this example (fixed WGSL, instanced
 * quads); it is not AXION's renderer.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "axion/ax_base.h"
#include "axion/base/ax_thread.h"
#include "axion/gpu/ax_gpu_surface.h"
#include "m0_window.h"

static const char shader_wgsl[] =
    "struct Viewport { size: vec2f, pad: vec2f };\n"
    "@group(0) @binding(0) var<uniform> vp: Viewport;\n"
    "struct VOut { @builtin(position) pos: vec4f, @location(0) color: vec4f };\n"
    "@vertex fn vs(@builtin(vertex_index) vi: u32, @location(0) rect: vec4f,\n"
    "              @location(1) color: vec4f) -> VOut {\n"
    "    var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),\n"
    "                                  vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));\n"
    "    let p = mix(rect.xy, rect.zw, corners[vi]);\n"
    "    var o: VOut;\n"
    "    o.pos = vec4f(p.x / vp.size.x * 2.0 - 1.0, 1.0 - p.y / vp.size.y * 2.0, 0.0, 1.0);\n"
    "    o.color = color;\n"
    "    return o;\n"
    "}\n"
    "@fragment fn fs(i: VOut) -> @location(0) vec4f { return i.color; }\n";

enum { MAX_RECTS = 8 };

typedef struct rect_instance {
    float rect[4];  /* x0, y0, x1, y1 in pixels */
    float color[4]; /* linear RGBA */
} rect_instance;

typedef struct pipeline {
    uint32_t generation; /* device generation it was built for; 0 = none */
    WGPURenderPipeline pipeline;
    WGPUBuffer uniforms;
    WGPUBuffer instances;
    WGPUBindGroup bind_group;
} pipeline;

static WGPUStringView view_of(const char *s)
{
    WGPUStringView v = {s, strlen(s)};
    return v;
}

static float srgb_to_linear(uint32_t c8)
{
    float c = (float)c8 / 255.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/* XRGB8888 sRGB (as shm.c paints it) to linear, which the sRGB view encodes back on write. */
static void linear_color(uint32_t xrgb, float out[4])
{
    out[0] = srgb_to_linear((xrgb >> 16) & 0xFFu);
    out[1] = srgb_to_linear((xrgb >> 8) & 0xFFu);
    out[2] = srgb_to_linear(xrgb & 0xFFu);
    out[3] = 1.0f;
}

static void release_pipeline(pipeline *p)
{
    if (p->bind_group != NULL) {
        wgpuBindGroupRelease(p->bind_group);
    }
    if (p->instances != NULL) {
        wgpuBufferRelease(p->instances);
    }
    if (p->uniforms != NULL) {
        wgpuBufferRelease(p->uniforms);
    }
    if (p->pipeline != NULL) {
        wgpuRenderPipelineRelease(p->pipeline);
    }
    memset(p, 0, sizeof *p);
}

/* (Re)builds the pipeline for the current device (after a device loss the old one is dead). */
static bool ensure_pipeline(pipeline *p, ax_gpu *gpu, WGPUTextureFormat format)
{
    WGPUDevice device = ax_gpu_device(gpu);
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    WGPUShaderModuleDescriptor sm_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    WGPUVertexAttribute attrs[2] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
    WGPUVertexBufferLayout vb = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    WGPURenderPipelineDescriptor rp_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    WGPUBufferDescriptor b_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    WGPUBindGroupDescriptor bg_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    WGPUBindGroupLayout layout;
    WGPUShaderModule module;

    if (p->generation == ax_gpu_device_generation(gpu)) {
        return true;
    }
    release_pipeline(p);

    wgsl.code = view_of(shader_wgsl);
    sm_desc.nextInChain = &wgsl.chain;
    module = wgpuDeviceCreateShaderModule(device, &sm_desc);
    if (module == NULL) {
        return false;
    }

    attrs[0].format = WGPUVertexFormat_Float32x4;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4;
    attrs[1].offset = 16;
    attrs[1].shaderLocation = 1;
    vb.stepMode = WGPUVertexStepMode_Instance;
    vb.arrayStride = sizeof(rect_instance);
    vb.attributeCount = 2;
    vb.attributes = attrs;

    target.format = format;
    fragment.module = module;
    fragment.entryPoint = view_of("fs");
    fragment.targetCount = 1;
    fragment.targets = &target;
    rp_desc.label = view_of("m0_window quads");
    rp_desc.vertex.module = module;
    rp_desc.vertex.entryPoint = view_of("vs");
    rp_desc.vertex.bufferCount = 1;
    rp_desc.vertex.buffers = &vb;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.fragment = &fragment;
    p->pipeline = wgpuDeviceCreateRenderPipeline(device, &rp_desc);
    wgpuShaderModuleRelease(module);
    if (p->pipeline == NULL) {
        return false;
    }

    b_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    b_desc.size = 16;
    p->uniforms = wgpuDeviceCreateBuffer(device, &b_desc);
    b_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    b_desc.size = sizeof(rect_instance) * MAX_RECTS;
    p->instances = wgpuDeviceCreateBuffer(device, &b_desc);

    layout = wgpuRenderPipelineGetBindGroupLayout(p->pipeline, 0);
    entry.binding = 0;
    entry.buffer = p->uniforms;
    entry.size = 16;
    bg_desc.layout = layout;
    bg_desc.entryCount = 1;
    bg_desc.entries = &entry;
    p->bind_group = wgpuDeviceCreateBindGroup(device, &bg_desc);
    wgpuBindGroupLayoutRelease(layout);

    p->generation = ax_gpu_device_generation(gpu);
    return p->uniforms != NULL && p->instances != NULL && p->bind_group != NULL;
}

static uint32_t add_rect(rect_instance *r, uint32_t n, float x0, float y0, float x1, float y1,
                         uint32_t xrgb)
{
    if (n >= MAX_RECTS || x1 <= x0 || y1 <= y0) {
        return n;
    }
    r[n].rect[0] = x0;
    r[n].rect[1] = y0;
    r[n].rect[2] = x1;
    r[n].rect[3] = y1;
    linear_color(xrgb, r[n].color);
    return n + 1;
}

static void encode_frame(ax_gpu *gpu, pipeline *p, const ax_gpu_frame *f, bool csd)
{
    const float w = (float)f->width;
    const float h = (float)f->height;
    WGPUDevice device = ax_gpu_device(gpu);
    WGPUQueue queue = ax_gpu_queue(gpu);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rect_instance rects[MAX_RECTS];
    float viewport[4] = {w, h, 0.0f, 0.0f};
    float bg[4];
    m0_layout l;
    uint32_t n = 0;
    WGPUCommandEncoder encoder;
    WGPURenderPassEncoder pass;
    WGPUCommandBuffer commands;

    m0_layout_frame(&f->target, csd, &l);
    if (l.caption_px > 0) {
        const float e = (float)l.edge_px;
        n = add_rect(rects, n, 0, 0, w, (float)l.caption_px, M0_COLOR_CAPTION);
        n = add_rect(rects, n, 0, 0, w, e, M0_COLOR_EDGE);
        n = add_rect(rects, n, 0, h - e, w, h, M0_COLOR_EDGE);
        n = add_rect(rects, n, 0, 0, e, h, M0_COLOR_EDGE);
        n = add_rect(rects, n, w - e, 0, w, h, M0_COLOR_EDGE);
    }
    n = add_rect(rects, n, (float)l.bar_x0, (float)l.caption_px, (float)l.bar_x1,
                 h - (float)l.bar_bottom_px, M0_COLOR_BAR);
    wgpuQueueWriteBuffer(queue, p->uniforms, 0, viewport, sizeof viewport);
    wgpuQueueWriteBuffer(queue, p->instances, 0, rects, sizeof(rect_instance) * n);

    linear_color(M0_COLOR_BG, bg);
    color.view = f->view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.r = bg[0];
    color.clearValue.g = bg[1];
    color.clearValue.b = bg[2];
    color.clearValue.a = 1.0;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;

    encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, p->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, p->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, p->instances, 0, sizeof(rect_instance) * n);
    wgpuRenderPassEncoderDraw(pass, 6, n, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

static const char *present_mode_name(WGPUPresentMode m)
{
    switch (m) {
    case WGPUPresentMode_Fifo:
        return "fifo";
    case WGPUPresentMode_FifoRelaxed:
        return "fifo-relaxed";
    case WGPUPresentMode_Mailbox:
        return "mailbox";
    case WGPUPresentMode_Immediate:
        return "immediate";
    default:
        return "undefined";
    }
}

static void gpu_extra_stats(ax_gpu_surface *surface, char *buf, size_t cap)
{
    ax_gpu_surface_stats st;
    uint64_t n;
    st.struct_size = sizeof st;
    ax_gpu_surface_get_stats(surface, &st, true);
    n = st.frames + st.failed_presents;
    snprintf(buf, cap,
             "gpu: resizes %llu (%.2f ms mean, %.2f ms max) | acquire %.2f ms mean, %.2f ms max | "
             "present %.2f ms mean, %.2f ms max | deferred acks %llu (drain %.2f ms max) | "
             "failed %llu | skipped %llu | recreated %llu",
             (unsigned long long)st.resizes,
             st.resizes != 0 ? (double)st.resize_ns_total / (double)st.resizes / 1e6 : 0.0,
             (double)st.resize_ns_max / 1e6,
             n != 0 ? (double)st.acquire_ns_total / (double)n / 1e6 : 0.0,
             (double)st.acquire_ns_max / 1e6,
             n != 0 ? (double)st.present_ns_total / (double)n / 1e6 : 0.0,
             (double)st.present_ns_max / 1e6, (unsigned long long)st.deferred_acks,
             (double)st.drain_ns_max / 1e6, (unsigned long long)st.failed_presents,
             (unsigned long long)st.skipped_frames, (unsigned long long)st.recreations);
}

int m0_gpu_draw_thread(void *arg)
{
    shared *s = (shared *)arg;
    ax_gpu_desc gdesc = {sizeof gdesc, NULL, AX_GPU_ADAPTER_DEFAULT};
    ax_gpu_surface_desc sdesc = {sizeof sdesc, AX_GPU_PACING_DEFAULT, WGPUPresentMode_Undefined};
    ax_gpu *gpu = NULL;
    ax_gpu_surface *surface = NULL;
    ax_gpu_info info;
    ax_gpu_surface_info sinfo;
    pipeline pipe;
    m0_draw_stats stats;
    uint64_t next_stats = ax_time_monotonic_ns() + 1000000000u;
    bool draw = true;
    ax_result r;

    memset(&pipe, 0, sizeof pipe);
    memset(&stats, 0, sizeof stats);
    gdesc.adapter = (ax_gpu_adapter_kind)s->opt.adapter;
    sdesc.pacing = (ax_gpu_pacing)s->opt.pacing;
    sdesc.present_mode = (WGPUPresentMode)s->opt.present_mode;
    r = ax_gpu_create(&gdesc, &gpu);
    if (r == AX_OK) {
        r = ax_gpu_surface_create(gpu, s->surface, &sdesc, &surface);
    }
    if (r != AX_OK) {
        fprintf(stderr, "m0_window: gpu: %s: %.*s\n", ax_result_name(r),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        ax_gpu_destroy(gpu);
        ax_atomic_store_u32(&s->draw_failed, 1, AX_MO_RELEASE);
        return 1;
    }
    info.struct_size = sizeof info;
    ax_gpu_get_info(gpu, &info);
    sinfo.struct_size = sizeof sinfo;
    ax_gpu_surface_get_info(surface, &sinfo);
    printf("gpu: %s | %s | %s | pacing %s | present mode %s%s\n", info.implementation,
           info.adapter_name, info.driver,
           sinfo.pacing == AX_GPU_PACING_DRIVER ? "driver" : "compositor",
           present_mode_name(sinfo.effective_present_mode),
           sinfo.present_mode_supported ? "" : " (fallback)");
    fflush(stdout);

    while (!m0_stopping(s)) {
        ax_gpu_frame f;
        ax_wake_reason reason = AX_WAKE_TIMEOUT;
        memset(&f, 0, sizeof f);
        f.struct_size = sizeof f;
        /* After a timeout (a hidden window gets no frame callbacks) draw only what must be. */
        f.only_if_required = !draw;
        r = ax_gpu_surface_begin_frame(surface, &f);
        if (r != AX_OK && r != AX_ERR_DEVICE_LOST) {
            fprintf(stderr, "m0_window: begin_frame: %s\n", ax_result_name(r));
            ax_atomic_store_u32(&s->draw_failed, 1, AX_MO_RELEASE);
            break;
        }
        if (f.texture != NULL) {
            ax_window_info winfo;
            uint64_t t0 = ax_time_monotonic_ns();
            winfo.struct_size = sizeof winfo;
            ax_window_get_info(s->window, &winfo);
            if (ensure_pipeline(&pipe, gpu, sinfo.view_format)) {
                encode_frame(gpu, &pipe, &f, !winfo.server_decorated);
            }
            m0_draw_stats_add(&stats, ax_time_monotonic_ns() - t0);
            (void)ax_gpu_surface_present(surface, &f);
        }
        ax_gpu_surface_wait(surface, true, 100u * 1000000u, &reason);
        draw = reason != AX_WAKE_TIMEOUT;

        if (s->opt.stats && ax_time_monotonic_ns() >= next_stats) {
            char extra[512];
            gpu_extra_stats(surface, extra, sizeof extra);
            m0_print_stats(s, &stats, extra);
            next_stats += 1000000000u;
        }
    }
    if (s->opt.stats) {
        m0_print_totals();
        printf("gpu: uncaptured errors %u | device generation %u\n",
               ax_gpu_uncaptured_error_count(gpu), ax_gpu_device_generation(gpu));
    }
    release_pipeline(&pipe);
    ax_gpu_surface_destroy(surface);
    ax_gpu_destroy(gpu);
    return 0;
}
