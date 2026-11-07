/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_thread, ax_mutex and ax_cond over Win32: threads, slim reader/writer locks (exclusive mode)
 * and condition variables. None of them needs a kernel object until contended, and none can fail
 * to initialize.
 */
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_thread.h"

typedef struct thread_impl {
    HANDLE handle;
    ax_thread_fn fn;
    void *arg;
} thread_impl;

AX_STATIC_ASSERT(sizeof(thread_impl) <= AX_THREAD_STORAGE_SIZE, "ax_thread storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(thread_impl) <= 8, "ax_thread storage under-aligned");
/* Debug builds record the owning thread so misuse aborts with a message, matching the
 * error-checking mutexes used on POSIX; SRW locks themselves detect nothing (decision 27). The
 * owner is read and written with Interlocked operations, so checking it from another thread is
 * not a data race. */
typedef struct mutex_impl {
    SRWLOCK lock;
    volatile LONG owner; /* GetCurrentThreadId() of the holder, 0 when unlocked (debug only) */
} mutex_impl;

AX_STATIC_ASSERT(sizeof(mutex_impl) <= AX_THREAD_STORAGE_SIZE, "ax_mutex storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(mutex_impl) <= 8, "ax_mutex storage under-aligned");
AX_STATIC_ASSERT(sizeof(DWORD) == sizeof(LONG), "thread ids are stored in a LONG");
AX_STATIC_ASSERT(sizeof(CONDITION_VARIABLE) <= AX_THREAD_STORAGE_SIZE, "ax_cond storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(CONDITION_VARIABLE) <= 8, "ax_cond storage under-aligned");

/* The storage arrays are only ever accessed through these native types, never as bytes. */
#define THREAD(t) ((thread_impl *)(void *)(t)->storage)
#define MUTEX(m)  ((mutex_impl *)(void *)(m)->storage)
#define COND(c)   ((CONDITION_VARIABLE *)(void *)(c)->storage)

AX_NORETURN static void fatal(const char *what)
{
    fprintf(stderr, "axion: %s failed (error %lu)\n", what, (unsigned long)GetLastError());
    fflush(stderr);
    abort();
}

#if AX_ENABLE_ASSERTS
AX_NORETURN static void misuse(const char *what)
{
    fprintf(stderr, "axion: %s\n", what);
    fflush(stderr);
    abort();
}

static DWORD owner_of(mutex_impl *m)
{
    return (DWORD)InterlockedCompareExchange(&m->owner, 0, 0);
}

static void set_owner(mutex_impl *m, DWORD owner)
{
    InterlockedExchange(&m->owner, (LONG)owner);
}

/* Before releasing the lock (unlock, or a condition wait): the caller must hold it. */
static void release_ownership(mutex_impl *m, const char *what)
{
    if (owner_of(m) != GetCurrentThreadId()) {
        misuse(what);
    }
    set_owner(m, 0);
}
#endif

/* ---- Threads --------------------------------------------------------------------------------- */

bool ax_threads_supported(void)
{
    return true;
}

static DWORD WINAPI thread_start(LPVOID arg)
{
    thread_impl *impl = THREAD((ax_thread *)arg);
    return (DWORD)impl->fn(impl->arg);
}

ax_result ax_thread_create(ax_thread *thread, ax_thread_fn fn, void *arg)
{
    thread_impl *impl;

    AX_ASSERT(thread != NULL && fn != NULL);
    impl = THREAD(thread);
    impl->fn = fn;
    impl->arg = arg;
    impl->handle = CreateThread(NULL, 0, thread_start, thread, 0, NULL);
    if (impl->handle == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "CreateThread failed (error %lu)",
                            (unsigned long)GetLastError());
    }
    return AX_OK;
}

int ax_thread_join(ax_thread *thread)
{
    thread_impl *impl = THREAD(thread);
    DWORD code = 0;
    if (WaitForSingleObject(impl->handle, INFINITE) != WAIT_OBJECT_0) {
        fatal("WaitForSingleObject");
    }
    if (!GetExitCodeThread(impl->handle, &code)) {
        fatal("GetExitCodeThread");
    }
    CloseHandle(impl->handle);
    impl->handle = NULL;
    return (int)code;
}

void ax_thread_yield(void)
{
    SwitchToThread();
}

/* ---- Mutexes --------------------------------------------------------------------------------- */

void ax_mutex_init(ax_mutex *mutex)
{
    InitializeSRWLock(&MUTEX(mutex)->lock);
    MUTEX(mutex)->owner = 0;
}

void ax_mutex_deinit(ax_mutex *mutex)
{
#if AX_ENABLE_ASSERTS
    if (owner_of(MUTEX(mutex)) != 0) {
        misuse("ax_mutex_deinit: the mutex is still locked");
    }
#endif
    (void)mutex; /* SRW locks own no resources */
}

void ax_mutex_lock(ax_mutex *mutex)
{
#if AX_ENABLE_ASSERTS
    if (owner_of(MUTEX(mutex)) == GetCurrentThreadId()) {
        misuse("ax_mutex_lock: this thread already holds the mutex (relocking would deadlock)");
    }
#endif
    AcquireSRWLockExclusive(&MUTEX(mutex)->lock);
#if AX_ENABLE_ASSERTS
    set_owner(MUTEX(mutex), GetCurrentThreadId());
#endif
}

bool ax_mutex_try_lock(ax_mutex *mutex)
{
    /* Like an error-checking pthread mutex, a try-lock by the holder simply fails. */
    if (!TryAcquireSRWLockExclusive(&MUTEX(mutex)->lock)) {
        return false;
    }
#if AX_ENABLE_ASSERTS
    set_owner(MUTEX(mutex), GetCurrentThreadId());
#endif
    return true;
}

void ax_mutex_unlock(ax_mutex *mutex)
{
#if AX_ENABLE_ASSERTS
    release_ownership(MUTEX(mutex), "ax_mutex_unlock: the calling thread does not hold the mutex");
#endif
    ReleaseSRWLockExclusive(&MUTEX(mutex)->lock);
}

/* ---- Condition variables --------------------------------------------------------------------- */

void ax_cond_init(ax_cond *cond)
{
    InitializeConditionVariable(COND(cond));
}

void ax_cond_deinit(ax_cond *cond)
{
    (void)cond; /* condition variables own no resources */
}

void ax_cond_wait(ax_cond *cond, ax_mutex *mutex)
{
#if AX_ENABLE_ASSERTS
    release_ownership(MUTEX(mutex), "ax_cond_wait: the calling thread does not hold the mutex");
#endif
    if (!SleepConditionVariableSRW(COND(cond), &MUTEX(mutex)->lock, INFINITE, 0)) {
        fatal("SleepConditionVariableSRW");
    }
#if AX_ENABLE_ASSERTS
    set_owner(MUTEX(mutex), GetCurrentThreadId());
#endif
}

bool ax_cond_wait_timeout(ax_cond *cond, ax_mutex *mutex, uint64_t timeout_ns)
{
    /* SleepConditionVariableSRW can report a timeout up to one timer tick early, so wait again
     * until the deadline has really passed: false must mean timeout_ns elapsed. */
    uint64_t start = ax_time_monotonic_ns();
    uint64_t deadline = timeout_ns > UINT64_MAX - start ? UINT64_MAX : start + timeout_ns;

    for (;;) {
        uint64_t now = ax_time_monotonic_ns();
        uint64_t remaining;
        uint64_t ms;
        BOOL woke;
        if (now >= deadline) {
            return false;
        }
        /* Round up to whole milliseconds, staying below INFINITE (0xFFFFFFFF). */
        remaining = deadline - now;
        ms = remaining / 1000000u + (remaining % 1000000u != 0 ? 1u : 0u);
        if (ms > 0xFFFFFFFEu) {
            ms = 0xFFFFFFFEu;
        }
#if AX_ENABLE_ASSERTS
        release_ownership(MUTEX(mutex),
                          "ax_cond_wait_timeout: the calling thread does not hold the mutex");
#endif
        woke = SleepConditionVariableSRW(COND(cond), &MUTEX(mutex)->lock, (DWORD)ms, 0);
#if AX_ENABLE_ASSERTS
        set_owner(MUTEX(mutex), GetCurrentThreadId());
#endif
        if (woke) {
            return true;
        }
        if (GetLastError() != ERROR_TIMEOUT) {
            fatal("SleepConditionVariableSRW");
        }
    }
}

void ax_cond_signal(ax_cond *cond)
{
    WakeConditionVariable(COND(cond));
}

void ax_cond_broadcast(ax_cond *cond)
{
    WakeAllConditionVariable(COND(cond));
}

/* ---- Time ------------------------------------------------------------------------------------ */

uint64_t ax_time_monotonic_ns(void)
{
    LARGE_INTEGER frequency; /* fixed at boot; cheap to query, and avoids a shared cache */
    LARGE_INTEGER counter;
    uint64_t c;
    uint64_t f;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    c = (uint64_t)counter.QuadPart;
    f = (uint64_t)frequency.QuadPart;
    /* Split to avoid overflowing c * 1e9. */
    return (c / f) * 1000000000u + ((c % f) * 1000000000u) / f;
}
