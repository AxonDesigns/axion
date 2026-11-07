/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: xdg toplevel windows, configure handling, decorations and scale.
 *
 * Configure flow (decision 34). xdg_toplevel.configure and decoration/scale events only record
 * pending state; xdg_surface.configure turns it into an ax_surface_config, publishes it for the
 * presenter and queues events. The main thread never acknowledges a configure and, after the
 * empty initial commit made here, never commits the surface: the presenter acknowledges the
 * latest serial in the same commit that delivers a buffer of the configured size.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_memory.h"
#include "fractional-scale-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wl_internal.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static int32_t logical_int(float v)
{
    return v <= 0.0f ? 0 : (int32_t)ceilf(v);
}

/* Publishes the window's configure (or a scale-only update) for the presenter. */
static void publish(ax_window_impl *w, bool size_changed, bool scale_changed)
{
    wl_window *ww = wl_window_of(w);
    ax_surface_config c;
    c.serial = ww->last_serial;
    c.logical_width = ww->logical_width;
    c.logical_height = ww->logical_height;
    c.scale = ww->scale;
    c.pixel_width = ax__pixels(ww->logical_width, ww->scale);
    c.pixel_height = ax__pixels(ww->logical_height, ww->scale);
    c.time_ns = ax_time_monotonic_ns();
    ax__window_publish_config(w, &c, size_changed, scale_changed);
}

static void set_scale(ax_window_impl *w, float scale)
{
    wl_window *ww = wl_window_of(w);
    if (scale <= 0.0f || scale == ww->scale) {
        return;
    }
    ww->scale = scale;
    if (ww->configured) {
        /* Same serial: a scale change is not a configure, but the presenter must redraw at the
         * new pixel size, and the app hears about it. */
        publish(w, false, true);
    }
}

/* ---- wl_surface
 * ---------------------------------------------------------------------------------- */

static void surface_enter(void *data, struct wl_surface *s, struct wl_output *output)
{
    (void)data;
    (void)s;
    (void)output;
}

static void surface_leave(void *data, struct wl_surface *s, struct wl_output *output)
{
    (void)data;
    (void)s;
    (void)output;
}

static void surface_preferred_buffer_scale(void *data, struct wl_surface *s, int32_t factor)
{
    ax_window_impl *w = (ax_window_impl *)data;
    (void)s;
    /* With wp-fractional-scale the fractional value is authoritative. */
    if (!wl_window_of(w)->fractional_scale_known && factor > 0) {
        set_scale(w, (float)factor);
    }
}

static void surface_preferred_buffer_transform(void *data, struct wl_surface *s, uint32_t t)
{
    (void)data;
    (void)s;
    (void)t; /* buffer transforms are not used yet */
}

static const struct wl_surface_listener surface_listener = {surface_enter, surface_leave,
                                                            surface_preferred_buffer_scale,
                                                            surface_preferred_buffer_transform};

static void fractional_preferred_scale(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale)
{
    ax_window_impl *w = (ax_window_impl *)data;
    (void)f;
    wl_window_of(w)->fractional_scale_known = true;
    set_scale(w, (float)scale / 120.0f); /* the protocol sends scale * 120 */
}

static const struct wp_fractional_scale_v1_listener fractional_listener = {
    fractional_preferred_scale};

/* ---- xdg
 * ----------------------------------------------------------------------------------------- */

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t width, int32_t height,
                               struct wl_array *states)
{
    ax_window_impl *w = (ax_window_impl *)data;
    wl_window *ww = wl_window_of(w);
    const uint32_t *state;
    (void)t;

    ww->pending_width = width;
    ww->pending_height = height;
    ww->pending_activated = false;
    ww->pending_suspended = false;
    ww->pending_maximized = false;
    ww->pending_fullscreen = false;
    ww->pending_tiled = false;
    for (state = (const uint32_t *)states->data;
         (const char *)state < (const char *)states->data + states->size; ++state) {
        switch (*state) {
        case XDG_TOPLEVEL_STATE_ACTIVATED:
            ww->pending_activated = true;
            break;
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
            ww->pending_maximized = true;
            break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            ww->pending_fullscreen = true;
            break;
        case 5: /* XDG_TOPLEVEL_STATE_TILED_LEFT .. TILED_BOTTOM (since 2) */
        case 6:
        case 7:
        case 8:
            ww->pending_tiled = true;
            break;
        case 9: /* XDG_TOPLEVEL_STATE_SUSPENDED (since 6), spelled out for older headers */
            ww->pending_suspended = true;
            break;
        default:
            break;
        }
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
    (void)t;
    ax__window_close_requested((ax_window_impl *)data);
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h)
{
    (void)data;
    (void)t;
    (void)w;
    (void)h;
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *t, struct wl_array *caps)
{
    (void)data;
    (void)t;
    (void)caps;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close, toplevel_configure_bounds, toplevel_wm_capabilities};

static void xdg_surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
    ax_window_impl *w = (ax_window_impl *)data;
    wl_window *ww = wl_window_of(w);
    float width;
    float height;
    bool size_changed;
    bool first = !ww->configured;
    (void)s;

    /* 0 means the client chooses: its floating size, which is the size it had before being
     * maximized, made fullscreen or tiled (so leaving those states restores it), and the
     * requested size at first. */
    width = ww->pending_width > 0 ? (float)ww->pending_width : ww->floating_width;
    height = ww->pending_height > 0 ? (float)ww->pending_height : ww->floating_height;
    if (!ww->pending_maximized && !ww->pending_fullscreen && !ww->pending_tiled) {
        ww->floating_width = width;
        ww->floating_height = height;
    }
    size_changed = first || width != ww->logical_width || height != ww->logical_height;

    ww->last_serial = serial;
    ww->logical_width = width;
    ww->logical_height = height;
    ww->configured = true;
    /* Always publish: every configure needs acknowledging, even with an unchanged size. */
    publish(w, size_changed, false);

    ax__window_set_focus(w, ww->pending_activated);
    ax__window_set_occluded(w, ww->pending_suspended);
}

static const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

static void decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *d, uint32_t mode)
{
    (void)d;
    ax__window_set_server_decorated((ax_window_impl *)data,
                                    mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
    decoration_configure};

/* ---- lifecycle
 * ------------------------------------------------------------------------------------ */

static void apply_size_limits(wl_window *ww)
{
    if (!ww->resizable) {
        /* Wayland has no "not resizable" flag: pinning min = max is the convention. */
        int32_t w = logical_int(ww->desc_width);
        int32_t h = logical_int(ww->desc_height);
        xdg_toplevel_set_min_size(ww->toplevel, w, h);
        xdg_toplevel_set_max_size(ww->toplevel, w, h);
    } else {
        xdg_toplevel_set_min_size(ww->toplevel, logical_int(ww->min_width),
                                  logical_int(ww->min_height));
    }
}

ax_result ax_wl_window_create(ax_app_impl *app, ax_window_impl *w, const ax_window_desc *desc)
{
    wl_app *wl = wl_app_of(app);
    wl_window *ww;
    char *title;

    ww = AX_MEM_NEW(&app->allocator, wl_window);
    if (ww == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_window_create: out of memory");
    }
    memset(ww, 0, sizeof *ww);
    w->backend = ww;
    ww->desc_width = desc->width > 0.0f ? desc->width : 640.0f;
    ww->desc_height = desc->height > 0.0f ? desc->height : 480.0f;
    ww->logical_width = ww->desc_width;
    ww->logical_height = ww->desc_height;
    ww->floating_width = ww->desc_width;
    ww->floating_height = ww->desc_height;
    ww->min_width = desc->min_width;
    ww->min_height = desc->min_height;
    ww->resizable = (desc->flags & AX_WINDOW_RESIZABLE) != 0;
    ww->scale = 1.0f;

    ww->surface = wl_compositor_create_surface(wl->compositor);
    wl_surface_add_listener(ww->surface, &surface_listener, w);
    if (wl->fractional_scale_manager != NULL && wl->viewporter != NULL) {
        ww->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            wl->fractional_scale_manager, ww->surface);
        wp_fractional_scale_v1_add_listener(ww->fractional_scale, &fractional_listener, w);
    }
    if (wl->viewporter != NULL) {
        ww->viewport = wp_viewporter_get_viewport(wl->viewporter, ww->surface);
    }

    ww->xdg_surface = xdg_wm_base_get_xdg_surface(wl->wm_base, ww->surface);
    xdg_surface_add_listener(ww->xdg_surface, &xdg_surface_listener, w);
    ww->toplevel = xdg_surface_get_toplevel(ww->xdg_surface);
    xdg_toplevel_add_listener(ww->toplevel, &toplevel_listener, w);

    /* xdg_toplevel wants NUL-terminated UTF-8. */
    title = (char *)ax_mem_alloc(&app->allocator, desc->title.len + 1, 1);
    if (title != NULL) {
        if (desc->title.len != 0) {
            memcpy(title, desc->title.ptr, desc->title.len);
        }
        title[desc->title.len] = '\0';
        xdg_toplevel_set_title(ww->toplevel, title);
        ax_mem_free(&app->allocator, title, desc->title.len + 1);
    }
    if (app->app_id[0] != '\0') {
        xdg_toplevel_set_app_id(ww->toplevel, app->app_id);
    }
    apply_size_limits(ww);

    /* Decorations: ask the compositor to draw the frame for NATIVE; NONE and (until it exists)
     * CUSTOM_TITLEBAR ask for client-side mode. Without xdg-decoration there is no server frame
     * and the app's hit regions drive move and resize. */
    if (wl->decoration_manager != NULL) {
        ww->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(wl->decoration_manager,
                                                                            ww->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(ww->decoration, &decoration_listener, w);
        zxdg_toplevel_decoration_v1_set_mode(ww->decoration,
                                             desc->decorations == AX_DECORATIONS_NATIVE
                                                 ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                                 : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }

    /* The presenter's private queue, and proxy wrappers that create objects on it. */
    ww->queue = wl_display_create_queue(wl->display);
    ww->surface_wrapper = (struct wl_surface *)wl_proxy_create_wrapper(ww->surface);
    wl_proxy_set_queue((struct wl_proxy *)ww->surface_wrapper, ww->queue);
    if (wl->presentation != NULL) {
        ww->presentation_wrapper =
            (struct wp_presentation *)wl_proxy_create_wrapper(wl->presentation);
        wl_proxy_set_queue((struct wl_proxy *)ww->presentation_wrapper, ww->queue);
    }

    /* The initial, empty commit: the compositor answers with the first configure. This is the
     * only commit the main thread ever makes on this surface. */
    wl_surface_commit(ww->surface);
    wl_display_flush(wl->display);
    return AX_OK;
}

void ax_wl_window_destroy(ax_app_impl *app, ax_window_impl *w)
{
    wl_window *ww = wl_window_of(w);
    (void)app;
    if (ww == NULL) {
        return;
    }
    ax_wl_pointer_window_destroyed(app, w);
    ax_wl_present_window_destroyed(w); /* objects on the presenter queue go first */
    if (ww->presentation_wrapper != NULL) {
        wl_proxy_wrapper_destroy(ww->presentation_wrapper);
    }
    if (ww->surface_wrapper != NULL) {
        wl_proxy_wrapper_destroy(ww->surface_wrapper);
    }
    if (ww->decoration != NULL) {
        zxdg_toplevel_decoration_v1_destroy(ww->decoration);
    }
    xdg_toplevel_destroy(ww->toplevel);
    xdg_surface_destroy(ww->xdg_surface);
    if (ww->fractional_scale != NULL) {
        wp_fractional_scale_v1_destroy(ww->fractional_scale);
    }
    if (ww->viewport != NULL) {
        wp_viewport_destroy(ww->viewport);
    }
    wl_surface_destroy(ww->surface);
    if (ww->queue != NULL) {
        wl_event_queue_destroy(ww->queue);
    }
    wl_display_flush(wl_app_of(w->app)->display);
    ax_mem_free(&w->app->allocator, ww, sizeof *ww);
    w->backend = NULL;
}

ax_result ax_wl_window_set_title(ax_window_impl *w, const char *title)
{
    xdg_toplevel_set_title(wl_window_of(w)->toplevel, title);
    return AX_OK;
}

ax_result ax_wl_window_set_min_size(ax_window_impl *w, float width, float height)
{
    wl_window *ww = wl_window_of(w);
    ww->min_width = width;
    ww->min_height = height;
    if (ww->resizable) {
        apply_size_limits(ww);
        /* Double-buffered state: it applies with the next commit, which is the presenter's. */
        ax__window_request_commit(w);
    }
    return AX_OK;
}

ax_window_impl *ax_wl_window_from_surface(ax_app_impl *app, struct wl_surface *surface)
{
    uint32_t i;
    if (surface == NULL) {
        return NULL;
    }
    for (i = 0; i < app->max_windows; ++i) {
        ax_window_impl *w = &app->windows[i];
        if (w->open && w->backend != NULL && wl_window_of(w)->surface == surface) {
            return w;
        }
    }
    return NULL;
}

/* ---- test hooks (wl_testing.h)
 * --------------------------------------------------------------------- */

#include "wl_testing.h"

void ax__wl_test_inject_close(ax_window window)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    if (w != NULL) {
        toplevel_close(w, wl_window_of(w)->toplevel);
    }
}

void ax__wl_test_set_maximized(ax_window window, bool maximized)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    if (w == NULL) {
        return;
    }
    if (maximized) {
        xdg_toplevel_set_maximized(wl_window_of(w)->toplevel);
    } else {
        xdg_toplevel_unset_maximized(wl_window_of(w)->toplevel);
    }
}
