/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_thread.h"

static int return_arg(void *arg)
{
    return *(int *)arg * 2;
}

AX_TEST(join_returns_thread_result)
{
    ax_thread t;
    int value = 21;
    AX_REQUIRE(ax_threads_supported());
    AX_REQUIRE(ax_thread_create(&t, return_arg, &value) == AX_OK);
    AX_CHECK_EQ_I(ax_thread_join(&t), 42);
}

/* ---- mutex + condition: a bounded queue with producers and consumers ------------------------- */

enum { QUEUE_CAP = 8, PRODUCERS = 4, CONSUMERS = 4, ITEMS_PER_PRODUCER = 5000 };

typedef struct queue {
    ax_mutex lock;
    ax_cond not_empty;
    ax_cond not_full;
    int items[QUEUE_CAP];
    int head;
    int count;
    int producers_left;
    long long consumed_sum;
    int consumed_count;
} queue;

static int produce(void *arg)
{
    queue *q = (queue *)arg;
    int i;
    for (i = 1; i <= ITEMS_PER_PRODUCER; ++i) {
        ax_mutex_lock(&q->lock);
        while (q->count == QUEUE_CAP) {
            ax_cond_wait(&q->not_full, &q->lock);
        }
        q->items[(q->head + q->count) % QUEUE_CAP] = i;
        q->count++;
        ax_cond_signal(&q->not_empty);
        ax_mutex_unlock(&q->lock);
    }
    ax_mutex_lock(&q->lock);
    q->producers_left--;
    ax_cond_broadcast(&q->not_empty); /* wake consumers so they can see the end */
    ax_mutex_unlock(&q->lock);
    return 0;
}

static int consume(void *arg)
{
    queue *q = (queue *)arg;
    for (;;) {
        int item;
        ax_mutex_lock(&q->lock);
        while (q->count == 0 && q->producers_left > 0) {
            ax_cond_wait(&q->not_empty, &q->lock);
        }
        if (q->count == 0) {
            ax_mutex_unlock(&q->lock);
            return 0;
        }
        item = q->items[q->head];
        q->head = (q->head + 1) % QUEUE_CAP;
        q->count--;
        q->consumed_sum += item;
        q->consumed_count++;
        ax_cond_signal(&q->not_full);
        ax_mutex_unlock(&q->lock);
    }
}

AX_TEST(mutex_with_condition_producer_consumer)
{
    static queue q; /* large-ish; keep it off the stack */
    ax_thread producers[PRODUCERS];
    ax_thread consumers[CONSUMERS];
    int i;

    ax_mutex_init(&q.lock);
    ax_cond_init(&q.not_empty);
    ax_cond_init(&q.not_full);
    q.head = q.count = 0;
    q.producers_left = PRODUCERS;
    q.consumed_sum = 0;
    q.consumed_count = 0;

    for (i = 0; i < CONSUMERS; ++i) {
        AX_REQUIRE(ax_thread_create(&consumers[i], consume, &q) == AX_OK);
    }
    for (i = 0; i < PRODUCERS; ++i) {
        AX_REQUIRE(ax_thread_create(&producers[i], produce, &q) == AX_OK);
    }
    for (i = 0; i < PRODUCERS; ++i) {
        ax_thread_join(&producers[i]);
    }
    for (i = 0; i < CONSUMERS; ++i) {
        ax_thread_join(&consumers[i]);
    }

    AX_CHECK_EQ_I(q.consumed_count, PRODUCERS * ITEMS_PER_PRODUCER);
    AX_CHECK_EQ_I(q.consumed_sum,
                  (long long)PRODUCERS * ITEMS_PER_PRODUCER * (ITEMS_PER_PRODUCER + 1) / 2);
    AX_CHECK_EQ_I(q.count, 0);
    ax_cond_deinit(&q.not_full);
    ax_cond_deinit(&q.not_empty);
    ax_mutex_deinit(&q.lock);
}

/* ---- timed waits ------------------------------------------------------------------------------
 */

AX_TEST(wait_timeout_expires)
{
    ax_mutex m;
    ax_cond c;
    uint64_t start;
    uint64_t elapsed;
    const uint64_t timeout = 20u * 1000000u; /* 20 ms */
    bool woke;
    int tries = 0;

    ax_mutex_init(&m);
    ax_cond_init(&c);
    ax_mutex_lock(&m);
    start = ax_time_monotonic_ns();
    /* Nobody signals; tolerate a few spurious wakeups. */
    do {
        woke = ax_cond_wait_timeout(&c, &m, timeout);
    } while (woke && ++tries < 100);
    elapsed = ax_time_monotonic_ns() - start;
    ax_mutex_unlock(&m);

    AX_CHECK(!woke);
    AX_CHECK(elapsed >= timeout); /* false guarantees the full timeout elapsed */
    ax_cond_deinit(&c);
    ax_mutex_deinit(&m);
}

typedef struct signal_state {
    ax_mutex m;
    ax_cond c;
    int flag;
} signal_state;

static int set_flag_and_signal(void *arg)
{
    signal_state *s = (signal_state *)arg;
    ax_mutex_lock(&s->m);
    s->flag = 1;
    ax_cond_signal(&s->c);
    ax_mutex_unlock(&s->m);
    return 0;
}

/* The bounded-wait pattern the main thread uses during live resize: wait for a condition, but
 * never longer than a deadline. */
AX_TEST(wait_timeout_returns_true_when_signaled)
{
    signal_state s;
    ax_thread t;
    const uint64_t deadline = ax_time_monotonic_ns() + 10ull * 1000000000u; /* 10 s, never hit */
    bool signaled = false;

    ax_mutex_init(&s.m);
    ax_cond_init(&s.c);
    s.flag = 0;

    ax_mutex_lock(&s.m);
    AX_REQUIRE(ax_thread_create(&t, set_flag_and_signal, &s) == AX_OK);
    while (!s.flag) {
        uint64_t now = ax_time_monotonic_ns();
        if (now >= deadline) {
            break;
        }
        signaled = ax_cond_wait_timeout(&s.c, &s.m, deadline - now) || signaled;
    }
    ax_mutex_unlock(&s.m);
    ax_thread_join(&t);

    AX_CHECK_EQ_I(s.flag, 1);
    AX_CHECK(signaled);
    ax_cond_deinit(&s.c);
    ax_mutex_deinit(&s.m);
}

AX_TEST(try_lock)
{
    ax_mutex m;
    ax_mutex_init(&m);
    AX_CHECK(ax_mutex_try_lock(&m));
    ax_mutex_unlock(&m);
    ax_mutex_lock(&m);
    ax_mutex_unlock(&m);
    ax_mutex_deinit(&m);
}

static int try_lock_from_other_thread(void *arg)
{
    return ax_mutex_try_lock((ax_mutex *)arg) ? 1 : 0;
}

AX_TEST(try_lock_fails_while_held_elsewhere)
{
    ax_mutex m;
    ax_thread t;
    ax_mutex_init(&m);
    ax_mutex_lock(&m);
    AX_REQUIRE(ax_thread_create(&t, try_lock_from_other_thread, &m) == AX_OK);
    AX_CHECK_EQ_I(ax_thread_join(&t), 0);
    ax_mutex_unlock(&m);
    ax_mutex_deinit(&m);
}

AX_TEST(monotonic_clock_advances)
{
    uint64_t a = ax_time_monotonic_ns();
    uint64_t b = ax_time_monotonic_ns();
    AX_CHECK(b >= a);
}

/* ---- per-thread error records -----------------------------------------------------------------
 */

static int set_error_on_thread(void *arg)
{
    (void)arg;
    if (ax_last_error()->code != AX_OK) {
        return 1; /* a new thread starts clean */
    }
    ax_set_error(AX_ERR_DEVICE_LOST, "from worker");
    return ax_last_error()->code == AX_ERR_DEVICE_LOST ? 0 : 2;
}

AX_TEST(error_records_are_per_thread)
{
    ax_thread t;
    ax_set_error(AX_ERR_WRONG_THREAD, "from main");
    AX_REQUIRE(ax_thread_create(&t, set_error_on_thread, NULL) == AX_OK);
    AX_CHECK_EQ_I(ax_thread_join(&t), 0);
    AX_CHECK_RESULT(ax_last_error()->code, AX_ERR_WRONG_THREAD);
    ax_clear_error();
}

/* ---- thread ids -------------------------------------------------------------------------------
 */

static int report_thread_id(void *arg)
{
    uint64_t *out = (uint64_t *)arg;
    out[0] = ax_thread_current_id();
    out[1] = ax_thread_current_id();
    return 0;
}

AX_TEST(thread_ids_are_stable_and_unique)
{
    uint64_t mine = ax_thread_current_id();
    uint64_t a[2] = {0, 0};
    uint64_t b[2] = {0, 0};
    ax_thread ta;
    ax_thread tb;

    AX_CHECK(mine != 0);
    AX_CHECK_EQ_U(ax_thread_current_id(), mine);
    AX_REQUIRE(ax_thread_create(&ta, report_thread_id, a) == AX_OK);
    AX_REQUIRE(ax_thread_create(&tb, report_thread_id, b) == AX_OK);
    ax_thread_join(&ta);
    ax_thread_join(&tb);
    AX_CHECK(a[0] != 0 && a[0] == a[1]);
    AX_CHECK(b[0] != 0 && b[0] == b[1]);
    AX_CHECK(a[0] != mine && b[0] != mine && a[0] != b[0]);
}

/* ---- misuse aborts in debug builds (error-checking mutexes on POSIX, owner tracking on Win32) */

AX_DEATH_TEST(mutex_relock_from_owner_aborts)
{
    static ax_mutex m;
    ax_mutex_init(&m);
    ax_mutex_lock(&m);
    ax_mutex_lock(&m); /* would deadlock: debug builds abort instead */
}

static int unlock_foreign(void *arg)
{
    ax_mutex_unlock((ax_mutex *)arg);
    return 0;
}

AX_DEATH_TEST(mutex_unlock_from_other_thread_aborts)
{
    static ax_mutex m;
    ax_thread t;
#if AX_TEST_TSAN
    AX_SKIP("ThreadSanitizer reports this misuse itself, before AXION's check runs");
#endif
    ax_mutex_init(&m);
    ax_mutex_lock(&m);
    if (ax_thread_create(&t, unlock_foreign, &m) == AX_OK) {
        ax_thread_join(&t); /* the worker's unlock aborts the process first */
    }
}

AX_DEATH_TEST(mutex_unlock_when_unlocked_aborts)
{
    static ax_mutex m;
#if AX_TEST_TSAN
    AX_SKIP("ThreadSanitizer reports this misuse itself, before AXION's check runs");
#endif
    ax_mutex_init(&m);
    ax_mutex_unlock(&m);
}

AX_TEST(try_lock_by_owner_fails_without_aborting)
{
    ax_mutex m;
    ax_mutex_init(&m);
    ax_mutex_lock(&m);
    AX_CHECK(!ax_mutex_try_lock(&m));
    ax_mutex_unlock(&m);
    ax_mutex_deinit(&m);
}
