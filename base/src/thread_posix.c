/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_thread, ax_mutex and ax_cond over pthreads (Linux, Android, macOS, iOS, Emscripten with
 * -pthread).
 */
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#    define _DARWIN_C_SOURCE /* pthread_cond_timedwait_relative_np, clock_gettime */
#endif

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_thread.h"

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#    error "thread_posix.c needs -pthread on Emscripten; single-threaded builds use thread_single.c"
#endif

typedef struct thread_impl {
    pthread_t id;
    ax_thread_fn fn;
    void *arg;
} thread_impl;

AX_STATIC_ASSERT(sizeof(thread_impl) <= AX_THREAD_STORAGE_SIZE, "ax_thread storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(thread_impl) <= 8, "ax_thread storage under-aligned");
AX_STATIC_ASSERT(sizeof(pthread_mutex_t) <= AX_THREAD_STORAGE_SIZE, "ax_mutex storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(pthread_mutex_t) <= 8, "ax_mutex storage under-aligned");
AX_STATIC_ASSERT(sizeof(pthread_cond_t) <= AX_THREAD_STORAGE_SIZE, "ax_cond storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(pthread_cond_t) <= 8, "ax_cond storage under-aligned");

/* The storage arrays are only ever accessed through these native types, never as bytes. */
#define THREAD(t) ((thread_impl *)(void *)(t)->storage)
#define MUTEX(m)  ((pthread_mutex_t *)(void *)(m)->storage)
#define COND(c)   ((pthread_cond_t *)(void *)(c)->storage)

/* pthread failures other than the documented ones mean misuse or a broken system: report and
 * abort in every build type rather than continue with a lock in an unknown state. */
static void check(int rc, const char *what)
{
    if (rc != 0) {
        fprintf(stderr, "axion: %s failed (error %d)\n", what, rc);
        fflush(stderr);
        abort();
    }
}

/* ---- Threads --------------------------------------------------------------------------------- */

bool ax_threads_supported(void)
{
    return true;
}

static void *thread_start(void *arg)
{
    thread_impl *impl = THREAD((ax_thread *)arg);
    return (void *)(intptr_t)impl->fn(impl->arg);
}

ax_result ax_thread_create(ax_thread *thread, ax_thread_fn fn, void *arg)
{
    thread_impl *impl;
    int rc;

    AX_ASSERT(thread != NULL && fn != NULL);
    impl = THREAD(thread);
    impl->fn = fn;
    impl->arg = arg;
    rc = pthread_create(&impl->id, NULL, thread_start, thread);
    if (rc != 0) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "pthread_create failed (error %d)", rc);
    }
    return AX_OK;
}

int ax_thread_join(ax_thread *thread)
{
    void *result = NULL;
    check(pthread_join(THREAD(thread)->id, &result), "pthread_join");
    return (int)(intptr_t)result;
}

void ax_thread_yield(void)
{
    sched_yield();
}

uint32_t ax_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) {
        return 1;
    }
    return n > 0xFFFF ? 0xFFFFu : (uint32_t)n;
}

/* ---- Mutexes --------------------------------------------------------------------------------- */

void ax_mutex_init(ax_mutex *mutex)
{
    pthread_mutexattr_t attr;
    check(pthread_mutexattr_init(&attr), "pthread_mutexattr_init");
#if AX_ENABLE_ASSERTS
    check(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK), "pthread_mutexattr_settype");
#endif
    check(pthread_mutex_init(MUTEX(mutex), &attr), "pthread_mutex_init");
    pthread_mutexattr_destroy(&attr);
}

void ax_mutex_deinit(ax_mutex *mutex)
{
    check(pthread_mutex_destroy(MUTEX(mutex)), "pthread_mutex_destroy");
}

void ax_mutex_lock(ax_mutex *mutex)
{
    check(pthread_mutex_lock(MUTEX(mutex)), "pthread_mutex_lock");
}

bool ax_mutex_try_lock(ax_mutex *mutex)
{
    int rc = pthread_mutex_trylock(MUTEX(mutex));
    if (rc == EBUSY) {
        return false;
    }
    check(rc, "pthread_mutex_trylock");
    return true;
}

void ax_mutex_unlock(ax_mutex *mutex)
{
    check(pthread_mutex_unlock(MUTEX(mutex)), "pthread_mutex_unlock");
}

/* ---- Condition variables --------------------------------------------------------------------- */

void ax_cond_init(ax_cond *cond)
{
#if defined(__APPLE__)
    /* Timed waits use the relative-time variant, which is immune to wall-clock changes. */
    check(pthread_cond_init(COND(cond), NULL), "pthread_cond_init");
#else
    /* Time absolute deadlines against the monotonic clock, not the adjustable wall clock. */
    pthread_condattr_t attr;
    check(pthread_condattr_init(&attr), "pthread_condattr_init");
    check(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC), "pthread_condattr_setclock");
    check(pthread_cond_init(COND(cond), &attr), "pthread_cond_init");
    pthread_condattr_destroy(&attr);
#endif
}

void ax_cond_deinit(ax_cond *cond)
{
    check(pthread_cond_destroy(COND(cond)), "pthread_cond_destroy");
}

void ax_cond_wait(ax_cond *cond, ax_mutex *mutex)
{
    check(pthread_cond_wait(COND(cond), MUTEX(mutex)), "pthread_cond_wait");
}

/* Longest wait honoured in one call; longer timeouts are clamped (callers loop anyway). */
#define AX_MAX_WAIT_SECONDS ((uint64_t)365 * 24 * 3600)

bool ax_cond_wait_timeout(ax_cond *cond, ax_mutex *mutex, uint64_t timeout_ns)
{
#if defined(__APPLE__)
    /* Relative waits: loop on the monotonic clock so that false always means timeout_ns has
     * elapsed, even if a wait reports a timeout slightly early. */
    uint64_t start = ax_time_monotonic_ns();
    uint64_t deadline = timeout_ns > UINT64_MAX - start ? UINT64_MAX : start + timeout_ns;

    for (;;) {
        uint64_t now = ax_time_monotonic_ns();
        uint64_t remaining;
        uint64_t sec;
        struct timespec rel;
        int rc;
        if (now >= deadline) {
            return false;
        }
        remaining = deadline - now;
        sec = remaining / 1000000000u;
        rel.tv_sec = (time_t)(sec > AX_MAX_WAIT_SECONDS ? AX_MAX_WAIT_SECONDS : sec);
        rel.tv_nsec = (long)(remaining % 1000000000u);
        rc = pthread_cond_timedwait_relative_np(COND(cond), MUTEX(mutex), &rel);
        if (rc != ETIMEDOUT) {
            check(rc, "pthread_cond_timedwait_relative_np");
            return true;
        }
    }
#else
    /* An absolute deadline on CLOCK_MONOTONIC: ETIMEDOUT is only reported once it has passed. */
    uint64_t sec = timeout_ns / 1000000000u;
    long nsec = (long)(timeout_ns % 1000000000u);
    struct timespec deadline;
    int rc;

    if (sec > AX_MAX_WAIT_SECONDS) {
        sec = AX_MAX_WAIT_SECONDS;
    }
    check(clock_gettime(CLOCK_MONOTONIC, &deadline) != 0 ? errno : 0, "clock_gettime");
    deadline.tv_sec += (time_t)sec;
    deadline.tv_nsec += nsec;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec += 1;
    }
    rc = pthread_cond_timedwait(COND(cond), MUTEX(mutex), &deadline);
    if (rc == ETIMEDOUT) {
        return false;
    }
    check(rc, "pthread_cond_timedwait");
    return true;
#endif
}

void ax_cond_signal(ax_cond *cond)
{
    check(pthread_cond_signal(COND(cond)), "pthread_cond_signal");
}

void ax_cond_broadcast(ax_cond *cond)
{
    check(pthread_cond_broadcast(COND(cond)), "pthread_cond_broadcast");
}

/* ---- Time ------------------------------------------------------------------------------------ */

uint64_t ax_time_monotonic_ns(void)
{
    struct timespec ts;
    check(clock_gettime(CLOCK_MONOTONIC, &ts) != 0 ? errno : 0, "clock_gettime");
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
