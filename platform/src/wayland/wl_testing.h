/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test hooks for the Wayland backend. Not part of any API; only the integration tests use them.
 */
#ifndef AX_PLATFORM_WL_TESTING_H
#define AX_PLATFORM_WL_TESTING_H

#include <stdbool.h>

#include "axion/ax_window.h"

/* Runs the xdg_toplevel.close handler for the window, as if the compositor had sent it. A headless
 * compositor has no user to click a close button, and weston's test protocol that could send it
 * is not packaged by distributions. Main thread. */
void ax__wl_test_inject_close(ax_window window);

/* Asks the compositor to maximize or unmaximize the window, which makes it send a configure with
 * a new size: the integration tests' way to trigger a compositor-driven resize. Main thread. */
void ax__wl_test_set_maximized(ax_window window, bool maximized);

#endif /* AX_PLATFORM_WL_TESTING_H */
