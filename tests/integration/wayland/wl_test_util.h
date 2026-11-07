/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Helpers for the Wayland integration tests.
 *
 * The tests connect only to the compositor named by AX_TEST_WAYLAND_DISPLAY
 * (tools/ci/with-weston.sh starts a headless weston and sets it), never to the desktop's
 * WAYLAND_DISPLAY, so running the suite never opens windows on a developer's screen. Without it the
 * tests are skipped, unless AX_REQUIRE_WAYLAND_TESTS=1 (set by the script and in CI), which makes
 * them fail.
 */
#ifndef AX_WL_TEST_UTIL_H
#define AX_WL_TEST_UTIL_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_platform.h"
#include "axion/base/ax_tracking_allocator.h"

/* 1: a test compositor is configured; 0: skip; -1: required but missing (fail). */
int axt_wayland_env(void);

#define AXT_REQUIRE_COMPOSITOR()                                                                 \
    do {                                                                                         \
        int axt__env = axt_wayland_env();                                                        \
        if (axt__env == 0) {                                                                     \
            AX_SKIP("AX_TEST_WAYLAND_DISPLAY is not set (run through tools/ci/with-weston.sh)"); \
        }                                                                                        \
        if (axt__env < 0) {                                                                      \
            ax_test_fail(__FILE__, __LINE__,                                                     \
                         "AX_REQUIRE_WAYLAND_TESTS=1 but "                                       \
                         "AX_TEST_WAYLAND_DISPLAY is not set");                                  \
            return;                                                                              \
        }                                                                                        \
    } while (0)

typedef struct axt_app {
    ax_tracking_allocator tracker;
    ax_allocator allocator;
    ax_app app;
} axt_app;

/* Creates an app on the calling thread (which becomes its main thread), counting allocations. */
ax_result axt_app_create(axt_app *t, uint32_t max_windows);
/* Destroys the app; returns the number of leaked allocations. */
size_t axt_app_destroy(axt_app *t);

ax_window_desc axt_window_desc(float width, float height, uint32_t decorations);

/* Pumps the app (main thread) until cond(ctx) is true or timeout_ns passes; returns cond's value.
 */
bool axt_pump_until(ax_app app, bool (*cond)(void *ctx), void *ctx, uint64_t timeout_ns);

/* Pumps and polls events until one of `type` for `window` arrives; copies it to out if non-NULL. */
bool axt_wait_event(ax_app app, uint32_t type, ax_window window, ax_event *out,
                    uint64_t timeout_ns);

#define AXT_SECONDS(s) ((uint64_t)(s) * 1000000000u)

#endif /* AX_WL_TEST_UTIL_H */
