/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: CPU buffers through wl_shm (axion/platform/ax_present_cpu.h, decision 84).
 *
 * Each window keeps up to four buffers, each in its own memfd and wl_shm_pool. They are created
 * through a wl_shm proxy wrapper bound to the window's private queue, so the compositor's
 * wl_buffer.release events arrive when the presenter dispatches that queue (as frame callbacks
 * do). A buffer is busy from its commit until its release. A new size replaces a free buffer of
 * another size. Everything here runs on the presenter thread, except the release at window
 * destruction (main thread, after presenting stopped).
 */
#define _GNU_SOURCE /* memfd_create */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "axion/platform/ax_present_cpu.h"
#include "wl_internal.h"

AX_STATIC_ASSERT(AX_CPU_MAX_BUFFERS == 4u, "wl_window.shm holds AX_CPU_MAX_BUFFERS slots");

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    wl_shm_slot *slot = (wl_shm_slot *)data;
    (void)buffer;
    slot->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {buffer_release};

static void destroy_slot(wl_shm_slot *s)
{
    if (s->buffer != NULL) {
        wl_buffer_destroy(s->buffer);
    }
    if (s->pool != NULL) {
        wl_shm_pool_destroy(s->pool);
    }
    if (s->pixels != NULL) {
        munmap(s->pixels, s->size);
    }
    if (s->fd > 0) {
        close(s->fd);
    }
    memset(s, 0, sizeof *s);
}

static ax_result create_slot(wl_window *ww, wl_shm_slot *s, uint32_t width, uint32_t height)
{
    const size_t stride = (size_t)width * 4u;
    const size_t size = stride * height;
    if (size > (size_t)INT32_MAX) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_surface_cpu_acquire: %ux%u is too large",
                            width, height);
    }
    s->fd = memfd_create("axion-cpu-buffer", MFD_CLOEXEC);
    if (s->fd < 0) {
        s->fd = 0;
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_surface_cpu_acquire: memfd_create: %s",
                            strerror(errno));
    }
    if (ftruncate(s->fd, (off_t)size) != 0) {
        destroy_slot(s);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_surface_cpu_acquire: ftruncate: %s",
                            strerror(errno));
    }
    s->pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
    if (s->pixels == MAP_FAILED) {
        s->pixels = NULL;
        destroy_slot(s);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_surface_cpu_acquire: mmap: %s",
                            strerror(errno));
    }
    s->size = size;
    s->pool = wl_shm_create_pool(ww->shm_wrapper, s->fd, (int32_t)size);
    s->buffer = wl_shm_pool_create_buffer(s->pool, 0, (int32_t)width, (int32_t)height,
                                          (int32_t)stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(s->buffer, &buffer_listener, s);
    s->width = width;
    s->height = height;
    s->busy = false;
    return AX_OK;
}

ax_result ax_wl_cpu_acquire(ax_window_impl *w, uint32_t width, uint32_t height, ax_cpu_buffer *out)
{
    wl_window *ww = wl_window_of(w);
    wl_app *wl = wl_app_of(w->app);
    wl_shm_slot *pick = NULL;
    uint32_t i;

    if (wl->shm == NULL) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_surface_cpu_acquire: the compositor has no "
                                                "wl_shm");
    }
    if (ww->shm_wrapper == NULL) {
        ww->shm_wrapper = (struct wl_shm *)wl_proxy_create_wrapper(wl->shm);
        if (ww->shm_wrapper == NULL) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_surface_cpu_acquire: out of memory");
        }
        wl_proxy_set_queue((struct wl_proxy *)ww->shm_wrapper, ww->queue);
    }
    /* Releases that arrived since the last wait. */
    wl_display_dispatch_queue_pending(wl->display, ww->queue);

    /* A free buffer of this size; else a free one to replace; else an empty slot. */
    for (i = 0; i < AX_CPU_MAX_BUFFERS && pick == NULL; ++i) {
        wl_shm_slot *s = &ww->shm[i];
        if (s->buffer != NULL && !s->busy && s->width == width && s->height == height) {
            pick = s;
        }
    }
    for (i = 0; i < AX_CPU_MAX_BUFFERS && pick == NULL; ++i) {
        if (ww->shm[i].buffer != NULL && !ww->shm[i].busy) {
            pick = &ww->shm[i];
            destroy_slot(pick);
        }
    }
    for (i = 0; i < AX_CPU_MAX_BUFFERS && pick == NULL; ++i) {
        if (ww->shm[i].buffer == NULL) {
            pick = &ww->shm[i];
        }
    }
    if (pick == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY,
                            "ax_surface_cpu_acquire: every buffer is still held by the compositor");
    }
    if (pick->buffer == NULL) {
        ax_result r = create_slot(ww, pick, width, height);
        if (r != AX_OK) {
            return r;
        }
    }
    out->pixels = pick->pixels;
    out->width = width;
    out->height = height;
    out->stride = (size_t)width * 4u;
    out->format = AX_CPU_FORMAT_BGRA8;
    out->slot = (uint32_t)(pick - ww->shm);
    return AX_OK;
}

void ax_wl_cpu_present(ax_window_impl *w, uint32_t slot)
{
    wl_window *ww = wl_window_of(w);
    wl_app *wl = wl_app_of(w->app);
    wl_shm_slot *s = &ww->shm[slot];
    if (s->buffer == NULL) {
        return;
    }
    wl_surface_attach(ww->surface, s->buffer, 0, 0);
    if (wl->compositor_version >= 4) {
        wl_surface_damage_buffer(ww->surface, 0, 0, INT32_MAX, INT32_MAX);
    } else {
        wl_surface_damage(ww->surface, 0, 0, INT32_MAX, INT32_MAX);
    }
    wl_surface_commit(ww->surface);
    s->busy = true;
    wl_display_flush(wl->display);
}

void ax_wl_cpu_release(ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    uint32_t i;
    for (i = 0; i < AX_CPU_MAX_BUFFERS; ++i) {
        destroy_slot(&ww->shm[i]);
    }
    if (ww->shm_wrapper != NULL) {
        wl_proxy_wrapper_destroy(ww->shm_wrapper);
        ww->shm_wrapper = NULL;
    }
}
