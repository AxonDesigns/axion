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
AX_STATIC_ASSERT(sizeof(SRWLOCK) <= AX_THREAD_STORAGE_SIZE, "ax_mutex storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(SRWLOCK) <= 8, "ax_mutex storage under-aligned");
AX_STATIC_ASSERT(sizeof(CONDITION_VARIABLE) <= AX_THREAD_STORAGE_SIZE, "ax_cond storage too small");
AX_STATIC_ASSERT(AX_ALIGNOF(CONDITION_VARIABLE) <= 8, "ax_cond storage under-aligned");

/* The storage arrays are only ever accessed through these native types, never as bytes. */
#define THREAD(t) ((thread_impl *)(void *)(t)->storage)
#define MUTEX(m)  ((SRWLOCK *)(void *)(m)->storage)
#define COND(c)   ((CONDITION_VARIABLE *)(void *)(c)->storage)

AX_NORETURN static void fatal(const char *what)
{
    fprintf(stderr, "axion: %s failed (error %lu)\n", what, (unsigned long)GetLastError());
    fflush(stderr);
    abort();
}

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
    InitializeSRWLock(MUTEX(mutex));
}

void ax_mutex_deinit(ax_mutex *mutex)
{
    (void)mutex; /* SRW locks own no resources */
}

void ax_mutex_lock(ax_mutex *mutex)
{
    AcquireSRWLockExclusive(MUTEX(mutex));
}

bool ax_mutex_try_lock(ax_mutex *mutex)
{
    return TryAcquireSRWLockExclusive(MUTEX(mutex)) != 0;
}

void ax_mutex_unlock(ax_mutex *mutex)
{
    ReleaseSRWLockExclusive(MUTEX(mutex));
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
    if (!SleepConditionVariableSRW(COND(cond), MUTEX(mutex), INFINITE, 0)) {
        fatal("SleepConditionVariableSRW");
    }
}

bool ax_cond_wait_timeout(ax_cond *cond, ax_mutex *mutex, uint64_t timeout_ns)
{
    /* Round up to whole milliseconds, staying below INFINITE (0xFFFFFFFF). */
    uint64_t ms = timeout_ns / 1000000u + (timeout_ns % 1000000u != 0 ? 1u : 0u);
    if (ms > 0xFFFFFFFEu) {
        ms = 0xFFFFFFFEu;
    }
    if (SleepConditionVariableSRW(COND(cond), MUTEX(mutex), (DWORD)ms, 0)) {
        return true;
    }
    if (GetLastError() == ERROR_TIMEOUT) {
        return false;
    }
    fatal("SleepConditionVariableSRW");
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
