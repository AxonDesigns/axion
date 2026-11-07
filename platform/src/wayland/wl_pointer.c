/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland backend: pointer input for hit-test driven move and resize.
 */
#define _POSIX_C_SOURCE 200809L

#include "wl_internal.h"

void ax_wl_seat_bound(ax_app_impl *app)
{
    (void)app;
}

void ax_wl_pointer_window_destroyed(ax_app_impl *app, ax_window_impl *w)
{
    (void)app;
    (void)w;
}

void ax_wl_pointer_shutdown(ax_app_impl *app)
{
    (void)app;
}
