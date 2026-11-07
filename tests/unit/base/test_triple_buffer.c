/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_triple_buffer.h"

AX_TEST(reader_sees_latest_publish)
{
    ax_triple_buffer tb;
    int slots[3] = {0, 0, 0};
    ax_tb_init(&tb);

    AX_CHECK(!ax_tb_has_new(&tb));
    slots[ax_tb_write_index(&tb)] = 1;
    ax_tb_publish(&tb);
    slots[ax_tb_write_index(&tb)] = 2;
    ax_tb_publish(&tb);
    AX_CHECK(ax_tb_has_new(&tb));
    AX_CHECK_EQ_I(slots[ax_tb_acquire(&tb)], 2); /* intermediate value skipped */
    AX_CHECK(!ax_tb_has_new(&tb));
    AX_CHECK_EQ_I(slots[ax_tb_acquire(&tb)], 2); /* nothing new: same slot again */
}

AX_TEST(slots_never_shared)
{
    ax_triple_buffer tb;
    int i;
    ax_tb_init(&tb);
    for (i = 0; i < 100; ++i) {
        uint32_t w = ax_tb_write_index(&tb);
        uint32_t r;
        if (i % 3 == 0) {
            ax_tb_publish(&tb);
        }
        r = ax_tb_acquire(&tb);
        AX_REQUIRE(ax_tb_write_index(&tb) != r);
        (void)w;
    }
}

/* Writer publishes increasing sequences; the reader must only ever see complete, increasing ones.
 */
typedef struct seq_payload {
    uint64_t a, b, c; /* a == b == c when complete */
} seq_payload;

typedef struct tb_shared {
    ax_triple_buffer tb;
    seq_payload slots[3];
    ax_atomic_u32 done;
} tb_shared;

static int tb_writer(void *arg)
{
    tb_shared *s = (tb_shared *)arg;
    uint64_t v;
    for (v = 1; v <= 200000; ++v) {
        seq_payload *p = &s->slots[ax_tb_write_index(&s->tb)];
        p->a = v;
        p->b = v;
        p->c = v;
        ax_tb_publish(&s->tb);
    }
    ax_atomic_store_u32(&s->done, 1, AX_MO_RELEASE);
    return 0;
}

AX_TEST(concurrent_reader_sees_consistent_increasing_values)
{
    static tb_shared s;
    ax_thread t;
    uint64_t last = 0;
    int bad = 0;

    ax_tb_init(&s.tb);
    s.slots[0].a = s.slots[0].b = s.slots[0].c = 0;
    ax_atomic_init_u32(&s.done, 0);
    AX_REQUIRE(ax_thread_create(&t, tb_writer, &s) == AX_OK);
    for (;;) {
        bool finished = ax_atomic_load_u32(&s.done, AX_MO_ACQUIRE) != 0;
        const seq_payload *p = &s.slots[ax_tb_acquire(&s.tb)];
        if (p->a != p->b || p->b != p->c || p->a < last) {
            bad++;
        }
        last = p->a;
        if (finished) {
            p = &s.slots[ax_tb_acquire(&s.tb)];
            last = p->a;
            break;
        }
    }
    ax_thread_join(&t);
    AX_CHECK_EQ_I(bad, 0);
    AX_CHECK_EQ_U(last, 200000);
}
