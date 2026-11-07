/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_thread, ax_mutex and ax_cond for builds without threads: Emscripten without -pthread, the
 * cooperative single-threaded mode the web uses when the page is not cross-origin isolated.
 *
 * There is exactly one thread, so:
 *   - ax_thread_create returns AX_ERR_UNSUPPORTED (a recoverable result, not a runtime crash);
 *   - a mutex is a flag, and debug builds assert on relocking (a deadlock in a threaded build);
 *   - nothing can ever signal a condition variable: ax_cond_wait_timeout returns false at once,
 *     and ax_cond_wait asserts in debug builds, since it would wait forever.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <time.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_thread.h"

#if defined(__EMSCRIPTEN_PTHREADS__)
#    error "thread_single.c is for builds without -pthread; threaded builds use thread_posix.c"
#endif

typedef struct mutex_impl {
    int locked;
} mutex_impl;

AX_STATIC_ASSERT(sizeof(mutex_impl) <= AX_THREAD_STORAGE_SIZE, "ax_mutex storage too small");

#define MUTEX(m) ((mutex_impl *)(void *)(m)->storage)

bool ax_threads_supported(void)
{
    return false;
}

ax_result ax_thread_create(ax_thread *thread, ax_thread_fn fn, void *arg)
{
    (void)thread;
    (void)fn;
    (void)arg;
    return ax_set_error(AX_ERR_UNSUPPORTED, "threads are unavailable: built without -pthread");
}

int ax_thread_join(ax_thread *thread)
{
    (void)thread;
    AX_ASSERT_MSG(0, "ax_thread_join: no thread can have been created in this build");
    return 0;
}

void ax_thread_yield(void)
{
}

void ax_mutex_init(ax_mutex *mutex)
{
    MUTEX(mutex)->locked = 0;
}

void ax_mutex_deinit(ax_mutex *mutex)
{
    AX_ASSERT_MSG(!MUTEX(mutex)->locked, "ax_mutex_deinit: mutex is locked");
    (void)mutex;
}

void ax_mutex_lock(ax_mutex *mutex)
{
    AX_ASSERT_MSG(!MUTEX(mutex)->locked, "ax_mutex_lock: relocking would deadlock");
    MUTEX(mutex)->locked = 1;
}

bool ax_mutex_try_lock(ax_mutex *mutex)
{
    if (MUTEX(mutex)->locked) {
        return false;
    }
    MUTEX(mutex)->locked = 1;
    return true;
}

void ax_mutex_unlock(ax_mutex *mutex)
{
    AX_ASSERT_MSG(MUTEX(mutex)->locked, "ax_mutex_unlock: mutex is not locked");
    MUTEX(mutex)->locked = 0;
}

void ax_cond_init(ax_cond *cond)
{
    (void)cond;
}

void ax_cond_deinit(ax_cond *cond)
{
    (void)cond;
}

void ax_cond_wait(ax_cond *cond, ax_mutex *mutex)
{
    (void)cond;
    AX_ASSERT_MSG(MUTEX(mutex)->locked, "ax_cond_wait: mutex must be held");
    AX_ASSERT_MSG(0, "ax_cond_wait: no other thread exists to signal; this would never return");
    (void)mutex; /* release builds: behave as a spurious wakeup */
}

bool ax_cond_wait_timeout(ax_cond *cond, ax_mutex *mutex, uint64_t timeout_ns)
{
    (void)cond;
    (void)timeout_ns;
    AX_ASSERT_MSG(MUTEX(mutex)->locked, "ax_cond_wait_timeout: mutex must be held");
    (void)mutex;
    return false;
}

void ax_cond_signal(ax_cond *cond)
{
    (void)cond;
}

void ax_cond_broadcast(ax_cond *cond)
{
    (void)cond;
}

uint64_t ax_time_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
