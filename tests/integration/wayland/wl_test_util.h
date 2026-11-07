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

/* ---- shared-memory buffers, for presenting without a GPU (tests only; never in platform/) ---- */

struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct wl_shm;
struct wl_surface;
struct wl_buffer;

#define AXT_SHM_MAX_BUFFERS 32

typedef struct axt_shm {
    struct wl_buffer *buffers[AXT_SHM_MAX_BUFFERS]; /* attached, not yet released */
    struct wl_display *display;
    struct wl_event_queue *queue; /* the surface's presenter queue */
    struct wl_display *display_wrapper;
    struct wl_registry *registry;
    struct wl_shm *shm;
} axt_shm;

/* Binds wl_shm on the surface's presenter queue (from ax_surface_get_native). Presenter thread. */
bool axt_shm_init(axt_shm *s, ax_surface surface);
/* Destroys buffers the compositor has not released yet: every object on the surface's queue must
 * be gone before the window is destroyed. Presenter thread, after its last commit. */
void axt_shm_deinit(axt_shm *s);

/* Attaches a new width x height buffer filled with `color` (XRGB8888), damages it and commits.
 * The buffer is destroyed when the compositor releases it. Presenter thread. */
bool axt_shm_commit(axt_shm *s, struct wl_surface *surface, uint32_t width, uint32_t height,
                    uint32_t color);

#endif /* AX_WL_TEST_UTIL_H */
