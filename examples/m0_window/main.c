/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window: one window, three threads, no GPU.
 *
 *   main thread    runs the platform event pump (ax_app_run) and nothing else
 *   app thread     consumes events and publishes the window's hit regions
 *   draw thread    presents with shared-memory buffers: a background, a title band (when the
 *                  compositor does not draw one), and a bar that moves at a constant speed in
 *                  logical units, timed by the frame clock's predicted present time, so a dropped
 *                  or late frame shows as a visible hitch
 *
 * Usage: m0_window [--decorations=native|none] [--stats] [--duration=SECONDS]
 *                  [--width=W] [--height=H]
 *
 * --stats prints presentation statistics once a second: mean interval between presented
 * frames, presented/missed/discarded counts and configure -> on-screen latency.
 *
 * wl_shm is used here only because the GPU layer is the next step; platform/ never uses it.
 */
#define _GNU_SOURCE /* memfd_create */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "axion/ax_base.h"
#include "axion/ax_platform.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_hit_regions.h"
#include "axion/platform/ax_present.h"
#include "axion/platform/ax_surface_native.h"

/* ---- options ------------------------------------------------------------------------------------
 */

typedef struct options {
    uint32_t decorations;
    bool stats;
    double duration; /* seconds; 0 = until closed */
    float width, height;
} options;

static int parse_options(int argc, char **argv, options *o)
{
    int i;
    o->decorations = AX_DECORATIONS_NATIVE;
    o->stats = false;
    o->duration = 0.0;
    o->width = 800;
    o->height = 500;
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
        } else {
            fprintf(stderr,
                    "usage: %s [--decorations=native|none] [--stats] [--duration=SECONDS]\n"
                    "          [--width=W] [--height=H]\n",
                    argv[0]);
            return -1;
        }
    }
    return 0;
}

/* ---- shared state -------------------------------------------------------------------------------
 */

#define CAPTION_HEIGHT 32.0f /* logical units */
#define BORDER         8.0f
#define CORNER         16.0f

typedef struct shared {
    options opt;
    ax_app app;
    ax_window window;
    ax_surface surface;
    ax_atomic_u32 stop;
    ax_atomic_u32 draw_failed;
    uint64_t start_ns;
} shared;

static bool stopping(shared *s)
{
    return ax_atomic_load_u32(&s->stop, AX_MO_ACQUIRE) != 0;
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
    r[n++] = (ax_hit_region){0, 0, w, CAPTION_HEIGHT, AX_REGION_CAPTION};
    r[n++] = (ax_hit_region){0, 0, w, BORDER, AX_REGION_RESIZE_TOP};
    r[n++] = (ax_hit_region){0, h - BORDER, w, BORDER, AX_REGION_RESIZE_BOTTOM};
    r[n++] = (ax_hit_region){0, 0, BORDER, h, AX_REGION_RESIZE_LEFT};
    r[n++] = (ax_hit_region){w - BORDER, 0, BORDER, h, AX_REGION_RESIZE_RIGHT};
    r[n++] = (ax_hit_region){0, 0, CORNER, CORNER, AX_REGION_RESIZE_TOP_LEFT};
    r[n++] = (ax_hit_region){w - CORNER, 0, CORNER, CORNER, AX_REGION_RESIZE_TOP_RIGHT};
    r[n++] = (ax_hit_region){0, h - CORNER, CORNER, CORNER, AX_REGION_RESIZE_BOTTOM_LEFT};
    r[n++] = (ax_hit_region){w - CORNER, h - CORNER, CORNER, CORNER, AX_REGION_RESIZE_BOTTOM_RIGHT};
    ax_window_set_hit_regions(s->window, r, n);
}

static int app_thread(void *arg)
{
    shared *s = (shared *)arg;
    while (!stopping(s)) {
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

#define BUFFERS 4

/* A buffer slot keeps its shared-memory pool across resizes: a new size only needs a new
 * wl_buffer over the same, already mapped memory (and growing the pool when it is too small), so an
 * interactive resize does not pay for memfd creation, mapping and fresh-page faults every frame. */
typedef struct buffer {
    int fd;
    struct wl_shm_pool *pool;
    uint32_t *pixels;
    size_t capacity; /* bytes mapped */

    struct wl_buffer *wl; /* current buffer over the pool, or NULL */
    uint32_t width, height;
    bool busy;      /* attached and not yet released by the compositor */
    bool fresh;     /* never drawn into at this size */
    int32_t bar_x0; /* bar position drawn into this buffer last time (pixels) */
    int32_t bar_x1;
} buffer;

typedef struct drawer {
    shared *s;
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct wl_surface *surface;
    struct wl_display *display_wrapper;
    struct wl_registry *registry;
    struct wl_shm *shm;
    buffer buffers[BUFFERS];
    int32_t prev_bar_x0, prev_bar_x1; /* bar in the last committed frame */
    uint32_t frame;
} drawer;

static void registry_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface,
                            uint32_t version)
{
    drawer *d = (drawer *)data;
    (void)version;
    if (strcmp(iface, wl_shm_interface.name) == 0 && d->shm == NULL) {
        d->shm = (struct wl_shm *)wl_registry_bind(reg, name, &wl_shm_interface, 1);
    }
}

static void registry_remove(void *data, struct wl_registry *reg, uint32_t name)
{
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

static void free_slot(buffer *b)
{
    if (b->wl != NULL) {
        wl_buffer_destroy(b->wl);
    }
    if (b->pool != NULL) {
        wl_shm_pool_destroy(b->pool);
    }
    if (b->pixels != NULL) {
        munmap(b->pixels, b->capacity);
    }
    if (b->fd > 0) {
        close(b->fd);
    }
    memset(b, 0, sizeof *b);
}

static void buffer_release(void *data, struct wl_buffer *wl)
{
    buffer *b = (buffer *)data;
    (void)wl;
    b->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {buffer_release};

/* Makes the slot's pool hold at least `bytes`, creating or growing it (pools only grow). */
static bool reserve_pool(drawer *d, buffer *b, size_t bytes)
{
    size_t capacity;
    if (b->pool != NULL && b->capacity >= bytes) {
        return true;
    }
    capacity = bytes + bytes / 4; /* headroom: a growing resize does not regrow every frame */
    if (b->pool == NULL) {
        b->fd = memfd_create("m0_window", MFD_CLOEXEC);
        if (b->fd < 0) {
            b->fd = 0;
            return false;
        }
    }
    if (ftruncate(b->fd, (off_t)capacity) != 0) {
        return false;
    }
    if (b->pixels != NULL) {
        munmap(b->pixels, b->capacity);
    }
    b->pixels = (uint32_t *)mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, b->fd, 0);
    if (b->pixels == MAP_FAILED) {
        b->pixels = NULL;
        return false;
    }
    if (b->pool == NULL) {
        b->pool = wl_shm_create_pool(d->shm, b->fd, (int32_t)capacity);
    } else {
        wl_shm_pool_resize(b->pool, (int32_t)capacity);
    }
    b->capacity = capacity;
    return true;
}

/* A free buffer of the right size, re-creating a slot's wl_buffer when the size changed; NULL if
 * the compositor holds every slot. */
static buffer *acquire_buffer(drawer *d, uint32_t w, uint32_t h)
{
    int i;
    buffer *b = NULL;
    for (i = 0; i < BUFFERS; ++i) { /* prefer a free slot that already has this size */
        if (!d->buffers[i].busy && d->buffers[i].wl != NULL && d->buffers[i].width == w &&
            d->buffers[i].height == h) {
            return &d->buffers[i];
        }
    }
    for (i = 0; i < BUFFERS && b == NULL; ++i) {
        if (!d->buffers[i].busy) {
            b = &d->buffers[i];
        }
    }
    if (b == NULL) {
        return NULL;
    }
    if (b->wl != NULL) {
        wl_buffer_destroy(b->wl);
        b->wl = NULL;
    }
    if (!reserve_pool(d, b, (size_t)w * h * 4u)) {
        return NULL;
    }
    b->wl = wl_shm_pool_create_buffer(b->pool, 0, (int32_t)w, (int32_t)h, (int32_t)(w * 4u),
                                      WL_SHM_FORMAT_XRGB8888);
    wl_buffer_add_listener(b->wl, &buffer_listener, b);
    b->width = w;
    b->height = h;
    b->fresh = true;
    return b;
}

static void fill_rect(buffer *b, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
    int32_t x, y;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int32_t)b->width)
        x1 = (int32_t)b->width;
    if (y1 > (int32_t)b->height)
        y1 = (int32_t)b->height;
    for (y = y0; y < y1; ++y) {
        uint32_t *row = b->pixels + (size_t)y * b->width;
        for (x = x0; x < x1; ++x) {
            row[x] = color;
        }
    }
}

#define BAR_SPEED 400.0  /* logical units per second */
#define BAR_TRACK 1200.0 /* logical units per lap, whatever the window size */

#define COLOR_BG      0xFF1E2129u
#define COLOR_CAPTION 0xFF2D3240u
#define COLOR_EDGE    0xFF4F7CFFu
#define COLOR_BAR     0xFFE8ECF4u

/* Paints the static parts: background, and the title band and edge when we draw the frame. */
static void draw_background(buffer *b, int32_t x0, int32_t x1, int32_t caption_px, int32_t edge_px)
{
    fill_rect(b, x0, 0, x1, (int32_t)b->height, COLOR_BG);
    if (caption_px > 0) {
        fill_rect(b, x0, 0, x1, caption_px, COLOR_CAPTION);
        fill_rect(b, x0, 0, x1, edge_px, COLOR_EDGE);
        fill_rect(b, x0, (int32_t)b->height - edge_px, x1, (int32_t)b->height, COLOR_EDGE);
        if (x0 < edge_px) {
            fill_rect(b, 0, 0, edge_px, (int32_t)b->height, COLOR_EDGE);
        }
        if (x1 > (int32_t)b->width - edge_px) {
            fill_rect(b, (int32_t)b->width - edge_px, 0, (int32_t)b->width, (int32_t)b->height,
                      COLOR_EDGE);
        }
    }
}

static bool draw_frame(drawer *d, const ax_frame_target *t, bool csd)
{
    const uint32_t w = t->pixel_width;
    const uint32_t h = t->pixel_height;
    const int32_t caption_px = csd ? (int32_t)lroundf(CAPTION_HEIGHT * t->scale) : 0;
    const int32_t edge_px = (int32_t)lroundf(1.0f * t->scale) < 1 ? 1 : (int32_t)lroundf(t->scale);
    const int32_t bar_w = (int32_t)lroundf(24.0f * t->scale);
    /* The bar moves at BAR_SPEED logical units per second along a track of fixed length
     * (BAR_TRACK), timed by the predicted present time. Its position depends only on time, never on
     * the window's size or scale, so resizing never makes it jump: a narrow window shows part of
     * the track, a wide one sees it start over early. Any dropped or late frame shows as a hitch.
     */
    const double pos = fmod((double)t->predicted_present_ns / 1e9 * BAR_SPEED, BAR_TRACK) - 24.0;
    const int32_t bar_x0 = (int32_t)lround(pos * (double)t->scale);
    const int32_t bar_x1 = bar_x0 + bar_w;
    buffer *b = acquire_buffer(d, w, h);
    int32_t dx0, dx1;

    if (b == NULL) {
        return false;
    }
    if (b->fresh) {
        draw_background(b, 0, (int32_t)w, caption_px, edge_px);
        b->fresh = false;
    } else {
        draw_background(b, b->bar_x0, b->bar_x1, caption_px, edge_px); /* erase its old bar */
    }
    fill_rect(b, bar_x0, caption_px, bar_x1, (int32_t)h - (csd ? edge_px : 0), COLOR_BAR);
    b->bar_x0 = bar_x0;
    b->bar_x1 = bar_x1;

    if (ax_surface_prepare_commit(d->s->surface, t) != AX_OK) {
        return false;
    }
    wl_surface_attach(d->surface, b->wl, 0, 0);
    /* Damage is relative to the previously committed frame: its bar and this one. A frame that
     * acknowledges a configure has a new size, so all of it is new. */
    if (t->acks_configure || d->frame == 0) {
        wl_surface_damage_buffer(d->surface, 0, 0, (int32_t)w, (int32_t)h);
    } else {
        dx0 = bar_x0 < d->prev_bar_x0 ? bar_x0 : d->prev_bar_x0;
        dx1 = bar_x1 > d->prev_bar_x1 ? bar_x1 : d->prev_bar_x1;
        if (dx1 - dx0 > (int32_t)w / 2) { /* wrapped around: two strips */
            wl_surface_damage_buffer(d->surface, bar_x0, 0, bar_w, (int32_t)h);
            wl_surface_damage_buffer(d->surface, d->prev_bar_x0, 0, bar_w, (int32_t)h);
        } else {
            wl_surface_damage_buffer(d->surface, dx0, 0, dx1 - dx0, (int32_t)h);
        }
    }
    wl_surface_commit(d->surface);
    wl_display_flush(d->display);
    b->busy = true;
    d->prev_bar_x0 = bar_x0;
    d->prev_bar_x1 = bar_x1;
    d->frame++;
    return true;
}

static void print_stats(const ax_present_stats *st, uint64_t frames_drawn, uint64_t draw_sum_ns,
                        uint64_t draw_max_ns)
{
    double interval_ms = (double)st->mean_interval_ns / 1e6;
    printf("stats: interval %.3f ms (%.1f Hz) | refresh %.3f ms | drawn %llu | presented %llu | "
           "missed %llu | discarded %llu | configure->present %.2f ms mean, %.2f ms max (%llu) | "
           "draw %.2f ms mean, %.2f ms max\n",
           interval_ms, interval_ms > 0.0 ? 1000.0 / interval_ms : 0.0,
           (double)st->refresh_interval_ns / 1e6, (unsigned long long)frames_drawn,
           (unsigned long long)st->presented, (unsigned long long)st->missed_vblanks,
           (unsigned long long)st->discarded, (double)st->mean_latency_ns / 1e6,
           (double)st->max_latency_ns / 1e6, (unsigned long long)st->latency_samples,
           frames_drawn != 0 ? (double)draw_sum_ns / (double)frames_drawn / 1e6 : 0.0,
           (double)draw_max_ns / 1e6);
    fflush(stdout);
}

static int draw_thread(void *arg)
{
    shared *s = (shared *)arg;
    drawer d;
    ax_surface_native native;
    bool draw = true;
    uint64_t next_stats = ax_time_monotonic_ns() + 1000000000u;
    uint64_t drawn_since = 0;
    uint64_t draw_sum_ns = 0;
    uint64_t draw_max_ns = 0;
    ax_present_stats total;
    int i;

    memset(&d, 0, sizeof d);
    memset(&total, 0, sizeof total);
    d.s = s;
    memset(&native, 0, sizeof native);
    native.struct_size = sizeof native;
    if (ax_surface_get_native(s->surface, &native) != AX_OK || native.kind != AX_NATIVE_WAYLAND) {
        ax_atomic_store_u32(&s->draw_failed, 1, AX_MO_RELEASE);
        return 1;
    }
    d.display = (struct wl_display *)native.u.wayland.display;
    d.queue = (struct wl_event_queue *)native.u.wayland.queue;
    d.surface = (struct wl_surface *)native.u.wayland.surface;
    /* Bind wl_shm on the surface's presenter queue, so buffer releases arrive there. */
    d.display_wrapper = (struct wl_display *)wl_proxy_create_wrapper(d.display);
    wl_proxy_set_queue((struct wl_proxy *)d.display_wrapper, d.queue);
    d.registry = wl_display_get_registry(d.display_wrapper);
    wl_registry_add_listener(d.registry, &registry_listener, &d);
    if (wl_display_roundtrip_queue(d.display, d.queue) < 0 || d.shm == NULL) {
        fprintf(stderr, "m0_window: no wl_shm\n");
        ax_atomic_store_u32(&s->draw_failed, 1, AX_MO_RELEASE);
        return 1;
    }

    while (!stopping(s)) {
        ax_frame_target t;
        ax_wake_reason reason;
        memset(&t, 0, sizeof t);
        t.struct_size = sizeof t;
        ax_surface_begin_frame(s->surface, &t);
        if (t.ready && (draw || t.acks_configure || t.commit_requested)) {
            ax_window_info info;
            info.struct_size = sizeof info;
            ax_window_get_info(s->window, &info);
            uint64_t t0 = ax_time_monotonic_ns();
            if (draw_frame(&d, &t, !info.server_decorated)) {
                uint64_t dt = ax_time_monotonic_ns() - t0;
                drawn_since++;
                draw_sum_ns += dt;
                draw_max_ns = dt > draw_max_ns ? dt : draw_max_ns;
            }
        }
        ax_surface_wait_frame(s->surface, 100u * 1000000u, &reason);
        /* Animate: draw on every frame callback. On a timeout (hidden window: no callbacks) wait
         * again rather than piling up frames. */
        draw = reason != AX_WAKE_TIMEOUT;

        if (s->opt.stats && ax_time_monotonic_ns() >= next_stats) {
            ax_present_stats st;
            st.struct_size = sizeof st;
            ax_surface_get_present_stats(s->surface, &st, true);
            print_stats(&st, drawn_since, draw_sum_ns, draw_max_ns);
            draw_sum_ns = 0;
            draw_max_ns = 0;
            total.presented += st.presented;
            total.missed_vblanks += st.missed_vblanks;
            total.discarded += st.discarded;
            drawn_since = 0;
            next_stats += 1000000000u;
        }
    }
    if (s->opt.stats) {
        printf("total: presented %llu | missed %llu | discarded %llu\n",
               (unsigned long long)total.presented, (unsigned long long)total.missed_vblanks,
               (unsigned long long)total.discarded);
    }
    /* Every object on the surface's queue must be gone before the window is destroyed. */
    for (i = 0; i < BUFFERS; ++i) {
        free_slot(&d.buffers[i]);
    }
    wl_shm_destroy(d.shm);
    wl_registry_destroy(d.registry);
    wl_proxy_wrapper_destroy(d.display_wrapper);
    return 0;
}

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
    if (ax_thread_create(&draw_t, draw_thread, &s) != AX_OK ||
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
