/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window, Render IR renderer (--renderer=ir): the document's thread split in small.
 *
 *   render thread  this file's m0_ir_draw_thread: owns ax_render_wgpu and the surface; each frame
 *                  it takes the newest snapshot and presents it (ax_render_frame_step), then asks
 *                  the scene for the next frame (time and size)
 *   scene thread   records the Render IR for each request (scene.c) and publishes it through the
 *                  mailbox; it never waits for the render thread, only for requests
 *
 * The main thread pumps events and the app thread publishes hit regions, as for shm.c.
 */
#include <stdio.h>
#include <string.h>

#include "axion/ax_base.h"
#include "axion/base/ax_thread.h"
#include "axion/render/ax_ir_mailbox.h"
#include "axion/render/ax_render_frame.h"
#include "axion/render/ax_render_wgpu_surface.h"
#include "m0_scene.h"

typedef struct scene_box {
    shared *s;
    ax_ir_mailbox mailbox;
    ax_mutex mutex;
    ax_cond cond;
    ax_scene_request request; /* the newest; older ones are replaced, mailbox style */
    uint64_t requests;
    bool stop;
    uint64_t recorded;
    uint64_t record_ns_max;
} scene_box;

/* Render thread: hands the request to the scene thread. Holds the mutex only to copy. */
static void on_request(void *ctx, const ax_scene_request *q)
{
    scene_box *box = (scene_box *)ctx;
    ax_mutex_lock(&box->mutex);
    box->request = *q;
    box->requests++;
    ax_cond_signal(&box->cond);
    ax_mutex_unlock(&box->mutex);
}

static int scene_thread(void *arg)
{
    scene_box *box = (scene_box *)arg;
    uint64_t seen = 0;
    for (;;) {
        ax_scene_request q;
        ax_window_info info;
        ax_ir_builder *b;
        uint64_t t0;
        ax_mutex_lock(&box->mutex);
        while (box->requests == seen && !box->stop) {
            ax_cond_wait_timeout(&box->cond, &box->mutex, 100u * 1000000u);
        }
        if (box->stop) {
            ax_mutex_unlock(&box->mutex);
            break;
        }
        q = box->request;
        seen = box->requests;
        ax_mutex_unlock(&box->mutex);

        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        ax_window_get_info(box->s->window, &info);
        t0 = ax_time_monotonic_ns();
        b = ax_ir_mailbox_begin(&box->mailbox, q.logical_width, q.logical_height, q.frame_time_ns,
                                ax_color_from_srgb8(M0_COLOR_BG));
        m0_scene_record(b, q.logical_width, q.logical_height, (double)q.frame_time_ns / 1e9,
                        !info.server_decorated);
        if (ax_ir_mailbox_publish(&box->mailbox) != AX_OK) {
            fprintf(stderr, "m0_window: scene: recording failed\n");
            ax_atomic_store_u32(&box->s->draw_failed, 1, AX_MO_RELEASE);
            break;
        }
        box->recorded++;
        if (ax_time_monotonic_ns() - t0 > box->record_ns_max) {
            box->record_ns_max = ax_time_monotonic_ns() - t0;
        }
    }
    return 0;
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

/* The " | gpu: ..." part, parsed by tools/diag/measure_m0.py, then the frame loop's counters. */
static void extra_stats(ax_gpu_surface *surface, ax_render_frame *rf, char *buf, size_t cap)
{
    ax_gpu_surface_stats st;
    ax_render_frame_stats fs;
    uint64_t n;
    int len;
    st.struct_size = sizeof st;
    ax_gpu_surface_get_stats(surface, &st, true);
    fs.struct_size = sizeof fs;
    ax_render_frame_get_stats(rf, &fs, true);
    n = st.frames + st.failed_presents;
    len = snprintf(
        buf, cap,
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
    if (len > 0 && (size_t)len < cap) {
        snprintf(buf + len, cap - (size_t)len,
                 " | ir: cleared %llu | scene late %llu | forced %llu | hidden %llu | wait %.2f ms "
                 "max | instances %u | unknown %u",
                 (unsigned long long)fs.cleared, (unsigned long long)fs.scene_late,
                 (unsigned long long)fs.forced_acks, (unsigned long long)fs.hidden_acks,
                 (double)fs.wait_ns_max / 1e6,
                 fs.presented != 0 ? (unsigned)(fs.counts.instances / fs.presented) : 0u,
                 fs.counts.unknown_items);
    }
}

int m0_ir_draw_thread(void *arg)
{
    shared *s = (shared *)arg;
    static scene_box box;
    const ax_renderer_vtbl *vt = ax_render_wgpu_vtbl();
    ax_render_wgpu_desc wd;
    ax_renderer_desc rd;
    ax_render_frame_desc fd;
    ax_render_frame rf;
    ax_thread scene_t;
    ax_render_wgpu *r = NULL;
    void *self = NULL;
    m0_draw_stats stats;
    uint64_t next_stats = ax_time_monotonic_ns() + 1000000000u;
    bool reported_start = false;
    ax_result res;

    memset(&box, 0, sizeof box);
    box.s = s;
    ax_ir_mailbox_init(&box.mailbox, ax_default_allocator());
    ax_mutex_init(&box.mutex);
    ax_cond_init(&box.cond);
    memset(&stats, 0, sizeof stats);

    memset(&wd, 0, sizeof wd);
    wd.chain.type = AX_RENDERER_CHAIN_WGPU;
    wd.adapter = (ax_gpu_adapter_kind)s->opt.adapter;
    wd.pacing = s->opt.pacing;
    wd.present_mode = s->opt.present_mode;
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.next = &wd.chain;
    rd.initial_background = ax_color_from_srgb8(M0_COLOR_BG);
    res = vt->create(&rd, &self);
    if (res == AX_OK) {
        r = (ax_render_wgpu *)self;
        res = vt->attach_surface(self, s->surface);
    }
    if (res != AX_OK || ax_thread_create(&scene_t, scene_thread, &box) != AX_OK) {
        fprintf(stderr, "m0_window: ir: %s: %.*s\n", ax_result_name(res),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        if (self != NULL) {
            vt->destroy(self);
        }
        ax_atomic_store_u32(&s->draw_failed, 1, AX_MO_RELEASE);
        return 1;
    }
    {
        ax_gpu_info info;
        ax_gpu_surface_info sinfo;
        info.struct_size = sizeof info;
        ax_gpu_get_info(ax_render_wgpu_gpu(r), &info);
        sinfo.struct_size = sizeof sinfo;
        ax_gpu_surface_get_info(ax_render_wgpu_gpu_surface(r, s->surface), &sinfo);
        printf("ir: ax_render_wgpu on %s | %s | %s | pacing %s | present mode %s%s\n",
               info.implementation, info.adapter_name, info.driver,
               sinfo.pacing == AX_GPU_PACING_DRIVER ? "driver" : "compositor",
               present_mode_name(sinfo.effective_present_mode),
               sinfo.present_mode_supported ? "" : " (fallback)");
        fflush(stdout);
    }

    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.vtbl = vt;
    fd.renderer = self;
    fd.surface = s->surface;
    fd.window = s->window;
    fd.mailbox = &box.mailbox;
    fd.request = on_request;
    fd.request_ctx = &box;
    fd.max_wait_ns = s->opt.max_wait_ns;
    ax_render_frame_init(&rf, &fd);

    while (!m0_stopping(s)) {
        res = ax_render_frame_step(&rf, true, 100u * 1000000u, NULL);
        if (res != AX_OK && res != AX_ERR_DEVICE_LOST) {
            fprintf(stderr, "m0_window: ir: frame: %s: %.*s\n", ax_result_name(res),
                    (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
            ax_atomic_store_u32(&s->draw_failed, 1, AX_MO_RELEASE);
            break;
        }
        if (!reported_start) {
            ax_render_wgpu_surface_stats ss;
            ss.struct_size = sizeof ss;
            ax_render_wgpu_get_surface_stats(r, s->surface, &ss);
            if (ss.attach_to_first_content_ns != 0) {
                ax_render_wgpu_stats rs;
                rs.struct_size = sizeof rs;
                ax_render_wgpu_get_stats(r, &rs, false);
                printf("ir: first frame %.1f ms, first content %.1f ms after attach (pipeline "
                       "ready in %.1f ms)\n",
                       (double)ss.attach_to_first_frame_ns / 1e6,
                       (double)ss.attach_to_first_content_ns / 1e6,
                       (double)rs.pipeline_ready_ns / 1e6);
                fflush(stdout);
                reported_start = true;
            }
        }
        if (s->opt.stats && ax_time_monotonic_ns() >= next_stats) {
            char extra[768];
            ax_render_wgpu_stats rs;
            rs.struct_size = sizeof rs;
            ax_render_wgpu_get_stats(r, &rs, true);
            stats.frames = rs.frames;
            stats.sum_ns = rs.encode_ns_total;
            stats.max_ns = rs.encode_ns_max;
            extra_stats(ax_render_wgpu_gpu_surface(r, s->surface), &rf, extra, sizeof extra);
            m0_print_stats(s, &stats, extra);
            next_stats += 1000000000u;
        }
    }

    ax_mutex_lock(&box.mutex);
    box.stop = true;
    ax_cond_signal(&box.cond);
    ax_mutex_unlock(&box.mutex);
    ax_thread_join(&scene_t);
    if (s->opt.stats) {
        m0_print_totals();
        printf("ir: snapshots recorded %llu (%.2f ms max) | uncaptured errors %u | device "
               "generation %u\n",
               (unsigned long long)box.recorded, (double)box.record_ns_max / 1e6,
               ax_gpu_uncaptured_error_count(ax_render_wgpu_gpu(r)),
               ax_gpu_device_generation(ax_render_wgpu_gpu(r)));
    }
    vt->detach_surface(self, s->surface);
    vt->destroy(self);
    ax_cond_deinit(&box.cond);
    ax_mutex_deinit(&box.mutex);
    ax_ir_mailbox_deinit(&box.mailbox);
    return 0;
}
