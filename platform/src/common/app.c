/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_app: handle table, main-thread checks and marshalling, the pump, events.
 */
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_memory.h"
#include "platform_internal.h"

/* ---- app handle table --------------------------------------------------------------------------
 * A small fixed table, claimed with compare-and-swap so ax_app_create needs no global lock. */

#define AX_MAX_APPS 8u

static ax_atomic_ptr g_apps[AX_MAX_APPS];
static ax_atomic_u32 g_app_generation = AX_ATOMIC_INIT(1);

ax_app_impl *ax__app_from_handle(ax_app app)
{
    uint32_t slot = ax_handle_slot(app.h);
    ax_app_impl *impl;
    if (app.h == 0 || slot >= AX_MAX_APPS) {
        return NULL;
    }
    impl = (ax_app_impl *)ax_atomic_load_ptr(&g_apps[slot], AX_MO_ACQUIRE);
    if (impl == NULL || impl->generation != ax_handle_generation(app.h)) {
        return NULL;
    }
    return impl;
}

ax_app_impl *ax__app_from_slot(uint32_t slot)
{
    return slot < AX_MAX_APPS ? (ax_app_impl *)ax_atomic_load_ptr(&g_apps[slot], AX_MO_ACQUIRE)
                              : NULL;
}

static ax_result invalid_app(const char *fn)
{
    return ax_set_error(AX_ERR_INVALID_HANDLE, "%s: invalid or destroyed ax_app", fn);
}

/* ---- thread checks and marshalling ------------------------------------------------------------
 */

bool ax__app_on_main(const ax_app_impl *app)
{
    return ax_thread_current_id() == app->main_thread;
}

/* thread: main. Debug builds abort; release builds return AX_ERR_WRONG_THREAD. */
#define AX_REQUIRE_MAIN(app, fn)                                                                   \
    do {                                                                                           \
        if (!ax__app_on_main(app)) {                                                               \
            AX_ASSERT_MSG(0, fn " must be called on the app's main thread");                       \
            return ax_set_error(AX_ERR_WRONG_THREAD, "%s must be called on the app's main thread", \
                                fn);                                                               \
        }                                                                                          \
    } while (0)

ax_result ax__app_call(ax_app_impl *app, ax_result (*fn)(void *ctx), void *ctx)
{
    ax_command cmd;
    void *head;

    if (ax__app_on_main(app)) {
        return fn(ctx);
    }
    cmd.fn = fn;
    cmd.ctx = ctx;
    cmd.result = AX_OK;
    cmd.message[0] = '\0';
    ax_atomic_init_u32(&cmd.done, 0);

    /* Push onto the lock-free stack (Treiber). */
    head = ax_atomic_load_ptr(&app->commands, AX_MO_RELAXED);
    do {
        cmd.next = (ax_command *)head;
    } while (
        !ax_atomic_compare_exchange_ptr(&app->commands, &head, &cmd, AX_MO_RELEASE, AX_MO_RELAXED));
    app->ops->wake(app);

    ax_mutex_lock(&app->cmd_lock);
    while (ax_atomic_load_u32(&cmd.done, AX_MO_ACQUIRE) == 0) {
        ax_cond_wait(&app->cmd_cond, &app->cmd_lock);
    }
    ax_mutex_unlock(&app->cmd_lock);

    /* The error detail was recorded on the main thread; re-record it on the caller's. */
    if (cmd.result != AX_OK) {
        ax_set_error(cmd.result, "%s", cmd.message);
    }
    return cmd.result;
}

/* Completes every queued command with `result` without running it (app shutting down). */
static void finish_commands(ax_app_impl *app, bool run, ax_result result)
{
    void *head = ax_atomic_load_ptr(&app->commands, AX_MO_RELAXED);
    ax_command *list;
    ax_command *fifo = NULL;
    ax_command *c;
    bool any = false;

    if (head == NULL) {
        return;
    }
    /* Take the whole stack, then reverse it into submission order. */
    while (!ax_atomic_compare_exchange_ptr(&app->commands, &head, NULL, AX_MO_ACQUIRE,
                                           AX_MO_RELAXED)) {
    }
    list = (ax_command *)head;
    while (list != NULL) {
        c = list;
        list = list->next;
        c->next = fifo;
        fifo = c;
    }
    while (fifo != NULL) {
        c = fifo;
        fifo = fifo->next; /* read before completion: the command lives on the caller's stack */
        if (run) {
            ax_clear_error();
            c->result = c->fn(c->ctx);
            if (c->result != AX_OK) {
                const ax_error *e = ax_last_error();
                size_t n =
                    e->message.len < sizeof c->message - 1 ? e->message.len : sizeof c->message - 1;
                memcpy(c->message, e->message.ptr, n);
                c->message[n] = '\0';
            }
        } else {
            c->result = result;
            static const char gone[] = "the ax_app was destroyed before the call could run";
            memcpy(c->message, gone, sizeof gone);
        }
        ax_atomic_store_u32(&c->done, 1, AX_MO_RELEASE);
        any = true;
    }
    if (any) {
        ax_mutex_lock(&app->cmd_lock);
        ax_cond_broadcast(&app->cmd_cond);
        ax_mutex_unlock(&app->cmd_lock);
    }
}

void ax__app_run_commands(ax_app_impl *app)
{
    if (ax_atomic_load_ptr(&app->commands, AX_MO_RELAXED) != NULL) {
        finish_commands(app, true, AX_OK);
    }
}

void ax__app_wake_presenters(ax_app_impl *app)
{
    ax_atomic_fetch_add_u64(&app->wake_gen, 1, AX_MO_SEQ_CST);
    if (ax_atomic_load_u32(&app->presenters_waiting, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&app->wake_lock);
        ax_cond_broadcast(&app->wake_cond);
        ax_mutex_unlock(&app->wake_lock);
    }
}

/* ---- create / destroy --------------------------------------------------------------------------
 */

static void wake_main_from_consumer(void *ctx)
{
    ax_app_impl *app = (ax_app_impl *)ctx;
    app->ops->wake(app);
}

static const ax_backend_ops *pick_backend(void)
{
    return &ax_wayland_backend;
}

static void free_app(ax_app_impl *app)
{
    uint32_t i;
    ax_allocator a = app->allocator;
    if (app->windows != NULL) {
        for (i = 0; i < app->max_windows; ++i) {
            ax_mutex_deinit(&app->windows[i].info_lock);
        }
        ax_mem_free(&a, app->windows, sizeof(ax_window_impl) * app->max_windows);
    }
    ax_evq_deinit(&app->events);
    if (app->app_id != NULL) {
        ax_mem_free(&a, app->app_id, strlen(app->app_id) + 1);
    }
    ax_cond_deinit(&app->wake_cond);
    ax_mutex_deinit(&app->wake_lock);
    ax_cond_deinit(&app->cmd_cond);
    ax_mutex_deinit(&app->cmd_lock);
    ax_mem_free(&a, app, sizeof *app);
}

ax_result ax_app_create(const ax_app_desc *desc, ax_app *out_app)
{
    ax_app_impl *app;
    uint32_t i;
    uint32_t slot;
    ax_result r;

    if (out_app == NULL || desc == NULL || desc->allocator == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_app_create: desc, allocator and out_app "
                                                   "are required");
    }
    out_app->h = 0;
    if (desc->struct_size < sizeof(ax_app_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_app_create: struct_size %u < %u",
                            desc->struct_size, (unsigned)sizeof(ax_app_desc));
    }

    app = AX_MEM_NEW(desc->allocator, ax_app_impl);
    if (app == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_app_create: out of memory");
    }
    memset(app, 0, sizeof *app);
    app->allocator = *desc->allocator;
    app->main_thread = ax_thread_current_id();
    app->ops = pick_backend();
    app->max_windows = desc->max_windows != 0 ? desc->max_windows : 32u;
    if (app->max_windows > 0xFFFFu) {
        app->max_windows = 0xFFFFu; /* window slots are 16 bits in a handle */
    }
    ax_mutex_init(&app->cmd_lock);
    ax_cond_init(&app->cmd_cond);
    ax_mutex_init(&app->wake_lock);
    ax_cond_init(&app->wake_cond);
    ax_atomic_init_ptr(&app->commands, NULL);
    ax_atomic_init_u32(&app->quit, 0);
    ax_atomic_init_u64(&app->wake_gen, 0);
    ax_atomic_init_u32(&app->presenters_waiting, 0);

    app->app_id = (char *)ax_mem_alloc(&app->allocator, desc->app_id.len + 1, 1);
    app->windows = (ax_window_impl *)ax_mem_alloc(
        &app->allocator, sizeof(ax_window_impl) * app->max_windows, AX_ALIGNOF(ax_window_impl));
    if (app->app_id == NULL || app->windows == NULL ||
        ax_evq_init(&app->events, &app->allocator,
                    desc->event_capacity ? desc->event_capacity : 256u, app->max_windows,
                    wake_main_from_consumer, app) != AX_OK) {
        free_app(app);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_app_create: out of memory");
    }
    if (desc->app_id.len != 0) {
        memcpy(app->app_id, desc->app_id.ptr, desc->app_id.len);
    }
    app->app_id[desc->app_id.len] = '\0';
    memset(app->windows, 0, sizeof(ax_window_impl) * app->max_windows);
    for (i = 0; i < app->max_windows; ++i) {
        ax_window_impl *w = &app->windows[i];
        w->app = app;
        w->index = i;
        ax_atomic_init_u64(&w->handle, 0);
        ax_atomic_init_u32(&w->commit_requested, 0);
        ax_atomic_init_u32(&w->presenting, 0);
        ax_mutex_init(&w->info_lock);
    }

    r = app->ops->app_init(app, desc);
    if (r != AX_OK) {
        free_app(app); /* app_init recorded the error */
        return r;
    }

    /* Claim a table slot. */
    app->generation = ax_atomic_fetch_add_u32(&g_app_generation, 1, AX_MO_RELAXED);
    if (app->generation == 0) {
        app->generation = ax_atomic_fetch_add_u32(&g_app_generation, 1, AX_MO_RELAXED);
    }
    for (slot = 0; slot < AX_MAX_APPS; ++slot) {
        void *expected = NULL;
        if (ax_atomic_compare_exchange_ptr(&g_apps[slot], &expected, app, AX_MO_ACQ_REL,
                                           AX_MO_RELAXED)) {
            break;
        }
    }
    if (slot == AX_MAX_APPS) {
        app->ops->app_shutdown(app);
        free_app(app);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_app_create: at most %u apps may exist",
                            AX_MAX_APPS);
    }
    app->slot = slot;
    out_app->h = ax_handle_make(slot, app->generation);
    return AX_OK;
}

ax_result ax_app_destroy(ax_app app)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    uint32_t i;
    if (impl == NULL) {
        return invalid_app("ax_app_destroy");
    }
    AX_REQUIRE_MAIN(impl, "ax_app_destroy");

    /* Windows still open are destroyed with the app. */
    for (i = 0; i < impl->max_windows; ++i) {
        if (impl->windows[i].open) {
            ax__window_destroy_on_main(&impl->windows[i]);
        }
    }
    ax_atomic_store_ptr(&g_apps[impl->slot], NULL, AX_MO_RELEASE);
    /* Callers blocked on a command must not hang. */
    finish_commands(impl, false, AX_ERR_INVALID_HANDLE);
    impl->ops->app_shutdown(impl);
    free_app(impl);
    return AX_OK;
}

/* ---- pump -------------------------------------------------------------------------------------
 */

ax_result ax_app_pump(ax_app app, uint64_t timeout_ns)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    if (impl == NULL) {
        return invalid_app("ax_app_pump");
    }
    AX_REQUIRE_MAIN(impl, "ax_app_pump");
    return impl->ops->pump(impl, timeout_ns);
}

ax_result ax_app_run(ax_app app)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    if (impl == NULL) {
        return invalid_app("ax_app_run");
    }
    AX_REQUIRE_MAIN(impl, "ax_app_run");
    while (ax_atomic_load_u32(&impl->quit, AX_MO_ACQUIRE) == 0) {
        ax_result r = impl->ops->pump(impl, UINT64_MAX);
        if (r != AX_OK) {
            return r;
        }
    }
    ax_atomic_store_u32(&impl->quit, 0, AX_MO_RELAXED);
    return AX_OK;
}

ax_result ax_app_quit(ax_app app)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    if (impl == NULL) {
        return invalid_app("ax_app_quit");
    }
    ax_atomic_store_u32(&impl->quit, 1, AX_MO_RELEASE);
    impl->ops->wake(impl);
    return AX_OK;
}

uint64_t ax_app_caps(ax_app app)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    return impl != NULL ? impl->caps : 0;
}

ax_backend ax_app_backend(ax_app app)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    return impl != NULL ? impl->ops->kind : AX_BACKEND_NONE;
}

/* ---- events -----------------------------------------------------------------------------------
 */

static ax_result check_event_out(ax_event *out, const char *fn)
{
    if (out == NULL || out->struct_size < sizeof(ax_event)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "%s: out_event must have struct_size >= %u", fn,
                            (unsigned)sizeof(ax_event));
    }
    return AX_OK;
}

ax_result ax_app_poll_event(ax_app app, ax_event *out_event)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    ax_result r;
    if (impl == NULL) {
        return invalid_app("ax_app_poll_event");
    }
    r = check_event_out(out_event, "ax_app_poll_event");
    if (r != AX_OK) {
        return r;
    }
    if (!ax_evq_pop(&impl->events, out_event)) {
        out_event->type = AX_EVENT_NONE;
    }
    return AX_OK;
}

ax_result ax_app_wait_event(ax_app app, ax_event *out_event, uint64_t timeout_ns)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    ax_result r;
    if (impl == NULL) {
        return invalid_app("ax_app_wait_event");
    }
    r = check_event_out(out_event, "ax_app_wait_event");
    if (r != AX_OK) {
        return r;
    }
    if (!ax_evq_wait(&impl->events, out_event, timeout_ns)) {
        out_event->type = AX_EVENT_NONE;
    }
    return AX_OK;
}
