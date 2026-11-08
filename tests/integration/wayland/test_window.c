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
#include "axion/platform/ax_event_notify.h"
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

/* ---- marshalled calls when the main thread stops pumping (decision 42)
 * ---------------------------- */

#include "common/platform_internal.h"

typedef struct blocked_call {
    ax_app app;
    ax_window window;
    ax_result result;
    char message[AX_ERROR_MESSAGE_CAPACITY];
    ax_atomic_u32 returned;
} blocked_call;

static int create_window_call(void *arg)
{
    blocked_call *c = (blocked_call *)arg;
    ax_window_desc d = axt_window_desc(100, 100, AX_DECORATIONS_NONE);
    const ax_error *e;
    c->result = ax_window_create(c->app, &d, &c->window);
    e = ax_last_error();
    if (c->result != AX_OK) {
        size_t n = e->message.len < sizeof c->message - 1 ? e->message.len : sizeof c->message - 1;
        memcpy(c->message, e->message.ptr, n);
        c->message[n] = '\0';
    }
    ax_atomic_store_u32(&c->returned, 1, AX_MO_RELEASE);
    return 0;
}

static void blocked_call_init(blocked_call *c, ax_app app)
{
    memset(c, 0, sizeof *c);
    c->app = app;
    ax_atomic_init_u32(&c->returned, 0);
}

/* Waits (without pumping) until the worker's call is queued for the main thread. */
static bool wait_until_queued(ax_app app)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    uint64_t deadline = ax_time_monotonic_ns() + AXT_SECONDS(5);
    while (ax_time_monotonic_ns() < deadline) {
        void *head = ax_atomic_load_ptr(&impl->commands, AX_MO_ACQUIRE);
        if (ax_atomic_load_u32(&impl->callers, AX_MO_ACQUIRE) != 0 && head != NULL) {
            return true;
        }
        ax_thread_yield();
    }
    return false;
}

static bool call_returned(void *p)
{
    return ax_atomic_load_u32(&((blocked_call *)p)->returned, AX_MO_ACQUIRE) != 0;
}

AX_TEST(waiting_calls_fail_when_run_returns_and_new_ones_are_refused)
{
    axt_app t;
    blocked_call first;
    blocked_call refused;
    blocked_call later;
    ax_thread th;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);

    /* A call made while nobody pumps waits for the main thread... */
    blocked_call_init(&first, t.app);
    AX_REQUIRE(ax_thread_create(&th, create_window_call, &first) == AX_OK);
    AX_REQUIRE(wait_until_queued(t.app));
    AX_CHECK(!call_returned(&first));
    /* ...and when ax_app_run returns (here at once, quit was already requested) it fails. */
    AX_REQUIRE(ax_app_quit(t.app) == AX_OK);
    AX_CHECK_RESULT(ax_app_run(t.app), AX_OK);
    ax_thread_join(&th);
    AX_CHECK_RESULT(first.result, AX_ERR_UNSUPPORTED);
    AX_CHECK(strstr(first.message, "not pumping") != NULL);
    AX_CHECK(AX_HANDLE_IS_NULL(first.window));

    /* New calls are refused at once while nothing pumps (the join proves it does not hang). */
    blocked_call_init(&refused, t.app);
    AX_REQUIRE(ax_thread_create(&th, create_window_call, &refused) == AX_OK);
    ax_thread_join(&th);
    AX_CHECK_RESULT(refused.result, AX_ERR_UNSUPPORTED);

    /* Pumping again accepts calls again. Pump once before starting the worker: until the main
     * thread pumps, its call would be refused, which is the documented behaviour, not the point. */
    AX_REQUIRE(ax_app_pump(t.app, 0) == AX_OK);
    blocked_call_init(&later, t.app);
    AX_REQUIRE(ax_thread_create(&th, create_window_call, &later) == AX_OK);
    AX_CHECK(axt_pump_until(t.app, call_returned, &later, AXT_SECONDS(10)));
    ax_thread_join(&th);
    AX_CHECK_RESULT(later.result, AX_OK);
    AX_CHECK_RESULT(ax_window_destroy(later.window), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(waiting_calls_fail_when_the_app_is_destroyed)
{
    axt_app t;
    blocked_call c;
    ax_thread th;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);

    blocked_call_init(&c, t.app);
    AX_REQUIRE(ax_thread_create(&th, create_window_call, &c) == AX_OK);
    AX_REQUIRE(wait_until_queued(t.app));
    /* Destroy fails the waiting call and returns only after the caller has left the app. */
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
    ax_thread_join(&th);
    AX_CHECK_RESULT(c.result, AX_ERR_UNSUPPORTED);
    AX_CHECK(strstr(c.message, "destroyed") != NULL);
    /* The handle is gone now. */
    blocked_call_init(&c, t.app);
    AX_REQUIRE(ax_thread_create(&th, create_window_call, &c) == AX_OK);
    ax_thread_join(&th);
    AX_CHECK_RESULT(c.result, AX_ERR_INVALID_HANDLE);
}

/* ---- a consumer that sleeps on its own wait is told when events are queued --------------------
 */

static void count_notify(void *user)
{
    ax_atomic_fetch_add_u32((ax_atomic_u32 *)user, 1, AX_MO_RELAXED);
}

static void drain_events(ax_app app)
{
    ax_event e;
    e.struct_size = sizeof e;
    do {
        ax_app_poll_event(app, &e);
    } while (e.type != AX_EVENT_NONE);
}

AX_TEST(event_notify_is_called_when_events_are_queued)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(200, 200, AX_DECORATIONS_NATIVE);
    ax_window w;
    ax_atomic_u32 calls;
    uint32_t before;
    AXT_REQUIRE_COMPOSITOR();
    ax_atomic_init_u32(&calls, 0);
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_CHECK_RESULT(ax_app_set_event_notify((ax_app){0}, count_notify, &calls),
                    AX_ERR_INVALID_HANDLE);
    AX_REQUIRE(ax_app_set_event_notify(t.app, count_notify, &calls) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &w) == AX_OK);

    /* The first configure queues RESIZED. */
    AX_CHECK(axt_wait_event(t.app, AX_EVENT_RESIZED, w, NULL, AXT_SECONDS(5)));
    AX_CHECK(ax_atomic_load_u32(&calls, AX_MO_RELAXED) >= 1);
    drain_events(t.app);

    before = ax_atomic_load_u32(&calls, AX_MO_RELAXED);
    ax__wl_test_inject_close(w);
    AX_CHECK(axt_wait_event(t.app, AX_EVENT_CLOSE_REQUESTED, w, NULL, AXT_SECONDS(5)));
    AX_CHECK(ax_atomic_load_u32(&calls, AX_MO_RELAXED) > before);

    /* Cleared: no more calls. */
    AX_CHECK_RESULT(ax_app_set_event_notify(t.app, NULL, NULL), AX_OK);
    before = ax_atomic_load_u32(&calls, AX_MO_RELAXED);
    ax__wl_test_inject_close(w);
    AX_CHECK(axt_wait_event(t.app, AX_EVENT_CLOSE_REQUESTED, w, NULL, AXT_SECONDS(5)));
    AX_CHECK_EQ_U(ax_atomic_load_u32(&calls, AX_MO_RELAXED), before);

    AX_CHECK_RESULT(ax_window_destroy(w), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}
