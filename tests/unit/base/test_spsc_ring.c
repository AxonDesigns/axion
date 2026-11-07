/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_spsc_ring.h"
#include "axion/base/ax_thread.h"

typedef struct record {
    uint64_t seq;
    uint64_t check; /* derived from seq: a torn element would not match */
    uint32_t extra[3];
} record;

static uint64_t check_of(uint64_t seq)
{
    return seq * 0x9E3779B97F4A7C15ull ^ 0xABCDEFull;
}

AX_TEST(fills_drains_and_wraps)
{
    ax_spsc_ring r;
    uint64_t v;
    uint64_t i;
    uint64_t round;
    AX_REQUIRE(ax_spsc_ring_init(&r, NULL, 5, sizeof(uint64_t)) == AX_OK);
    AX_CHECK_EQ_U(ax_spsc_ring_capacity(&r), 8);
    AX_CHECK(!ax_spsc_ring_pop(&r, &v));
    for (round = 0; round < 10; ++round) { /* indices wrap many times */
        for (i = 0; i < 8; ++i) {
            v = round * 100 + i;
            AX_CHECK(ax_spsc_ring_push(&r, &v));
        }
        v = 999;
        AX_CHECK(!ax_spsc_ring_push(&r, &v)); /* full */
        AX_CHECK_EQ_U(ax_spsc_ring_count(&r), 8);
        for (i = 0; i < 8; ++i) {
            AX_REQUIRE(ax_spsc_ring_pop(&r, &v));
            AX_CHECK_EQ_U(v, round * 100 + i);
        }
        AX_CHECK(!ax_spsc_ring_pop(&r, &v));
    }
    ax_spsc_ring_deinit(&r);
}

enum { STRESS_COUNT = 200000 };

static int producer_main(void *arg)
{
    ax_spsc_ring *r = (ax_spsc_ring *)arg;
    uint64_t seq;
    for (seq = 1; seq <= STRESS_COUNT; ++seq) {
        record rec;
        memset(&rec, 0, sizeof rec);
        rec.seq = seq;
        rec.check = check_of(seq);
        rec.extra[1] = (uint32_t)seq;
        while (!ax_spsc_ring_push(r, &rec)) {
            ax_thread_yield();
        }
    }
    return 0;
}

AX_TEST(two_threads_keep_order_and_never_tear)
{
    ax_spsc_ring r;
    ax_thread t;
    uint64_t expected = 1;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    AX_REQUIRE(ax_spsc_ring_init(&r, NULL, 64, sizeof(record)) == AX_OK);
    AX_REQUIRE(ax_thread_create(&t, producer_main, &r) == AX_OK);
    while (expected <= STRESS_COUNT) {
        record rec;
        if (!ax_spsc_ring_pop(&r, &rec)) {
            ax_thread_yield();
            continue;
        }
        if (rec.seq != expected || rec.check != check_of(expected) ||
            rec.extra[1] != (uint32_t)expected) {
            AX_CHECK_EQ_U(rec.seq, expected);
            AX_CHECK_EQ_U(rec.check, check_of(expected));
            break;
        }
        expected++;
    }
    ax_thread_join(&t);
    AX_CHECK_EQ_U(expected, STRESS_COUNT + 1u);
    ax_spsc_ring_deinit(&r);
}
