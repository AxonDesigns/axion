/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wayland integration tests: apps, windows, configures and events against a real (headless)
 * compositor. The test thread is the app's main thread and pumps it.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

AX_TEST(app_connects_and_reports_caps)
{
    axt_app t;
    uint64_t caps;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);

    AX_CHECK_EQ_I(ax_app_backend(t.app), AX_BACKEND_WAYLAND);
    caps = ax_app_caps(t.app);
    AX_CHECK((caps & AX_CAP_WINDOW_FRAMELESS) != 0);
    AX_CHECK((caps & AX_CAP_WINDOW_MULTIPLE) != 0);
    AX_CHECK((caps & AX_CAP_WINDOW_HIT_REGIONS) != 0);
    /* Wayland never allows these. */
    AX_CHECK((caps & AX_CAP_WINDOW_SET_POSITION) == 0);
    AX_CHECK((caps & AX_CAP_WINDOW_ALWAYS_ON_TOP) == 0);
    /* Not implemented in this step: accepted, ignored, reported false. */
    AX_CHECK((caps & AX_CAP_WINDOW_TRANSPARENCY) == 0);
    AX_CHECK((caps & AX_CAP_WINDOW_BACKDROP_BLUR) == 0);
    AX_CHECK((caps & AX_CAP_WINDOW_CUSTOM_TITLEBAR) == 0);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(create_and_destroy_windows)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(320, 240, AX_DECORATIONS_NATIVE);
    ax_window w[3];
    ax_surface s;
    int i;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);

    for (i = 0; i < 3; ++i) {
        AX_REQUIRE(ax_window_create(t.app, &d, &w[i]) == AX_OK);
        AX_CHECK(!AX_HANDLE_IS_NULL(w[i]));
    }
    AX_CHECK(!AX_HANDLE_EQ(w[0], w[1]) && !AX_HANDLE_EQ(w[1], w[2]));
    AX_CHECK_RESULT(ax_window_get_surface(w[1], &s), AX_OK);
    AX_CHECK(!AX_HANDLE_IS_NULL(s));

    AX_CHECK_RESULT(ax_window_destroy(w[1]), AX_OK);
    /* A destroyed window's handle is stale, and so is its surface's. */
    AX_CHECK_RESULT(ax_window_destroy(w[1]), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_window_set_title(w[1], AX_STR("x")), AX_ERR_INVALID_HANDLE);
    {
        ax_surface_info info;
        info.struct_size = sizeof info;
        AX_CHECK_RESULT(ax_surface_get_info(s, &info), AX_ERR_INVALID_HANDLE);
    }
    /* The slot is reused with a new handle; the old one stays invalid. */
    {
        ax_window again;
        AX_REQUIRE(ax_window_create(t.app, &d, &again) == AX_OK);
        AX_CHECK(!AX_HANDLE_EQ(again, w[1]));
        AX_CHECK_RESULT(ax_window_destroy(w[1]), AX_ERR_INVALID_HANDLE);
        AX_CHECK_RESULT(ax_window_destroy(again), AX_OK);
    }
    AX_CHECK_RESULT(ax_window_destroy(w[0]), AX_OK);
    AX_CHECK_RESULT(ax_window_destroy(w[2]), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(first_configure_delivers_resized)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(320, 240, AX_DECORATIONS_NONE);
    ax_window w;
    ax_event e;
    ax_window_info info;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &w) == AX_OK);

    info.struct_size = sizeof info;
    AX_REQUIRE(ax_window_get_info(w, &info) == AX_OK);
    AX_CHECK(!info.configured);

    AX_REQUIRE(axt_wait_event(t.app, AX_EVENT_RESIZED, w, &e, AXT_SECONDS(5)));
    AX_CHECK(e.data.resized.width > 0 && e.data.resized.height > 0);
    AX_CHECK(e.data.resized.scale >= 1.0f);
    AX_CHECK_EQ_U(e.data.resized.pixel_width,
                  (uint32_t)(e.data.resized.width * e.data.resized.scale + 0.5f));
    AX_REQUIRE(ax_window_get_info(w, &info) == AX_OK);
    AX_CHECK(info.configured);
    AX_CHECK(info.width == e.data.resized.width && info.height == e.data.resized.height);
    {
        ax_surface s;
        ax_surface_info si;
        si.struct_size = sizeof si;
        AX_REQUIRE(ax_window_get_surface(w, &s) == AX_OK);
        AX_REQUIRE(ax_surface_get_info(s, &si) == AX_OK);
        AX_CHECK_EQ_U(si.pixel_width, e.data.resized.pixel_width);
        AX_CHECK_EQ_U(si.pixel_height, e.data.resized.pixel_height);
    }
    AX_CHECK_RESULT(ax_window_destroy(w), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

/* A headless compositor has no user to press a close button, and weston's test protocol that
 * could send xdg_toplevel.close is not packaged by distributions. The test therefore injects the
 * event into the backend's xdg_toplevel.close handler (wl_testing.h): everything from the handler
 * to the event queue is exercised, but not the compositor sending it. A real close is checked
 * by hand on a desktop with examples/m0_window. */
AX_TEST(close_requested_is_delivered_and_cancelable)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(200, 200, AX_DECORATIONS_NATIVE);
    ax_window w;
    ax_window_info info;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &w) == AX_OK);

    ax__wl_test_inject_close(w);
    AX_CHECK(axt_wait_event(t.app, AX_EVENT_CLOSE_REQUESTED, w, NULL, AXT_SECONDS(5)));
    /* Ignoring the request cancels it: the window is still there. */
    info.struct_size = sizeof info;
    AX_CHECK_RESULT(ax_window_get_info(w, &info), AX_OK);
    /* The user asks again: a new event. */
    ax__wl_test_inject_close(w);
    AX_CHECK(axt_wait_event(t.app, AX_EVENT_CLOSE_REQUESTED, w, NULL, AXT_SECONDS(5)));
    AX_CHECK_RESULT(ax_window_destroy(w), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

/* ---- calls from other threads are handed to the main thread ---------------------------------- */

typedef struct worker_ctx {
    ax_app app;
    ax_window window;
    ax_result create_result;
    ax_result title_result;
    ax_result destroy_result;
    ax_atomic_u32 done;
} worker_ctx;

static int app_thread(void *arg)
{
    worker_ctx *c = (worker_ctx *)arg;
    ax_window_desc d = axt_window_desc(300, 200, AX_DECORATIONS_NONE);
    c->create_result = ax_window_create(c->app, &d, &c->window);
    c->title_result = ax_window_set_title(c->window, AX_STR("from the app thread"));
    c->destroy_result = ax_window_destroy(c->window);
    ax_atomic_store_u32(&c->done, 1, AX_MO_RELEASE);
    return 0;
}

static bool worker_done(void *p)
{
    return ax_atomic_load_u32(&((worker_ctx *)p)->done, AX_MO_ACQUIRE) != 0;
}

AX_TEST(calls_from_other_threads_run_on_main)
{
    axt_app t;
    worker_ctx c;
    ax_thread th;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);

    memset(&c, 0, sizeof c);
    c.app = t.app;
    ax_atomic_init_u32(&c.done, 0);
    AX_REQUIRE(ax_thread_create(&th, app_thread, &c) == AX_OK);
    /* The main thread keeps pumping; the worker's calls complete inside the pump. */
    AX_CHECK(axt_pump_until(t.app, worker_done, &c, AXT_SECONDS(10)));
    ax_thread_join(&th);
    AX_CHECK_RESULT(c.create_result, AX_OK);
    AX_CHECK_RESULT(c.title_result, AX_OK);
    AX_CHECK_RESULT(c.destroy_result, AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

static int create_on_dead_window(void *arg)
{
    worker_ctx *c = (worker_ctx *)arg;
    /* Errors raised on the main thread come back with their message on this thread. */
    c->title_result = ax_window_set_title(c->window, AX_STR("stale"));
    ax_atomic_store_u32(&c->done, 1, AX_MO_RELEASE);
    return ax_last_error()->code == AX_ERR_INVALID_HANDLE ? 0 : 1;
}

AX_TEST(errors_from_marshalled_calls_reach_the_caller)
{
    axt_app t;
    worker_ctx c;
    ax_thread th;
    ax_window_desc d = axt_window_desc(100, 100, AX_DECORATIONS_NONE);
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    memset(&c, 0, sizeof c);
    c.app = t.app;
    ax_atomic_init_u32(&c.done, 0);
    AX_REQUIRE(ax_window_create(t.app, &d, &c.window) == AX_OK);
    AX_REQUIRE(ax_window_destroy(c.window) == AX_OK);
    AX_REQUIRE(ax_thread_create(&th, create_on_dead_window, &c) == AX_OK);
    AX_CHECK(axt_pump_until(t.app, worker_done, &c, AXT_SECONDS(10)));
    AX_CHECK_EQ_I(ax_thread_join(&th), 0);
    AX_CHECK_RESULT(c.title_result, AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(max_windows_is_enforced)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(100, 100, AX_DECORATIONS_NONE);
    ax_window a, b, c;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 2) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &a) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &b) == AX_OK);
    AX_CHECK_RESULT(ax_window_create(t.app, &d, &c), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK(AX_HANDLE_IS_NULL(c));
    AX_CHECK_RESULT(ax_window_destroy(a), AX_OK);
    AX_CHECK_RESULT(ax_window_create(t.app, &d, &c), AX_OK);
    /* ax_app_destroy destroys the windows still open. */
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(struct_size_is_checked)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(100, 100, AX_DECORATIONS_NONE);
    ax_window w;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    d.struct_size = 8;
    AX_CHECK_RESULT(ax_window_create(t.app, &d, &w), AX_ERR_ABI_MISMATCH);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

/* ---- thread checks ------------------------------------------------------------------------------
 */

static int pump_from_wrong_thread(void *arg)
{
    worker_ctx *c = (worker_ctx *)arg;
    c->create_result = ax_app_pump(c->app, 0); /* debug builds abort here */
    return 0;
}

AX_DEATH_TEST(pump_off_the_main_thread_aborts)
{
    axt_app t;
    worker_ctx c;
    ax_thread th;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    memset(&c, 0, sizeof c);
    c.app = t.app;
    AX_REQUIRE(ax_thread_create(&th, pump_from_wrong_thread, &c) == AX_OK);
    ax_thread_join(&th);
}

#if defined(NDEBUG)
static int destroy_from_wrong_thread(void *arg)
{
    worker_ctx *c = (worker_ctx *)arg;
    c->create_result = ax_app_destroy(c->app);
    return 0;
}
#endif

AX_TEST(main_only_calls_report_wrong_thread_in_release)
{
#if !defined(NDEBUG)
    AX_SKIP("debug builds abort instead (see pump_off_the_main_thread_aborts)");
#else
    axt_app t;
    worker_ctx c;
    ax_thread th;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    memset(&c, 0, sizeof c);
    c.app = t.app;
    AX_REQUIRE(ax_thread_create(&th, destroy_from_wrong_thread, &c) == AX_OK);
    ax_thread_join(&th);
    AX_CHECK_RESULT(c.create_result, AX_ERR_WRONG_THREAD);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
#endif
}
