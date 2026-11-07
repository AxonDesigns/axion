/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window: one window, the document's threads in small.
 *
 *   main thread    runs the platform event pump (ax_app_run) and nothing else
 *   app thread     consumes events and publishes the window's hit regions
 *   draw threads   --renderer=shm (shm.c): one presenter thread painting shared-memory buffers: a
 *                  background, a title band (when the compositor does not draw one), and a bar
 *                  that moves at a constant speed in logical units, timed by the frame clock's
 *                  predicted present time, so a dropped or late frame shows as a visible hitch.
 *                  --renderer=ir (ir.c, scene.c): a scene thread records the Render IR of each
 *                  frame and publishes it; the render thread presents the newest snapshot with
 *                  ax_render_wgpu.
 *
 * Usage: m0_window [--renderer=shm|ir] [--decorations=native|none] [--stats]
 *                  [--duration=SECONDS] [--width=W] [--height=H]
 *                  [--pacing=default|compositor|driver]                         (ir)
 *                  [--present-mode=default|fifo|fifo-relaxed|mailbox|immediate] (ir)
 *                  [--adapter=default|software] [--max-wait-ms=MS]              (ir)
 *
 * --stats prints presentation statistics once a second: mean interval between presented frames,
 * presented/missed/discarded/lost counts, configure -> on-screen latency and draw time; the ir
 * renderer adds swapchain resizes, acquire and present times, deferred acknowledgements and the
 * size handshake's counters.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_base.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_hit_regions.h"
#include "m0_window.h"

#if defined(M0_HAVE_IR)
#    include "axion/gpu/ax_gpu_surface.h"
#endif

/* ---- options ------------------------------------------------------------------------------------
 */

static const char usage[] =
    "usage: %s [--renderer=shm|ir] [--decorations=native|none] [--stats] [--duration=SECONDS]\n"
    "          [--width=W] [--height=H] [--pacing=default|compositor|driver]\n"
    "          [--present-mode=default|fifo|fifo-relaxed|mailbox|immediate]\n"
    "          [--adapter=default|software] [--max-wait-ms=MS]\n";

static int parse_options(int argc, char **argv, options *o)
{
    int i;
    memset(o, 0, sizeof *o);
    o->decorations = AX_DECORATIONS_NATIVE;
    o->width = 800;
    o->height = 500;
    o->renderer = M0_RENDERER_SHM;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--decorations=native") == 0) {
            o->decorations = AX_DECORATIONS_NATIVE;
        } else if (strcmp(a, "--decorations=none") == 0) {
            o->decorations = AX_DECORATIONS_NONE;
        } else if (strcmp(a, "--stats") == 0) {
            o->stats = true;
        } else if (strncmp(a, "--duration=", 11) == 0) {
            o->duration = atof(a + 11);
        } else if (strncmp(a, "--width=", 8) == 0) {
            o->width = (float)atof(a + 8);
        } else if (strncmp(a, "--height=", 9) == 0) {
            o->height = (float)atof(a + 9);
        } else if (strcmp(a, "--renderer=shm") == 0) {
            o->renderer = M0_RENDERER_SHM;
        } else if (strcmp(a, "--renderer=ir") == 0) {
#if defined(M0_HAVE_IR)
            o->renderer = M0_RENDERER_IR;
#else
            fprintf(stderr, "%s: built without the gpu module\n", argv[0]);
            return -1;
#endif
#if defined(M0_HAVE_IR)
        } else if (strncmp(a, "--max-wait-ms=", 14) == 0) {
            o->max_wait_ns = (uint64_t)(atof(a + 14) * 1e6);
        } else if (strcmp(a, "--pacing=default") == 0) {
            o->pacing = AX_GPU_PACING_DEFAULT;
        } else if (strcmp(a, "--pacing=compositor") == 0) {
            o->pacing = AX_GPU_PACING_COMPOSITOR;
        } else if (strcmp(a, "--pacing=driver") == 0) {
            o->pacing = AX_GPU_PACING_DRIVER;
        } else if (strcmp(a, "--present-mode=default") == 0) {
            o->present_mode = WGPUPresentMode_Undefined;
        } else if (strcmp(a, "--present-mode=fifo") == 0) {
            o->present_mode = WGPUPresentMode_Fifo;
        } else if (strcmp(a, "--present-mode=fifo-relaxed") == 0) {
            o->present_mode = WGPUPresentMode_FifoRelaxed;
        } else if (strcmp(a, "--present-mode=mailbox") == 0) {
            o->present_mode = WGPUPresentMode_Mailbox;
        } else if (strcmp(a, "--present-mode=immediate") == 0) {
            o->present_mode = WGPUPresentMode_Immediate;
        } else if (strcmp(a, "--adapter=default") == 0) {
            o->adapter = AX_GPU_ADAPTER_DEFAULT;
        } else if (strcmp(a, "--adapter=software") == 0) {
            o->adapter = AX_GPU_ADAPTER_SOFTWARE;
#endif
        } else {
            fprintf(stderr, usage, argv[0]);
            return -1;
        }
    }
    return 0;
}

/* ---- shared by the renderers -------------------------------------------------------------------
 */

void m0_layout_frame(const ax_frame_target *t, bool csd, m0_layout *out)
{
    const double pos =
        fmod((double)t->predicted_present_ns / 1e9 * M0_BAR_SPEED, M0_BAR_TRACK) - 24.0;
    int32_t edge = (int32_t)lroundf(t->scale);
    out->caption_px = csd ? (int32_t)lroundf(M0_CAPTION_HEIGHT * t->scale) : 0;
    out->edge_px = edge < 1 ? 1 : edge;
    out->bar_x0 = (int32_t)lround(pos * (double)t->scale);
    out->bar_x1 = out->bar_x0 + (int32_t)lroundf(24.0f * t->scale);
    out->bar_bottom_px = csd ? out->edge_px : 0;
}

void m0_draw_stats_add(m0_draw_stats *d, uint64_t ns)
{
    d->frames++;
    d->sum_ns += ns;
    if (ns > d->max_ns) {
        d->max_ns = ns;
    }
}

static ax_present_stats g_total; /* written by the presenter thread only */

void m0_print_stats(shared *s, m0_draw_stats *draw, const char *extra)
{
    ax_present_stats st;
    double interval_ms;
    st.struct_size = sizeof st;
    if (ax_surface_get_present_stats(s->surface, &st, true) != AX_OK) {
        return;
    }
    interval_ms = (double)st.mean_interval_ns / 1e6;
    printf("stats: interval %.3f ms (%.1f Hz) | refresh %.3f ms | drawn %llu | presented %llu | "
           "missed %llu | discarded %llu | lost %llu | configure->present %.2f ms mean, %.2f ms "
           "max (%llu) | draw %.2f ms mean, %.2f ms max%s%s\n",
           interval_ms, interval_ms > 0.0 ? 1000.0 / interval_ms : 0.0,
           (double)st.refresh_interval_ns / 1e6, (unsigned long long)draw->frames,
           (unsigned long long)st.presented, (unsigned long long)st.missed_vblanks,
           (unsigned long long)st.discarded, (unsigned long long)st.lost_frames,
           (double)st.mean_latency_ns / 1e6, (double)st.max_latency_ns / 1e6,
           (unsigned long long)st.latency_samples,
           draw->frames != 0 ? (double)draw->sum_ns / (double)draw->frames / 1e6 : 0.0,
           (double)draw->max_ns / 1e6, extra != NULL ? " | " : "", extra != NULL ? extra : "");
    fflush(stdout);
    g_total.presented += st.presented;
    g_total.missed_vblanks += st.missed_vblanks;
    g_total.discarded += st.discarded;
    g_total.lost_frames += st.lost_frames;
    memset(draw, 0, sizeof *draw);
}

void m0_print_totals(void)
{
    printf("total: presented %llu | missed %llu | discarded %llu | lost %llu\n",
           (unsigned long long)g_total.presented, (unsigned long long)g_total.missed_vblanks,
           (unsigned long long)g_total.discarded, (unsigned long long)g_total.lost_frames);
}

/* ---- app thread: events and hit regions --------------------------------------------------------
 */

static void publish_regions(shared *s, float w, float h)
{
    ax_window_info info;
    ax_hit_region r[9];
    uint32_t n = 0;

    info.struct_size = sizeof info;
    if (ax_window_get_info(s->window, &info) != AX_OK || info.server_decorated) {
        ax_window_set_hit_regions(s->window, NULL, 0); /* the compositor's frame does it */
        return;
    }
    /* Later regions win: the caption first, then edges, then corners on top. */
    r[n++] = (ax_hit_region){0, 0, w, M0_CAPTION_HEIGHT, AX_REGION_CAPTION};
    r[n++] = (ax_hit_region){0, 0, w, M0_BORDER, AX_REGION_RESIZE_TOP};
    r[n++] = (ax_hit_region){0, h - M0_BORDER, w, M0_BORDER, AX_REGION_RESIZE_BOTTOM};
    r[n++] = (ax_hit_region){0, 0, M0_BORDER, h, AX_REGION_RESIZE_LEFT};
    r[n++] = (ax_hit_region){w - M0_BORDER, 0, M0_BORDER, h, AX_REGION_RESIZE_RIGHT};
    r[n++] = (ax_hit_region){0, 0, M0_CORNER, M0_CORNER, AX_REGION_RESIZE_TOP_LEFT};
    r[n++] = (ax_hit_region){w - M0_CORNER, 0, M0_CORNER, M0_CORNER, AX_REGION_RESIZE_TOP_RIGHT};
    r[n++] = (ax_hit_region){0, h - M0_CORNER, M0_CORNER, M0_CORNER, AX_REGION_RESIZE_BOTTOM_LEFT};
    r[n++] = (ax_hit_region){w - M0_CORNER, h - M0_CORNER, M0_CORNER, M0_CORNER,
                             AX_REGION_RESIZE_BOTTOM_RIGHT};
    ax_window_set_hit_regions(s->window, r, n);
}

static int app_thread(void *arg)
{
    shared *s = (shared *)arg;
    while (!m0_stopping(s)) {
        ax_event e;
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        ax_app_wait_event(s->app, &e, 50u * 1000000u);
        switch (e.type) {
        case AX_EVENT_RESIZED:
            publish_regions(s, e.data.resized.width, e.data.resized.height);
            break;
        case AX_EVENT_CLOSE_REQUESTED:
            printf("close requested\n");
            ax_atomic_store_u32(&s->stop, 1, AX_MO_RELEASE);
            break;
        case AX_EVENT_SCALE_CHANGED:
            printf("scale changed: %.3f\n", (double)e.data.scale_changed.scale);
            break;
        case AX_EVENT_FOCUS:
            printf("focus: %s\n", e.data.focus.focused ? "gained" : "lost");
            break;
        case AX_EVENT_OCCLUSION:
            printf("occlusion: %s\n", e.data.occlusion.occluded ? "hidden" : "visible");
            break;
        default:
            break;
        }
        if (s->opt.duration > 0.0 &&
            (double)(ax_time_monotonic_ns() - s->start_ns) / 1e9 >= s->opt.duration) {
            ax_atomic_store_u32(&s->stop, 1, AX_MO_RELEASE);
        }
        if (ax_atomic_load_u32(&s->draw_failed, AX_MO_ACQUIRE) != 0) {
            ax_atomic_store_u32(&s->stop, 1, AX_MO_RELEASE);
        }
    }
    ax_app_quit(s->app);
    return 0;
}

/* ---- draw thread: shared-memory buffers --------------------------------------------------------
 */

/* ---- main thread -------------------------------------------------------------------------------
 */

int main(int argc, char **argv)
{
    static shared s;
    ax_app_desc app_desc;
    ax_window_desc win_desc;
    ax_thread app_t, draw_t;
    uint64_t caps;
    ax_result r;

    memset(&s, 0, sizeof s);
    if (parse_options(argc, argv, &s.opt) != 0) {
        return 2;
    }
    ax_atomic_init_u32(&s.stop, 0);
    ax_atomic_init_u32(&s.draw_failed, 0);

    memset(&app_desc, 0, sizeof app_desc);
    app_desc.struct_size = sizeof app_desc;
    app_desc.allocator = ax_default_allocator();
    app_desc.app_id = AX_STR("net.axonworks.axion.m0_window");
    r = ax_app_create(&app_desc, &s.app);
    if (r != AX_OK) {
        fprintf(stderr, "m0_window: %s: %.*s\n", ax_result_name(r),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        return 1;
    }
    caps = ax_app_caps(s.app);
    printf("AXION %u.%u.%u, Wayland | server decorations %s | fractional scale %s | presentation "
           "timing %s | cursor shapes %s\n",
           AX_VERSION_GET_MAJOR(ax_version()), AX_VERSION_GET_MINOR(ax_version()),
           AX_VERSION_GET_PATCH(ax_version()),
           (caps & AX_CAP_WINDOW_SERVER_DECORATIONS) ? "yes" : "no",
           (caps & AX_CAP_FRACTIONAL_SCALE) ? "yes" : "no",
           (caps & AX_CAP_PRESENTATION_TIMING) ? "yes" : "no",
           (caps & AX_CAP_CURSOR_SHAPES) ? "yes" : "no");

    memset(&win_desc, 0, sizeof win_desc);
    win_desc.struct_size = sizeof win_desc;
    win_desc.title = AX_STR("AXION m0_window");
    win_desc.width = s.opt.width;
    win_desc.height = s.opt.height;
    win_desc.min_width = 240;
    win_desc.min_height = 160;
    win_desc.decorations = s.opt.decorations;
    win_desc.flags = AX_WINDOW_RESIZABLE;
    r = ax_window_create(s.app, &win_desc, &s.window);
    if (r != AX_OK || ax_window_get_surface(s.window, &s.surface) != AX_OK) {
        fprintf(stderr, "m0_window: %s: %.*s\n", ax_result_name(r),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        ax_app_destroy(s.app);
        return 1;
    }

    s.start_ns = ax_time_monotonic_ns();
    if (ax_thread_create(&draw_t,
#if defined(M0_HAVE_IR)
                         s.opt.renderer == M0_RENDERER_IR ? m0_ir_draw_thread :
#endif
                                                          m0_shm_draw_thread,
                         &s) != AX_OK ||
        ax_thread_create(&app_t, app_thread, &s) != AX_OK) {
        fprintf(stderr, "m0_window: could not start threads\n");
        return 1;
    }

    r = ax_app_run(s.app); /* until the app thread calls ax_app_quit */
    ax_atomic_store_u32(&s.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&app_t);
    ax_thread_join(&draw_t);
    if (r != AX_OK) {
        fprintf(stderr, "m0_window: %s: %.*s\n", ax_result_name(r),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
    }
    ax_window_destroy(s.window);
    ax_app_destroy(s.app);
    return (r == AX_OK && ax_atomic_load_u32(&s.draw_failed, AX_MO_ACQUIRE) == 0) ? 0 : 1;
}
