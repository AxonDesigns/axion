/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: connection, globals, capabilities and the main thread's pump.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_memory.h"
#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wl_internal.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

/* Highest versions this backend knows how to use. */
#define AX_WL_COMPOSITOR_VERSION 6u /* wl_surface.preferred_buffer_scale */
#define AX_WL_WM_BASE_VERSION    6u /* xdg_toplevel suspended state */
#define AX_WL_SEAT_VERSION       5u

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

/* ---- globals ------------------------------------------------------------------------------------
 */

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    /* Answered at once on the main thread, so the window never looks hung even while app code is
     * busy. */
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

static void presentation_clock_id(void *data, struct wp_presentation *presentation, uint32_t clk)
{
    wl_app *wl = (wl_app *)data;
    (void)presentation;
    wl->presentation_clock = (clockid_t)clk;
}

static const struct wp_presentation_listener presentation_listener = {presentation_clock_id};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    ax_app_impl *app = (ax_app_impl *)data;
    wl_app *wl = wl_app_of(app);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl->compositor_version = min_u32(version, AX_WL_COMPOSITOR_VERSION);
        wl->compositor = (struct wl_compositor *)wl_registry_bind(
            registry, name, &wl_compositor_interface, wl->compositor_version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        wl->wm_base_version = min_u32(version, AX_WL_WM_BASE_VERSION);
        wl->wm_base = (struct xdg_wm_base *)wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                                             wl->wm_base_version);
        xdg_wm_base_add_listener(wl->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        wl->decoration_manager = (struct zxdg_decoration_manager_v1 *)wl_registry_bind(
            registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_presentation_interface.name) == 0) {
        wl->presentation = (struct wp_presentation *)wl_registry_bind(
            registry, name, &wp_presentation_interface, 1);
        wp_presentation_add_listener(wl->presentation, &presentation_listener, wl);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        wl->viewporter =
            (struct wp_viewporter *)wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        wl->fractional_scale_manager = (struct wp_fractional_scale_manager_v1 *)wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        wl->cursor_shape_manager = (struct wp_cursor_shape_manager_v1 *)wl_registry_bind(
            registry, name, &wp_cursor_shape_manager_v1_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl->shm = (struct wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0 && wl->seat == NULL) {
        wl->seat_version = min_u32(version, AX_WL_SEAT_VERSION);
        wl->seat = (struct wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface,
                                                      wl->seat_version);
        /* Listen at once: the seat's capabilities arrive right after the bind (during the second
         * roundtrip, when every other global is already bound). */
        ax_wl_seat_bound(app);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name; /* output and seat hot-unplug are not handled yet */
}

static const struct wl_registry_listener registry_listener = {registry_global,
                                                              registry_global_remove};

static uint64_t compute_caps(const wl_app *wl)
{
    uint64_t caps = AX_CAP_WINDOW_FRAMELESS | AX_CAP_WINDOW_MULTIPLE | AX_CAP_WINDOW_HIT_REGIONS;
    /* Wayland never lets clients place windows globally or keep them on top; transparency,
     * backdrops, capture exclusion and state restore are not implemented yet. */
    if (wl->decoration_manager != NULL) {
        caps |= AX_CAP_WINDOW_SERVER_DECORATIONS;
    }
    if (wl->fractional_scale_manager != NULL && wl->viewporter != NULL) {
        caps |= AX_CAP_FRACTIONAL_SCALE;
    }
    if (wl->presentation != NULL) {
        caps |= AX_CAP_PRESENTATION_TIMING;
    }
    if (wl->wm_base_version >= 6) {
        caps |= AX_CAP_OCCLUSION_EVENTS;
    }
    if (wl->cursor_shape_manager != NULL || wl->shm != NULL) {
        caps |= AX_CAP_CURSOR_SHAPES;
    }
    return caps;
}

static void destroy_globals(wl_app *wl)
{
    if (wl->cursor_shape_manager != NULL) {
        wp_cursor_shape_manager_v1_destroy(wl->cursor_shape_manager);
    }
    if (wl->fractional_scale_manager != NULL) {
        wp_fractional_scale_manager_v1_destroy(wl->fractional_scale_manager);
    }
    if (wl->viewporter != NULL) {
        wp_viewporter_destroy(wl->viewporter);
    }
    if (wl->presentation != NULL) {
        wp_presentation_destroy(wl->presentation);
    }
    if (wl->decoration_manager != NULL) {
        zxdg_decoration_manager_v1_destroy(wl->decoration_manager);
    }
    if (wl->wm_base != NULL) {
        xdg_wm_base_destroy(wl->wm_base);
    }
    if (wl->shm != NULL) {
        wl_shm_destroy(wl->shm);
    }
    if (wl->seat != NULL) {
        if (wl->seat_version >= WL_SEAT_RELEASE_SINCE_VERSION) {
            wl_seat_release(wl->seat);
        } else {
            wl_seat_destroy(wl->seat);
        }
    }
    if (wl->compositor != NULL) {
        wl_compositor_destroy(wl->compositor);
    }
    if (wl->registry != NULL) {
        wl_registry_destroy(wl->registry);
    }
}

/* ---- app lifecycle ------------------------------------------------------------------------------
 */

static void wl_shutdown(ax_app_impl *app)
{
    wl_app *wl = wl_app_of(app);
    if (wl == NULL) {
        return;
    }
    ax_wl_pointer_shutdown(app);
    destroy_globals(wl);
    if (wl->display != NULL) {
        wl_display_flush(wl->display);
        wl_display_disconnect(wl->display);
    }
    if (wl->wake_fd >= 0) {
        close(wl->wake_fd);
    }
    ax_mem_free(&app->allocator, wl, sizeof *wl);
    app->backend = NULL;
}

static ax_result wl_init(ax_app_impl *app, const ax_app_desc *desc)
{
    wl_app *wl;
    (void)desc;

    wl = AX_MEM_NEW(&app->allocator, wl_app);
    if (wl == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_app_create: out of memory");
    }
    memset(wl, 0, sizeof *wl);
    wl->wake_fd = -1;
    wl->presentation_clock = CLOCK_MONOTONIC;
    wl->current_cursor = -1;
    app->backend = wl;

    wl->display = wl_display_connect(NULL);
    if (wl->display == NULL) {
        wl_shutdown(app);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_app_create: no Wayland compositor reachable "
                                                "(WAYLAND_DISPLAY / XDG_RUNTIME_DIR)");
    }
    wl->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wl->wake_fd < 0) {
        wl_shutdown(app);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_app_create: eventfd failed (errno %d)",
                            errno);
    }
    wl->registry = wl_display_get_registry(wl->display);
    wl_registry_add_listener(wl->registry, &registry_listener, app);
    /* First roundtrip: globals. Second: events from freshly bound globals (presentation clock). */
    if (wl_display_roundtrip(wl->display) < 0 || wl_display_roundtrip(wl->display) < 0) {
        wl_shutdown(app);
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_app_create: the Wayland connection failed");
    }
    if (wl->compositor == NULL || wl->wm_base == NULL) {
        wl_shutdown(app);
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_app_create: the compositor lacks wl_compositor or xdg_wm_base");
    }
    app->caps = compute_caps(wl);
    return AX_OK;
}

/* ---- pump
 * ---------------------------------------------------------------------------------------- */

static ax_result connection_lost(wl_app *wl)
{
    int err = wl_display_get_error(wl->display);
    return ax_set_error(AX_ERR_DEVICE_LOST, "the Wayland connection was lost (error %d)", err);
}

static int timeout_ms(uint64_t timeout_ns)
{
    uint64_t ms;
    if (timeout_ns == UINT64_MAX) {
        return -1;
    }
    ms = timeout_ns / 1000000u + (timeout_ns % 1000000u != 0 ? 1u : 0u);
    return ms > 0x7FFFFFFFu ? 0x7FFFFFFF : (int)ms;
}

static void drain_wake_fd(wl_app *wl)
{
    uint64_t value;
    while (read(wl->wake_fd, &value, sizeof value) == (ssize_t)sizeof value) {
    }
}

static ax_result wl_pump(ax_app_impl *app, uint64_t timeout_ns)
{
    wl_app *wl = wl_app_of(app);
    struct pollfd fds[2];
    int ret;

    /* Work handed over by other threads, and events the consumer made room for. */
    ax__app_run_commands(app);
    ax_evq_flush(&app->events);

    while (wl_display_prepare_read(wl->display) != 0) {
        if (wl_display_dispatch_pending(wl->display) < 0) {
            return connection_lost(wl);
        }
    }

    fds[0].fd = wl_display_get_fd(wl->display);
    fds[0].events = POLLIN;
    fds[1].fd = wl->wake_fd;
    fds[1].events = POLLIN;
    fds[0].revents = fds[1].revents = 0;

    if (wl_display_flush(wl->display) < 0) {
        if (errno == EAGAIN) {
            fds[0].events |= POLLOUT; /* socket full: retry when writable */
        } else {
            wl_display_cancel_read(wl->display);
            return connection_lost(wl);
        }
    }

    do {
        ret = poll(fds, 2, timeout_ms(timeout_ns));
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        wl_display_cancel_read(wl->display);
        return ax_set_error(AX_ERR_DEVICE_LOST, "poll failed (errno %d)", errno);
    }

    if ((fds[0].revents & POLLIN) != 0) {
        if (wl_display_read_events(wl->display) < 0) {
            return connection_lost(wl);
        }
    } else {
        wl_display_cancel_read(wl->display);
    }
    if ((fds[0].revents & (POLLERR | POLLHUP)) != 0) {
        return connection_lost(wl);
    }
    if (wl_display_dispatch_pending(wl->display) < 0) {
        return connection_lost(wl);
    }
    /* Events for presenter queues may have been read: let presenters dispatch them. */
    ax__app_wake_presenters(app);

    if ((fds[1].revents & POLLIN) != 0) {
        drain_wake_fd(wl);
    }
    ax__app_run_commands(app);
    ax_evq_flush(&app->events);
    if (wl_display_flush(wl->display) < 0 && errno != EAGAIN) {
        return connection_lost(wl);
    }
    return AX_OK;
}

static void wl_wake(ax_app_impl *app)
{
    wl_app *wl = wl_app_of(app);
    uint64_t one = 1;
    /* A full counter (never in practice) still leaves the fd readable, which is all we need. */
    ssize_t n = write(wl->wake_fd, &one, sizeof one);
    (void)n;
}

uint64_t ax_wl_presentation_to_monotonic(wl_app *wl, uint64_t sec, uint32_t nsec)
{
    uint64_t t = sec * 1000000000u + nsec;
    struct timespec now_clk;
    uint64_t clk_now;
    uint64_t mono_now;

    if (wl->presentation_clock == CLOCK_MONOTONIC) {
        return t; /* ax_time_monotonic_ns uses CLOCK_MONOTONIC on Linux */
    }
    /* Another clock (CLOCK_MONOTONIC_RAW, ...): shift by the clocks' current offset. */
    if (clock_gettime(wl->presentation_clock, &now_clk) != 0) {
        return t;
    }
    clk_now = (uint64_t)now_clk.tv_sec * 1000000000u + (uint64_t)now_clk.tv_nsec;
    mono_now = ax_time_monotonic_ns();
    return t + mono_now - clk_now;
}

const ax_backend_ops ax_wayland_backend = {
    AX_BACKEND_WAYLAND,
    wl_init,
    wl_shutdown,
    wl_pump,
    wl_wake,
    ax_wl_window_create,
    ax_wl_window_destroy,
    ax_wl_window_set_title,
    ax_wl_window_set_min_size,
    ax_wl_surface_native,
    ax_wl_surface_prepare_commit,
    ax_wl_surface_dispatch,
    ax_wl_surface_frame_due,
    ax_wl_surface_frame_pending,
    ax_wl_surface_commit_failed,
    ax_wl_surface_drop_frame,
    ax_wl_surface_commits_in_flight,
    ax_wl_cpu_acquire,
    ax_wl_cpu_present,
    ax_wl_cpu_release,
};
