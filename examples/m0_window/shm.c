/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window, shared-memory renderer (--renderer=shm): paints XRGB8888 buffers on the CPU and
 * commits them itself. Only the bar and the part it left are repainted each frame. Kept to
 * compare with the GPU path under the same statistics; platform/ never uses wl_shm.
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
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_surface_native.h"
#include "m0_window.h"

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

/* Paints the static parts: background, and the title band and edge when we draw the frame. */
static void draw_background(buffer *b, int32_t x0, int32_t x1, int32_t caption_px, int32_t edge_px)
{
    fill_rect(b, x0, 0, x1, (int32_t)b->height, M0_COLOR_BG);
    if (caption_px > 0) {
        fill_rect(b, x0, 0, x1, caption_px, M0_COLOR_CAPTION);
        fill_rect(b, x0, 0, x1, edge_px, M0_COLOR_EDGE);
        fill_rect(b, x0, (int32_t)b->height - edge_px, x1, (int32_t)b->height, M0_COLOR_EDGE);
        if (x0 < edge_px) {
            fill_rect(b, 0, 0, edge_px, (int32_t)b->height, M0_COLOR_EDGE);
        }
        if (x1 > (int32_t)b->width - edge_px) {
            fill_rect(b, (int32_t)b->width - edge_px, 0, (int32_t)b->width, (int32_t)b->height,
                      M0_COLOR_EDGE);
        }
    }
}

static bool draw_frame(drawer *d, const ax_frame_target *t, bool csd)
{
    const uint32_t w = t->pixel_width;
    const uint32_t h = t->pixel_height;
    m0_layout l;
    int32_t caption_px, edge_px, bar_x0, bar_x1, bar_w;
    buffer *b = acquire_buffer(d, w, h);
    int32_t dx0, dx1;

    if (b == NULL) {
        return false;
    }
    m0_layout_frame(t, csd, &l);
    caption_px = l.caption_px;
    edge_px = l.edge_px;
    bar_x0 = l.bar_x0;
    bar_x1 = l.bar_x1;
    bar_w = bar_x1 - bar_x0;
    if (b->fresh) {
        draw_background(b, 0, (int32_t)w, caption_px, edge_px);
        b->fresh = false;
    } else {
        draw_background(b, b->bar_x0, b->bar_x1, caption_px, edge_px); /* erase its old bar */
    }
    fill_rect(b, bar_x0, caption_px, bar_x1, (int32_t)h - l.bar_bottom_px, M0_COLOR_BAR);
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

int m0_shm_draw_thread(void *arg)
{
    shared *s = (shared *)arg;
    drawer d;
    ax_surface_native native;
    bool draw = true;
    uint64_t next_stats = ax_time_monotonic_ns() + 1000000000u;
    m0_draw_stats stats;
    int i;

    memset(&d, 0, sizeof d);
    memset(&stats, 0, sizeof stats);
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

    while (!m0_stopping(s)) {
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
                m0_draw_stats_add(&stats, ax_time_monotonic_ns() - t0);
            }
        }
        ax_surface_wait_frame(s->surface, 100u * 1000000u, &reason);
        /* Animate: draw on every frame callback. On a timeout (hidden window: no callbacks) wait
         * again rather than piling up frames. */
        draw = reason != AX_WAKE_TIMEOUT;

        if (s->opt.stats && ax_time_monotonic_ns() >= next_stats) {
            m0_print_stats(s, &stats, NULL);
            next_stats += 1000000000u;
        }
    }
    if (s->opt.stats) {
        m0_print_totals();
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
