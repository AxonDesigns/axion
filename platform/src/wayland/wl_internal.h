/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend internals.
 *
 * Threads. Every object below is created, used and destroyed on the main thread, except the
 * per-surface presentation objects (wl_callback, wp_presentation_feedback), which the surface's
 * presenter creates through proxy wrappers bound to the surface's private event queue and
 * dispatches itself. The main thread is the only one that reads the display fd.
 */
#ifndef AX_PLATFORM_WL_INTERNAL_H
#define AX_PLATFORM_WL_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <wayland-client.h>

#include "common/platform_internal.h"

struct wl_cursor_theme;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;
struct wp_presentation;
struct wp_viewporter;
struct wp_viewport;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct wp_cursor_shape_manager_v1;
struct wp_cursor_shape_device_v1;

typedef struct wl_app {
    struct wl_display *display;
    struct wl_registry *registry;
    int wake_fd; /* eventfd: other threads wake the pump */

    struct wl_compositor *compositor;
    uint32_t compositor_version;
    struct xdg_wm_base *wm_base;
    uint32_t wm_base_version;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct wp_presentation *presentation;
    clockid_t presentation_clock;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    struct wl_shm *shm;

    /* pointer input, only what hit-testing needs (step 7) */
    struct wl_seat *seat;
    uint32_t seat_version;
    struct wl_pointer *pointer;
    struct wp_cursor_shape_device_v1 *cursor_shape_device;
    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    struct ax_window_impl *pointer_window; /* window under the pointer */
    uint32_t pointer_enter_serial;
    float pointer_x, pointer_y;
    int current_cursor;
} wl_app;

/* A presentation request in flight (owned by the presenter). */
typedef struct wl_frame_record {
    struct wp_presentation_feedback *feedback;
    struct ax_window_impl *window;
    uint64_t configure_ns; /* configure this frame acknowledged, or 0 */
    bool in_use;
} wl_frame_record;

#define AX_WL_MAX_FRAMES_IN_FLIGHT 8

typedef struct wl_window {
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;

    /* presenter side: a private queue and proxy wrappers bound to it */
    struct wl_event_queue *queue;
    struct wl_surface *surface_wrapper;
    struct wp_presentation *presentation_wrapper;
    struct wl_callback *frame_callback; /* pending frame callback, presenter-owned */
    bool frame_done;                    /* presenter-owned */
    wl_frame_record frames[AX_WL_MAX_FRAMES_IN_FLIGHT];
    int applied_buffer_scale; /* presenter-owned: last wl_surface.set_buffer_scale */

    /* main thread: configure assembly */
    int32_t pending_width, pending_height; /* from xdg_toplevel.configure; 0 = client decides */
    bool pending_activated;
    bool pending_suspended;
    bool pending_maximized;
    bool pending_fullscreen;
    bool pending_tiled;
    bool configured;
    uint32_t last_serial;
    float logical_width, logical_height; /* last configured */
    float desc_width, desc_height;
    float floating_width, floating_height; /* size outside maximized/fullscreen/tiled states */
    float scale;                           /* preferred scale from the compositor */
    bool fractional_scale_known;
    bool resizable;
    float min_width, min_height;
} wl_window;

static inline wl_app *wl_app_of(ax_app_impl *app)
{
    return (wl_app *)app->backend;
}

static inline wl_window *wl_window_of(ax_window_impl *w)
{
    return (wl_window *)w->backend;
}

/* wl_window.c */
ax_result ax_wl_window_create(ax_app_impl *app, ax_window_impl *w, const ax_window_desc *desc);
void ax_wl_window_destroy(ax_app_impl *app, ax_window_impl *w);
ax_result ax_wl_window_set_title(ax_window_impl *w, const char *title);
ax_result ax_wl_window_set_min_size(ax_window_impl *w, float width, float height);
ax_window_impl *ax_wl_window_from_surface(ax_app_impl *app, struct wl_surface *surface);

/* wl_present.c */
void ax_wl_surface_native(ax_window_impl *w, ax_surface_native *out);
ax_result ax_wl_surface_prepare_commit(ax_window_impl *w, const ax_frame_target *frame);
void ax_wl_surface_dispatch(ax_window_impl *w);
bool ax_wl_surface_frame_due(ax_window_impl *w);
void ax_wl_present_window_destroyed(ax_window_impl *w);

/* wl_pointer.c */
void ax_wl_seat_bound(ax_app_impl *app);
void ax_wl_pointer_window_destroyed(ax_app_impl *app, ax_window_impl *w);
void ax_wl_pointer_shutdown(ax_app_impl *app);

/* Monotonic time of a presentation timestamp on the compositor's clock. */
uint64_t ax_wl_presentation_to_monotonic(wl_app *wl, uint64_t sec, uint32_t nsec);

#endif /* AX_PLATFORM_WL_INTERNAL_H */
