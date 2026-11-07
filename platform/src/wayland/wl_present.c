/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: the presenter side of a surface.
 */
#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "wl_internal.h"

void ax_wl_surface_native(ax_window_impl *w, ax_surface_native *out)
{
    wl_window *ww = wl_window_of(w);
    out->kind = AX_NATIVE_WAYLAND;
    out->u.wayland.display = wl_app_of(w->app)->display;
    out->u.wayland.surface = ww->surface;
    out->u.wayland.queue = ww->queue;
}

ax_result ax_wl_surface_prepare_commit(ax_window_impl *w, const ax_frame_target *frame)
{
    (void)w;
    (void)frame;
    return ax_set_error(AX_ERR_UNSUPPORTED, "presentation is not implemented yet");
}

void ax_wl_surface_dispatch(ax_window_impl *w)
{
    (void)w;
}

bool ax_wl_surface_frame_due(ax_window_impl *w)
{
    (void)w;
    return false;
}

void ax_wl_present_window_destroyed(ax_window_impl *w)
{
    (void)w;
}
