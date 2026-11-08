/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: pointer input, only what hit-test driven move and resize need (the full
 * input model is M2).
 *
 * On a left-button press the main thread looks the pointer position up in the window's newest
 * published hit map (a triple buffer: no waiting on the publishing thread) and starts an
 * interactive move or resize. Cursors follow the region under the pointer: by name through
 * wp-cursor-shape-v1 where the compositor has it, otherwise images from the cursor theme through
 * libwayland-cursor (decision 33).
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include <wayland-cursor.h>

#include "common/hit_test.h"
#include "cursor-shape-v1-client-protocol.h"
#include "wl_internal.h"
#include "xdg-shell-client-protocol.h"

#define AX_BTN_LEFT 0x110 /* BTN_LEFT from linux/input-event-codes.h */

/* ---- cursors ------------------------------------------------------------------------------------
 */

static uint32_t cursor_shape(ax_cursor c)
{
    switch (c) {
    case AX_CURSOR_RESIZE_N:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE;
    case AX_CURSOR_RESIZE_S:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
    case AX_CURSOR_RESIZE_W:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE;
    case AX_CURSOR_RESIZE_E:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE;
    case AX_CURSOR_RESIZE_NW:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE;
    case AX_CURSOR_RESIZE_NE:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE;
    case AX_CURSOR_RESIZE_SW:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE;
    case AX_CURSOR_RESIZE_SE:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE;
    case AX_CURSOR_DEFAULT:
    default:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
}

/* Cursor theme names, newest spelling first (themes differ). */
static const char *const *cursor_names(ax_cursor c)
{
    static const char *const def[] = {"default", "left_ptr", NULL};
    static const char *const n[] = {"n-resize", "top_side", NULL};
    static const char *const s[] = {"s-resize", "bottom_side", NULL};
    static const char *const w[] = {"w-resize", "left_side", NULL};
    static const char *const e[] = {"e-resize", "right_side", NULL};
    static const char *const nw[] = {"nw-resize", "top_left_corner", NULL};
    static const char *const ne[] = {"ne-resize", "top_right_corner", NULL};
    static const char *const sw[] = {"sw-resize", "bottom_left_corner", NULL};
    static const char *const se[] = {"se-resize", "bottom_right_corner", NULL};
    switch (c) {
    case AX_CURSOR_RESIZE_N:
        return n;
    case AX_CURSOR_RESIZE_S:
        return s;
    case AX_CURSOR_RESIZE_W:
        return w;
    case AX_CURSOR_RESIZE_E:
        return e;
    case AX_CURSOR_RESIZE_NW:
        return nw;
    case AX_CURSOR_RESIZE_NE:
        return ne;
    case AX_CURSOR_RESIZE_SW:
        return sw;
    case AX_CURSOR_RESIZE_SE:
        return se;
    case AX_CURSOR_DEFAULT:
    default:
        return def;
    }
}

static bool load_theme(wl_app *wl)
{
    const char *theme;
    const char *size_env;
    int size = 24;
    if (wl->cursor_theme != NULL) {
        return true;
    }
    if (wl->shm == NULL) {
        return false;
    }
    theme = getenv("XCURSOR_THEME");
    size_env = getenv("XCURSOR_SIZE");
    if (size_env != NULL && atoi(size_env) > 0) {
        size = atoi(size_env);
    }
    wl->cursor_theme = wl_cursor_theme_load(theme, size, wl->shm);
    if (wl->cursor_theme != NULL && wl->cursor_surface == NULL) {
        wl->cursor_surface = wl_compositor_create_surface(wl->compositor);
    }
    return wl->cursor_theme != NULL;
}

static void set_cursor(wl_app *wl, ax_cursor c, bool force)
{
    if (wl->pointer == NULL || (!force && wl->current_cursor == (int)c)) {
        return;
    }
    wl->current_cursor = (int)c;
    if (wl->cursor_shape_device != NULL) {
        wp_cursor_shape_device_v1_set_shape(wl->cursor_shape_device, wl->pointer_enter_serial,
                                            cursor_shape(c));
        return;
    }
    if (load_theme(wl)) {
        const char *const *name;
        struct wl_cursor *cursor = NULL;
        for (name = cursor_names(c); *name != NULL && cursor == NULL; ++name) {
            cursor = wl_cursor_theme_get_cursor(wl->cursor_theme, *name);
        }
        if (cursor != NULL && cursor->image_count > 0) {
            struct wl_cursor_image *image = cursor->images[0];
            struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
            wl_pointer_set_cursor(wl->pointer, wl->pointer_enter_serial, wl->cursor_surface,
                                  (int32_t)image->hotspot_x, (int32_t)image->hotspot_y);
            wl_surface_attach(wl->cursor_surface, buffer, 0, 0);
            wl_surface_damage_buffer(wl->cursor_surface, 0, 0, (int32_t)image->width,
                                     (int32_t)image->height);
            wl_surface_commit(wl->cursor_surface);
        }
    }
}

/* ---- hit testing ---------------------------------------------------------------------------- */

static uint32_t region_under_pointer(wl_app *wl)
{
    const ax_hit_map *map;
    if (wl->pointer_window == NULL) {
        return AX_REGION_CLIENT;
    }
    map = ax__window_hit_map(wl->pointer_window);
    if (map == NULL) {
        return AX_REGION_CLIENT;
    }
    return ax_hit_test(map->regions, map->count, wl->pointer_x, wl->pointer_y);
}

static void update_cursor(wl_app *wl, bool force)
{
    bool resizable =
        wl->pointer_window != NULL && (wl->pointer_window->flags & AX_WINDOW_RESIZABLE) != 0;
    set_cursor(wl, ax_hit_region_cursor(region_under_pointer(wl), resizable), force);
}

/* ---- wl_pointer ---------------------------------------------------------------------------------
 */

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
                          wl_fixed_t x, wl_fixed_t y)
{
    ax_app_impl *app = (ax_app_impl *)data;
    wl_app *wl = wl_app_of(app);
    (void)p;
    wl->pointer_window = ax_wl_window_from_surface(app, s);
    wl->pointer_enter_serial = serial;
    wl->pointer_x = (float)wl_fixed_to_double(x);
    wl->pointer_y = (float)wl_fixed_to_double(y);
    if (wl->pointer_window != NULL) {
        update_cursor(wl, true); /* the cursor is undefined until a client sets it */
    }
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
    wl_app *wl = wl_app_of((ax_app_impl *)data);
    (void)p;
    (void)serial;
    (void)s;
    wl->pointer_window = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
                           wl_fixed_t y)
{
    wl_app *wl = wl_app_of((ax_app_impl *)data);
    (void)p;
    (void)time;
    wl->pointer_x = (float)wl_fixed_to_double(x);
    wl->pointer_y = (float)wl_fixed_to_double(y);
    if (wl->pointer_window != NULL) {
        update_cursor(wl, false);
    }
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state)
{
    ax_app_impl *app = (ax_app_impl *)data;
    wl_app *wl = wl_app_of(app);
    ax_window_impl *w = wl->pointer_window;
    uint32_t region;
    uint32_t edges;
    (void)p;
    (void)time;

    if (w == NULL || button != AX_BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }
    region = region_under_pointer(wl);
    if (region == AX_REGION_CAPTION) {
        xdg_toplevel_move(wl_window_of(w)->toplevel, wl->seat, serial);
        return;
    }
    edges = ax_hit_region_edges(region);
    if (edges != 0 && (w->flags & AX_WINDOW_RESIZABLE) != 0) {
        /* AX_EDGE_* bits match xdg_toplevel.resize_edge (top 1, bottom 2, left 4, right 8). */
        xdg_toplevel_resize(wl_window_of(w)->toplevel, wl->seat, serial, edges);
    }
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
                         wl_fixed_t value)
{
    (void)data;
    (void)p;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *p)
{
    (void)data;
    (void)p;
}

static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source)
{
    (void)data;
    (void)p;
    (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
    (void)data;
    (void)p;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t d)
{
    (void)data;
    (void)p;
    (void)axis;
    (void)d;
}

/* Designated initializers: later libwayland headers append events (axis_value120, ...), which
 * the seat version we bind never sends; they stay NULL here. */
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

/* ---- wl_seat -------------------------------------------------------------------------------- */

static void release_pointer(wl_app *wl)
{
    if (wl->cursor_shape_device != NULL) {
        wp_cursor_shape_device_v1_destroy(wl->cursor_shape_device);
        wl->cursor_shape_device = NULL;
    }
    if (wl->pointer != NULL) {
        if (wl->seat_version >= WL_POINTER_RELEASE_SINCE_VERSION) {
            wl_pointer_release(wl->pointer);
        } else {
            wl_pointer_destroy(wl->pointer);
        }
        wl->pointer = NULL;
    }
    wl->pointer_window = NULL;
    wl->current_cursor = -1;
}

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    ax_app_impl *app = (ax_app_impl *)data;
    wl_app *wl = wl_app_of(app);
    bool has_pointer = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;

    if (has_pointer && wl->pointer == NULL) {
        wl->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(wl->pointer, &pointer_listener, app);
        if (wl->cursor_shape_manager != NULL) {
            wl->cursor_shape_device =
                wp_cursor_shape_manager_v1_get_pointer(wl->cursor_shape_manager, wl->pointer);
        }
    } else if (!has_pointer && wl->pointer != NULL) {
        release_pointer(wl);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void ax_wl_seat_bound(ax_app_impl *app)
{
    wl_app *wl = wl_app_of(app);
    wl_seat_add_listener(wl->seat, &seat_listener, app);
}

void ax_wl_pointer_window_destroyed(ax_app_impl *app, ax_window_impl *w)
{
    wl_app *wl = wl_app_of(app);
    if (wl->pointer_window == w) {
        wl->pointer_window = NULL;
    }
}

void ax_wl_pointer_shutdown(ax_app_impl *app)
{
    wl_app *wl = wl_app_of(app);
    release_pointer(wl);
    if (wl->cursor_surface != NULL) {
        wl_surface_destroy(wl->cursor_surface);
        wl->cursor_surface = NULL;
    }
    if (wl->cursor_theme != NULL) {
        wl_cursor_theme_destroy(wl->cursor_theme);
        wl->cursor_theme = NULL;
    }
}
