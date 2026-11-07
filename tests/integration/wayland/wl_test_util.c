/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#define _GNU_SOURCE /* memfd_create */

#include "wl_test_util.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "axion/platform/ax_surface_native.h"

#include "axion/base/ax_thread.h"

int axt_wayland_env(void)
{
    const char *display = getenv("AX_TEST_WAYLAND_DISPLAY");
    const char *required = getenv("AX_REQUIRE_WAYLAND_TESTS");
    if (display == NULL || display[0] == '\0') {
        return (required != NULL && strcmp(required, "1") == 0) ? -1 : 0;
    }
    /* libwayland connects to WAYLAND_DISPLAY: point it at the test compositor only. */
    setenv("WAYLAND_DISPLAY", display, 1);
    return 1;
}

ax_result axt_app_create(axt_app *t, uint32_t max_windows)
{
    ax_app_desc desc;
    memset(&desc, 0, sizeof desc);
    ax_tracking_allocator_init(&t->tracker, ax_default_allocator());
    t->allocator = ax_tracking_allocator_get(&t->tracker);
    desc.struct_size = sizeof desc;
    desc.allocator = &t->allocator;
    desc.app_id = AX_STR("net.axonworks.axion.tests");
    desc.max_windows = max_windows;
    return ax_app_create(&desc, &t->app);
}

size_t axt_app_destroy(axt_app *t)
{
    ax_app_destroy(t->app);
    return ax_tracking_allocator_deinit(&t->tracker);
}

ax_window_desc axt_window_desc(float width, float height, uint32_t decorations)
{
    ax_window_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.title = AX_STR("AXION test");
    d.width = width;
    d.height = height;
    d.decorations = decorations;
    d.flags = AX_WINDOW_RESIZABLE;
    return d;
}

bool axt_pump_until(ax_app app, bool (*cond)(void *ctx), void *ctx, uint64_t timeout_ns)
{
    uint64_t deadline = ax_time_monotonic_ns() + timeout_ns;
    while (!cond(ctx)) {
        if (ax_time_monotonic_ns() >= deadline) {
            return false;
        }
        if (ax_app_pump(app, 10u * 1000000u) != AX_OK) {
            return false;
        }
    }
    return true;
}

typedef struct wait_ctx {
    ax_app app;
    uint32_t type;
    ax_window window;
    ax_event *out;
    bool found;
} wait_ctx;

static bool event_arrived(void *p)
{
    wait_ctx *c = (wait_ctx *)p;
    ax_event e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    while (!c->found && ax_app_poll_event(c->app, &e) == AX_OK && e.type != AX_EVENT_NONE) {
        if (e.type == c->type && e.window.h == c->window.h) {
            c->found = true;
            if (c->out != NULL) {
                *c->out = e;
            }
        }
    }
    return c->found;
}

bool axt_wait_event(ax_app app, uint32_t type, ax_window window, ax_event *out, uint64_t timeout_ns)
{
    wait_ctx c;
    c.app = app;
    c.type = type;
    c.window = window;
    c.out = out;
    c.found = false;
    return axt_pump_until(app, event_arrived, &c, timeout_ns);
}

/* ---- shm ----------------------------------------------------------------------------------------
 */

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    axt_shm *s = (axt_shm *)data;
    (void)version;
    if (strcmp(interface, wl_shm_interface.name) == 0 && s->shm == NULL) {
        s->shm = (struct wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {registry_global,
                                                              registry_global_remove};

bool axt_shm_init(axt_shm *s, ax_surface surface)
{
    ax_surface_native native;
    memset(s, 0, sizeof *s);
    memset(&native, 0, sizeof native);
    native.struct_size = sizeof native;
    if (ax_surface_get_native(surface, &native) != AX_OK || native.kind != AX_NATIVE_WAYLAND) {
        return false;
    }
    s->display = (struct wl_display *)native.u.wayland.display;
    s->queue = (struct wl_event_queue *)native.u.wayland.queue;
    /* A registry on the presenter queue, so wl_shm and every buffer made from it deliver their
     * events there. The roundtrip reads the socket from this thread, which libwayland supports
     * alongside the main thread's reads. */
    s->display_wrapper = (struct wl_display *)wl_proxy_create_wrapper(s->display);
    wl_proxy_set_queue((struct wl_proxy *)s->display_wrapper, s->queue);
    s->registry = wl_display_get_registry(s->display_wrapper);
    wl_registry_add_listener(s->registry, &registry_listener, s);
    if (wl_display_roundtrip_queue(s->display, s->queue) < 0 || s->shm == NULL) {
        axt_shm_deinit(s);
        return false;
    }
    return true;
}

void axt_shm_deinit(axt_shm *s)
{
    int i;
    for (i = 0; i < AXT_SHM_MAX_BUFFERS; ++i) {
        if (s->buffers[i] != NULL) {
            wl_buffer_destroy(s->buffers[i]);
        }
    }
    if (s->shm != NULL) {
        wl_shm_destroy(s->shm);
    }
    if (s->registry != NULL) {
        wl_registry_destroy(s->registry);
    }
    if (s->display_wrapper != NULL) {
        wl_proxy_wrapper_destroy(s->display_wrapper);
    }
    memset(s, 0, sizeof *s);
}

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    axt_shm *s = (axt_shm *)data;
    int i;
    for (i = 0; i < AXT_SHM_MAX_BUFFERS; ++i) {
        if (s->buffers[i] == buffer) {
            s->buffers[i] = NULL;
        }
    }
    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {buffer_release};

bool axt_shm_commit(axt_shm *s, struct wl_surface *surface, uint32_t width, uint32_t height,
                    uint32_t color)
{
    size_t stride = (size_t)width * 4u;
    size_t size = stride * height;
    int fd = memfd_create("axion-test-buffer", MFD_CLOEXEC);
    uint32_t *pixels;
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    size_t i;
    int slot;

    for (slot = 0; slot < AXT_SHM_MAX_BUFFERS && s->buffers[slot] != NULL; ++slot) {
    }
    if (slot == AXT_SHM_MAX_BUFFERS) {
        if (fd >= 0) {
            close(fd);
        }
        return false; /* the compositor is holding every buffer */
    }
    if (fd < 0) {
        return false;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return false;
    }
    pixels = (uint32_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        close(fd);
        return false;
    }
    for (i = 0; i < size / 4u; ++i) {
        pixels[i] = color;
    }
    munmap(pixels, size);
    pool = wl_shm_create_pool(s->shm, fd, (int32_t)size);
    buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)width, (int32_t)height, (int32_t)stride,
                                       WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &buffer_listener, s);
    s->buffers[slot] = buffer;

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, (int32_t)width, (int32_t)height);
    wl_surface_commit(surface);
    wl_display_flush(s->display);
    return true;
}

/* ---- compositor globals ------------------------------------------------------------------- */

typedef struct axt_global_query {
    const char *interface;
    bool found;
} axt_global_query;

static void query_global(void *data, struct wl_registry *registry, uint32_t name,
                         const char *interface, uint32_t version)
{
    axt_global_query *q = (axt_global_query *)data;
    (void)registry;
    (void)name;
    (void)version;
    if (strcmp(interface, q->interface) == 0) {
        q->found = true;
    }
}

static void query_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener query_listener = {query_global, query_global_remove};

bool axt_compositor_has_global(const char *interface)
{
    axt_global_query q;
    struct wl_display *display;
    struct wl_registry *registry;
    const char *name = getenv("AX_TEST_WAYLAND_DISPLAY");

    q.interface = interface;
    q.found = false;
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    display = wl_display_connect(name);
    if (display == NULL) {
        return false;
    }
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &query_listener, &q);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return q.found;
}
