/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: threads, mutexes and condition variables over pthreads and Win32.
 * (<threads.h> is missing on macOS and older MSVC.)
 *
 * ax_thread, ax_mutex and ax_cond are embeddable structs with fixed-size opaque storage, checked
 * against the native types with _Static_assert in each implementation. No OS header leaks into
 * this one, initialization never allocates and never fails, and objects must not be moved or
 * copied once initialized (or, for a thread, between create and join).
 *
 * Mutexes are not recursive. Debug builds on POSIX use error-checking mutexes, so relocking from
 * the owning thread or unlocking from another thread aborts instead of deadlocking.
 *
 * Implementations: thread_win32.c (Windows), thread_posix.c (everything with pthreads) and
 * thread_single.c (Emscripten built without -pthread: the cooperative single-threaded web mode).
 *
 * Without threads (ax_threads_supported() returns false): ax_thread_create returns
 * AX_ERR_UNSUPPORTED, and mutexes work for the one thread there is. Condition-variable waits do
 * not exist in that mode: no other thread could ever signal, so ax_cond_wait and
 * ax_cond_wait_timeout are usage errors (debug builds assert; release builds return as a spurious
 * wakeup, with ax_cond_wait_timeout returning true). Code shared with the single-threaded build
 * checks ax_threads_supported() before waiting.
 */
#ifndef AX_BASE_THREAD_H
#define AX_BASE_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_types.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

/* 64 bytes: the largest native object they wrap is pthread_mutex_t on macOS (64 bytes). */
#define AX_THREAD_STORAGE_SIZE 64

typedef struct ax_thread {
    AX_ALIGNAS(8) unsigned char storage[AX_THREAD_STORAGE_SIZE];
} ax_thread;

typedef struct ax_mutex {
    AX_ALIGNAS(8) unsigned char storage[AX_THREAD_STORAGE_SIZE];
} ax_mutex;

typedef struct ax_cond {
    AX_ALIGNAS(8) unsigned char storage[AX_THREAD_STORAGE_SIZE];
} ax_cond;

/* ---- Threads --------------------------------------------------------------------------------- */

/* Entry point of a thread; its return value is what ax_thread_join returns. */
typedef int (*ax_thread_fn)(void *arg);

/* False in builds without threads (single-threaded web). */
AX_API bool ax_threads_supported(void);

/*
 * Starts fn(arg) on a new thread. thread must stay at the same address until joined.
 * Errors: AX_ERR_UNSUPPORTED without thread support, AX_ERR_OUT_OF_MEMORY when the OS refuses
 * (resource limits); both record ax_last_error.
 */
AX_API ax_result ax_thread_create(ax_thread *thread, ax_thread_fn fn, void *arg);

/* Waits for the thread to finish and returns fn's result. Every created thread is joined once. */
AX_API int ax_thread_join(ax_thread *thread);

/* Gives up the rest of the calling thread's time slice. */
AX_API void ax_thread_yield(void);

/*
 * A nonzero id for the calling thread, unique for the life of the process (ids are never reused,
 * unlike OS thread ids). Cheap after the first call on a thread. Used for thread-affinity checks.
 * Thread: any.
 */
AX_API uint64_t ax_thread_current_id(void);

/* ---- Mutexes --------------------------------------------------------------------------------- */

AX_API void ax_mutex_init(ax_mutex *mutex);
AX_API void ax_mutex_deinit(ax_mutex *mutex); /* must be unlocked */
AX_API void ax_mutex_lock(ax_mutex *mutex);
AX_API bool ax_mutex_try_lock(ax_mutex *mutex); /* true if the lock was taken */
AX_API void ax_mutex_unlock(ax_mutex *mutex);

/* ---- Condition variables --------------------------------------------------------------------- */

AX_API void ax_cond_init(ax_cond *cond);
AX_API void ax_cond_deinit(ax_cond *cond); /* no thread may be waiting */

/* Atomically unlocks mutex (held by the caller) and waits; relocks before returning. May wake
 * spuriously: always wait in a loop that rechecks the condition. */
AX_API void ax_cond_wait(ax_cond *cond, ax_mutex *mutex);

/*
 * Like ax_cond_wait, but with a timeout. Returns true if woken (by a signal, or spuriously) and
 * false once at least timeout_ns nanoseconds have elapsed on the monotonic clock; either way
 * mutex is held again on return. A timeout is not an error. Waits may overshoot by the OS clock's
 * resolution (milliseconds on Windows), never undershoot. Loops that wait for a condition with a
 * deadline recompute the remaining time from ax_time_monotonic_ns() after each wake.
 */
AX_API bool ax_cond_wait_timeout(ax_cond *cond, ax_mutex *mutex, uint64_t timeout_ns);

AX_API void ax_cond_signal(ax_cond *cond);
AX_API void ax_cond_broadcast(ax_cond *cond);

/* ---- Time ------------------------------------------------------------------------------------ */

/* Nanoseconds from a monotonic clock with an arbitrary origin, for timeouts and deadlines. */
AX_API uint64_t ax_time_monotonic_ns(void);

AX_EXTERN_C_END

#endif /* AX_BASE_THREAD_H */
