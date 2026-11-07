/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#define _POSIX_C_SOURCE 200809L

#include "wl_test_util.h"

#include <stdlib.h>
#include <string.h>

#include "axion/base/ax_thread.h"

int axt_wayland_env(void)
{
    const char *display = getenv("AX_TEST_WAYLAND_DISPLAY");
    const char *required = getenv("AX_REQUIRE_WAYLAND_TESTS");
    if (display == NULL || display[0] == '\0') {
        return (required != NULL && strcmp(required, "1") == 0) ? -1 : 0;
    }
    /* libwayland connects to WAYLAND_DISPLAY: point it at the test compositor only. */
    setenv("WAYLAND_DISPLAY", display, 1);
    return 1;
}

ax_result axt_app_create(axt_app *t, uint32_t max_windows)
{
    ax_app_desc desc;
    memset(&desc, 0, sizeof desc);
    ax_tracking_allocator_init(&t->tracker, ax_default_allocator());
    t->allocator = ax_tracking_allocator_get(&t->tracker);
    desc.struct_size = sizeof desc;
    desc.allocator = &t->allocator;
    desc.app_id = AX_STR("net.axonworks.axion.tests");
    desc.max_windows = max_windows;
    return ax_app_create(&desc, &t->app);
}

size_t axt_app_destroy(axt_app *t)
{
    ax_app_destroy(t->app);
    return ax_tracking_allocator_deinit(&t->tracker);
}

ax_window_desc axt_window_desc(float width, float height, uint32_t decorations)
{
    ax_window_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.title = AX_STR("AXION test");
    d.width = width;
    d.height = height;
    d.decorations = decorations;
    d.flags = AX_WINDOW_RESIZABLE;
    return d;
}

bool axt_pump_until(ax_app app, bool (*cond)(void *ctx), void *ctx, uint64_t timeout_ns)
{
    uint64_t deadline = ax_time_monotonic_ns() + timeout_ns;
    while (!cond(ctx)) {
        if (ax_time_monotonic_ns() >= deadline) {
            return false;
        }
        if (ax_app_pump(app, 10u * 1000000u) != AX_OK) {
            return false;
        }
    }
    return true;
}

typedef struct wait_ctx {
    ax_app app;
    uint32_t type;
    ax_window window;
    ax_event *out;
    bool found;
} wait_ctx;

static bool event_arrived(void *p)
{
    wait_ctx *c = (wait_ctx *)p;
    ax_event e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    while (!c->found && ax_app_poll_event(c->app, &e) == AX_OK && e.type != AX_EVENT_NONE) {
        if (e.type == c->type && e.window.h == c->window.h) {
            c->found = true;
            if (c->out != NULL) {
                *c->out = e;
            }
        }
    }
    return c->found;
}

bool axt_wait_event(ax_app app, uint32_t type, ax_window window, ax_event *out, uint64_t timeout_ns)
{
    wait_ctx c;
    c.app = app;
    c.type = type;
    c.window = window;
    c.out = out;
    c.found = false;
    return axt_pump_until(app, event_arrived, &c, timeout_ns);
}
