/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"

enum { THREADS = 8, ITERATIONS = 100000 };

AX_TEST(single_thread_operations)
{
    ax_atomic_u32 a = AX_ATOMIC_INIT(5);
    ax_atomic_u64 b;
    ax_atomic_ptr p;
    int x = 0;
    int y = 0;
    uint32_t e32;
    uint64_t e64;
    void *ep;

    ax_atomic_init_u64(&b, 0xFFFFFFFFull);
    ax_atomic_init_ptr(&p, NULL);

    AX_CHECK_EQ_U(ax_atomic_load_u32(&a, AX_MO_RELAXED), 5);
    AX_CHECK_EQ_U(ax_atomic_fetch_add_u32(&a, 3, AX_MO_SEQ_CST), 5);
    ax_atomic_store_u32(&a, 0xFFFFFFFFu, AX_MO_RELEASE);
    AX_CHECK_EQ_U(ax_atomic_fetch_add_u32(&a, 1, AX_MO_RELAXED), 0xFFFFFFFFu);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&a, AX_MO_ACQUIRE), 0); /* wraps */

    /* 64-bit values really are 64 bits wide. */
    AX_CHECK_EQ_U(ax_atomic_fetch_add_u64(&b, 1, AX_MO_ACQ_REL), 0xFFFFFFFFull);
    AX_CHECK_EQ_U(ax_atomic_load_u64(&b, AX_MO_SEQ_CST), 0x100000000ull);
    ax_atomic_store_u64(&b, 0x123456789ABCDEFull, AX_MO_RELAXED);
    AX_CHECK_EQ_U(ax_atomic_load_u64(&b, AX_MO_RELAXED), 0x123456789ABCDEFull);

    e32 = 1;
    AX_CHECK(!ax_atomic_compare_exchange_u32(&a, &e32, 9, AX_MO_SEQ_CST, AX_MO_RELAXED));
    AX_CHECK_EQ_U(e32, 0); /* failure reports the current value */
    AX_CHECK(ax_atomic_compare_exchange_u32(&a, &e32, 9, AX_MO_ACQ_REL, AX_MO_ACQUIRE));
    AX_CHECK_EQ_U(ax_atomic_load_u32(&a, AX_MO_RELAXED), 9);

    e64 = 0;
    AX_CHECK(!ax_atomic_compare_exchange_u64(&b, &e64, 1, AX_MO_SEQ_CST, AX_MO_SEQ_CST));
    AX_CHECK_EQ_U(e64, 0x123456789ABCDEFull);
    AX_CHECK(ax_atomic_compare_exchange_u64(&b, &e64, 0xF000000000000000ull, AX_MO_SEQ_CST,
                                            AX_MO_SEQ_CST));
    AX_CHECK_EQ_U(ax_atomic_load_u64(&b, AX_MO_RELAXED), 0xF000000000000000ull);

    ep = &y;
    AX_CHECK(!ax_atomic_compare_exchange_ptr(&p, &ep, &x, AX_MO_SEQ_CST, AX_MO_RELAXED));
    AX_CHECK(ep == NULL);
    AX_CHECK(ax_atomic_compare_exchange_ptr(&p, &ep, &x, AX_MO_RELEASE, AX_MO_RELAXED));
    AX_CHECK(ax_atomic_load_ptr(&p, AX_MO_ACQUIRE) == &x);
    ax_atomic_store_ptr(&p, &y, AX_MO_SEQ_CST);
    AX_CHECK(ax_atomic_load_ptr(&p, AX_MO_RELAXED) == &y);
}

typedef struct counters {
    ax_atomic_u32 c32;
    ax_atomic_u64 c64;
    ax_atomic_u64 cas64; /* incremented with a compare-exchange loop */
} counters;

static int increment_worker(void *arg)
{
    counters *c = (counters *)arg;
    int i;
    for (i = 0; i < ITERATIONS; ++i) {
        uint64_t seen = ax_atomic_load_u64(&c->cas64, AX_MO_RELAXED);
        ax_atomic_fetch_add_u32(&c->c32, 1, AX_MO_RELAXED);
        ax_atomic_fetch_add_u64(&c->c64, 1, AX_MO_ACQ_REL);
        while (!ax_atomic_compare_exchange_u64(&c->cas64, &seen, seen + 1, AX_MO_ACQ_REL,
                                               AX_MO_RELAXED)) {
        }
    }
    return 0;
}

AX_TEST(counter_incremented_from_many_threads)
{
    counters c;
    ax_thread threads[THREADS];
    int i;

    ax_atomic_init_u32(&c.c32, 0);
    ax_atomic_init_u64(&c.c64, 0);
    ax_atomic_init_u64(&c.cas64, 0);
    AX_REQUIRE(ax_threads_supported());

    for (i = 0; i < THREADS; ++i) {
        AX_REQUIRE(ax_thread_create(&threads[i], increment_worker, &c) == AX_OK);
    }
    for (i = 0; i < THREADS; ++i) {
        AX_CHECK_EQ_I(ax_thread_join(&threads[i]), 0);
    }
    AX_CHECK_EQ_U(ax_atomic_load_u32(&c.c32, AX_MO_SEQ_CST), (uint32_t)THREADS * ITERATIONS);
    AX_CHECK_EQ_U(ax_atomic_load_u64(&c.c64, AX_MO_SEQ_CST), (uint64_t)THREADS * ITERATIONS);
    AX_CHECK_EQ_U(ax_atomic_load_u64(&c.cas64, AX_MO_SEQ_CST), (uint64_t)THREADS * ITERATIONS);
}

/* Release/acquire message passing: data written before a release store is visible after an
 * acquire load that observes it. */
typedef struct mailbox {
    uint64_t payload[4]; /* plain memory, published through `ready` */
    ax_atomic_u32 ready;
} mailbox;

static int producer(void *arg)
{
    mailbox *m = (mailbox *)arg;
    m->payload[0] = 11;
    m->payload[1] = 22;
    m->payload[2] = 33;
    m->payload[3] = 44;
    ax_atomic_store_u32(&m->ready, 1, AX_MO_RELEASE);
    return 0;
}

AX_TEST(release_acquire_publishes_data)
{
    int round;
    for (round = 0; round < 200; ++round) {
        mailbox m = {{0, 0, 0, 0}, AX_ATOMIC_INIT(0)};
        ax_thread t;
        AX_REQUIRE(ax_thread_create(&t, producer, &m) == AX_OK);
        while (ax_atomic_load_u32(&m.ready, AX_MO_ACQUIRE) == 0) {
            ax_thread_yield();
        }
        AX_REQUIRE(m.payload[0] == 11 && m.payload[1] == 22 && m.payload[2] == 33 &&
                   m.payload[3] == 44);
        ax_thread_join(&t);
    }
}
