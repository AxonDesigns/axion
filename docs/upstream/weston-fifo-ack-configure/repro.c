/*
 * Minimal reproduction: xdg_surface.ack_configure applied before content updates still queued
 * behind a wp_fifo_v1 barrier.
 *
 * The client commits a 320x240 buffer every time it can, each commit with
 * wp_fifo_v1.set_barrier + wait_barrier (what Mesa's Vulkan WSI does for VK_PRESENT_MODE_FIFO_KHR).
 * After a few frames it asks to be maximized and, as soon as the configure arrives, acknowledges
 * it and commits a buffer of the configured size. Expected: the acknowledgement takes effect with
 * that commit. Observed on weston 15.0.1 (headless, pixman): weston applies the acknowledgement on
 * receipt; when it later applies the 320x240 content updates still queued behind the barrier, it
 * checks them against the maximized state and posts
 *   xdg_wm_base error 4: "xdg_surface geometry (320 x 240) does not match the configured
 *   maximized state (W x H)".
 *
 * Build: see build.sh. Run against a weston that announces wp_fifo_manager_v1.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "fifo-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wp_fifo_manager_v1 *fifo_manager;

static uint32_t pending_serial;
static int32_t pending_w, pending_h;
static bool configured;

static void registry_global(void *data, struct wl_registry *r, uint32_t name, const char *iface,
                            uint32_t version)
{
    (void)data;
    (void)version;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    } else if (strcmp(iface, wp_fifo_manager_v1_interface.name) == 0) {
        fifo_manager = wl_registry_bind(r, name, &wp_fifo_manager_v1_interface, 1);
    }
}
static void registry_remove(void *data, struct wl_registry *r, uint32_t name)
{
    (void)data;
    (void)r;
    (void)name;
}
static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

static void wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static void surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
    (void)data;
    (void)s;
    pending_serial = serial;
    configured = true;
}
static const struct xdg_surface_listener surface_listener = {surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
                               struct wl_array *states)
{
    (void)data;
    (void)t;
    (void)states;
    pending_w = w;
    pending_h = h;
}
static void toplevel_close(void *data, struct xdg_toplevel *t)
{
    (void)data;
    (void)t;
}
static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close,
                                                               NULL, NULL};

static void buffer_release(void *data, struct wl_buffer *b)
{
    (void)data;
    wl_buffer_destroy(b);
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};

static struct wl_buffer *make_buffer(int w, int h, uint32_t color)
{
    int stride = w * 4, size = stride * h;
    char name[] = "/tmp/fifo-repro-XXXXXX";
    int fd = mkstemp(name);
    uint32_t *px;
    struct wl_shm_pool *pool;
    struct wl_buffer *b;
    int i;
    unlink(name);
    if (fd < 0 || ftruncate(fd, size) != 0) {
        exit(1);
    }
    px = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (i = 0; i < w * h; ++i) {
        px[i] = color;
    }
    munmap(px, (size_t)size);
    pool = wl_shm_create_pool(shm, fd, size);
    b = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b, &buffer_listener, NULL);
    return b;
}

/* REPRO_NO_FIFO=1: the same client without barriers, as a control (no error expected). */
static bool use_fifo(void)
{
    const char *v = getenv("REPRO_NO_FIFO");
    return v == NULL || strcmp(v, "1") != 0;
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_surface *surface;
    struct xdg_surface *xsurface;
    struct xdg_toplevel *toplevel;
    struct wp_fifo_v1 *fifo;
    int w = 320, h = 240, frame;

    if (display == NULL) {
        fprintf(stderr, "no display\n");
        return 1;
    }
    wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (compositor == NULL || shm == NULL || wm_base == NULL || fifo_manager == NULL) {
        fprintf(stderr, "missing globals (wp_fifo_manager_v1: %s)\n", fifo_manager ? "yes" : "no");
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
    surface = wl_compositor_create_surface(compositor);
    fifo = wp_fifo_manager_v1_get_fifo(fifo_manager, surface);
    xsurface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xsurface, &surface_listener, NULL);
    toplevel = xdg_surface_get_toplevel(xsurface);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
    wl_surface_commit(surface);
    while (!configured) {
        wl_display_dispatch(display);
    }
    xdg_surface_ack_configure(xsurface, pending_serial);
    configured = false;

    /* FIFO frames: queue several content updates behind barriers, as a FIFO swapchain does. */
    for (frame = 0; frame < 6; ++frame) {
        wl_surface_attach(surface, make_buffer(w, h, 0xFF203040u + (uint32_t)frame * 16u), 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, w, h);
        if (use_fifo()) {
            wp_fifo_v1_set_barrier(fifo);
            wp_fifo_v1_wait_barrier(fifo);
        }
        wl_surface_commit(surface);
    }
    wl_display_flush(display);

    /* Resize: maximize, then acknowledge and commit the new size at once. */
    xdg_toplevel_set_maximized(toplevel);
    while (!configured) {
        if (wl_display_dispatch(display) < 0) {
            break;
        }
    }
    if (pending_w > 0 && pending_h > 0) {
        w = pending_w;
        h = pending_h;
    }
    printf("acknowledging configure %u (%dx%d)%s\n", pending_serial, w, h,
           use_fifo() ? " with 6 content updates of 320x240 queued behind barriers" : "");
    xdg_surface_ack_configure(xsurface, pending_serial);
    wl_surface_attach(surface, make_buffer(w, h, 0xFF406080u), 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, w, h);
    if (use_fifo()) {
        wp_fifo_v1_set_barrier(fifo);
        wp_fifo_v1_wait_barrier(fifo);
    }
    wl_surface_commit(surface);

    /* Let the queue drain; a protocol error shows up here. */
    for (frame = 0; frame < 30; ++frame) {
        if (wl_display_roundtrip(display) < 0) {
            int err = wl_display_get_error(display);
            fprintf(stderr, "connection closed by the compositor (error %d)\n", err);
            return 2;
        }
        {
            struct timespec ts = {0, 20000000};
            nanosleep(&ts, NULL);
        }
    }
    printf("no protocol error\n");
    wl_display_disconnect(display);
    return 0;
}
