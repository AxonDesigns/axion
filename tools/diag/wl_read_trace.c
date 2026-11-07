/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * wl_read_trace: a development-only LD_PRELOAD library that records which threads read the Wayland
 * connection and dispatch queues (decision 38, risk 2: does the Vulkan WSI read the display fd,
 * and from which thread?). Never linked into anything AXION ships; built only with
 * -DAX_BUILD_DIAG_TOOLS=ON.
 *
 *   LD_PRELOAD=build/<preset>/tools/diag/libax_wl_read_trace.so m0_window --renderer=gpu ...
 *
 * At exit it prints, per thread (id, name, and "main" for the process's first thread), how many
 * times each libwayland entry point was called. wl_display_read_events is the call that reads the
 * socket (the last thread of a prepare_read group performs the read); the dispatch and roundtrip
 * calls show which queues a thread services.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct wl_display;
struct wl_event_queue;

enum {
    F_PREPARE_READ,
    F_PREPARE_READ_QUEUE,
    F_READ_EVENTS,
    F_CANCEL_READ,
    F_DISPATCH_QUEUE,
    F_DISPATCH_QUEUE_PENDING,
    F_DISPATCH_QUEUE_TIMEOUT,
    F_ROUNDTRIP_QUEUE,
    F_COUNT
};
static const char *const fn_names[F_COUNT] = {
    "prepare_read",   "prepare_read_queue",      "read_events",            "cancel_read",
    "dispatch_queue", "dispatch_queue_pending", "dispatch_queue_timeout", "roundtrip_queue"};

#define MAX_THREADS 64
typedef struct thread_slot {
    _Atomic long tid; /* 0 = free */
    char name[16];
    _Atomic uint64_t calls[F_COUNT];
} thread_slot;
static thread_slot slots[MAX_THREADS];

static thread_slot *slot_for_current(void)
{
    static _Thread_local thread_slot *mine;
    long tid;
    int i;
    if (mine != NULL) {
        return mine;
    }
    tid = (long)syscall(SYS_gettid);
    for (i = 0; i < MAX_THREADS; ++i) {
        long expected = 0;
        if (atomic_compare_exchange_strong(&slots[i].tid, &expected, tid)) {
            pthread_getname_np(pthread_self(), slots[i].name, sizeof slots[i].name);
            mine = &slots[i];
            return mine;
        }
    }
    return NULL; /* table full: not counted */
}

static void count(int fn)
{
    thread_slot *s = slot_for_current();
    if (s != NULL) {
        atomic_fetch_add(&s->calls[fn], 1);
    }
}

#define NEXT(ret, name, params)                       \
    static ret (*real_##name) params;                 \
    if (real_##name == NULL) {                        \
        *(void **)&real_##name = dlsym(RTLD_NEXT, #name); \
    }

int wl_display_prepare_read(struct wl_display *d)
{
    NEXT(int, wl_display_prepare_read, (struct wl_display *));
    count(F_PREPARE_READ);
    return real_wl_display_prepare_read(d);
}

int wl_display_prepare_read_queue(struct wl_display *d, struct wl_event_queue *q)
{
    NEXT(int, wl_display_prepare_read_queue, (struct wl_display *, struct wl_event_queue *));
    count(F_PREPARE_READ_QUEUE);
    return real_wl_display_prepare_read_queue(d, q);
}

int wl_display_read_events(struct wl_display *d)
{
    NEXT(int, wl_display_read_events, (struct wl_display *));
    count(F_READ_EVENTS);
    return real_wl_display_read_events(d);
}

void wl_display_cancel_read(struct wl_display *d)
{
    NEXT(void, wl_display_cancel_read, (struct wl_display *));
    count(F_CANCEL_READ);
    real_wl_display_cancel_read(d);
}

int wl_display_dispatch_queue(struct wl_display *d, struct wl_event_queue *q)
{
    NEXT(int, wl_display_dispatch_queue, (struct wl_display *, struct wl_event_queue *));
    count(F_DISPATCH_QUEUE);
    return real_wl_display_dispatch_queue(d, q);
}

int wl_display_dispatch_queue_pending(struct wl_display *d, struct wl_event_queue *q)
{
    NEXT(int, wl_display_dispatch_queue_pending, (struct wl_display *, struct wl_event_queue *));
    count(F_DISPATCH_QUEUE_PENDING);
    return real_wl_display_dispatch_queue_pending(d, q);
}

int wl_display_dispatch_queue_timeout(struct wl_display *d, struct wl_event_queue *q,
                                      const struct timespec *timeout)
{
    NEXT(int, wl_display_dispatch_queue_timeout,
         (struct wl_display *, struct wl_event_queue *, const struct timespec *));
    count(F_DISPATCH_QUEUE_TIMEOUT);
    return real_wl_display_dispatch_queue_timeout(d, q, timeout);
}

int wl_display_roundtrip_queue(struct wl_display *d, struct wl_event_queue *q)
{
    NEXT(int, wl_display_roundtrip_queue, (struct wl_display *, struct wl_event_queue *));
    count(F_ROUNDTRIP_QUEUE);
    return real_wl_display_roundtrip_queue(d, q);
}

__attribute__((destructor)) static void report(void)
{
    int i, f;
    long pid = (long)getpid();
    fprintf(stderr, "wl_read_trace: libwayland calls per thread\n");
    for (i = 0; i < MAX_THREADS; ++i) {
        long tid = atomic_load(&slots[i].tid);
        if (tid == 0) {
            continue;
        }
        fprintf(stderr, "  tid %ld%s \"%s\":", tid, tid == pid ? " (main)" : "", slots[i].name);
        for (f = 0; f < F_COUNT; ++f) {
            uint64_t n = atomic_load(&slots[i].calls[f]);
            if (n != 0) {
                fprintf(stderr, " %s=%llu", fn_names[f], (unsigned long long)n);
            }
        }
        fprintf(stderr, "\n");
    }
}
