/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The mailbox under concurrency also runs under ThreadSanitizer (the clang-tsan preset).
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_tracking_allocator.h"
#include "axion/render/ax_ir_mailbox.h"

static void sleep_ns(uint64_t ns)
{
    ax_mutex m;
    ax_cond c;
    const uint64_t end = ax_time_monotonic_ns() + ns;
    ax_mutex_init(&m);
    ax_cond_init(&c);
    ax_mutex_lock(&m);
    while (ax_time_monotonic_ns() < end) {
        ax_cond_wait_timeout(&c, &m, end - ax_time_monotonic_ns());
    }
    ax_mutex_unlock(&m);
    ax_cond_deinit(&c);
    ax_mutex_deinit(&m);
}

/* A snapshot whose every value is derived from `n`, so a reader can check it is whole. */
static void record(ax_ir_mailbox *mb, uint64_t n)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const uint32_t items = (uint32_t)(n % 23u) + 1u;
    uint32_t i;
    ax_ir_builder *b = ax_ir_mailbox_begin(mb, (float)(n % 1000u), 100.0f, n,
                                           ax_color_make((float)items, 0, 0, 1));
    for (i = 0; i < items; ++i) {
        if (i % 5 == 4) {
            d.transform = ax_affine2_translate((float)n, (float)i);
            ax_ir_push_layer(b, &d);
            ax_ir_rect(b, ax_rect_make((float)n, (float)i, 1, 1), ax_color_make(1, 1, 1, 1));
            ax_ir_pop_layer(b);
        } else {
            ax_ir_rect(b, ax_rect_make((float)n, (float)i, 1, 1), ax_color_make(1, 1, 1, 1));
        }
    }
}

/* True when every value of the snapshot matches its frame_time_ns. */
static bool snapshot_is_whole(const ax_render_ir *ir)
{
    const uint64_t n = ir->frame_time_ns;
    uint32_t l, count = 0;
    if (ax_ir_validate(ir) != AX_OK || ir->logical_width != (float)(n % 1000u)) {
        return false;
    }
    for (l = 0; l < ir->layer_count; ++l) {
        ax_ir_item_iter it = ax_ir_items(ir, ax_ir_layer_at(ir, l));
        const ax_ir_item_header *h;
        while ((h = ax_ir_item_next(&it)) != NULL) {
            const ax_ir_rect_item *r = (const ax_ir_rect_item *)(const void *)h;
            if (h->type != AX_IR_ITEM_RECT || r->rect.x != (float)n) {
                return false;
            }
            ++count;
        }
    }
    return count == (uint32_t)(n % 23u) + 1u && ir->background.r == (float)count;
}

AX_TEST(reader_takes_the_newest_snapshot)
{
    ax_ir_mailbox mb;
    const ax_render_ir *ir, *again;
    ax_ir_mailbox_init(&mb, ax_default_allocator());

    AX_CHECK(ax_ir_mailbox_acquire(&mb) == NULL);
    AX_CHECK_EQ_U(ax_ir_mailbox_published(&mb), 0);
    AX_CHECK(!ax_ir_mailbox_wait(&mb, 0, 0));

    record(&mb, 10);
    AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    ir = ax_ir_mailbox_acquire(&mb);
    AX_REQUIRE(ir != NULL);
    AX_CHECK_EQ_U(ir->sequence, 1);
    AX_CHECK_EQ_U(ir->frame_time_ns, 10);

    record(&mb, 11);
    AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    record(&mb, 12);
    AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    AX_CHECK(ax_ir_mailbox_wait(&mb, 1, 0));
    ir = ax_ir_mailbox_acquire(&mb);
    AX_REQUIRE(ir != NULL);
    AX_CHECK_EQ_U(ir->sequence, 3); /* 2 was replaced before the reader took it */
    AX_CHECK(snapshot_is_whole(ir));
    again = ax_ir_mailbox_acquire(&mb);
    AX_CHECK_EQ_P(again, ir); /* nothing newer: the same snapshot */
    AX_CHECK(!ax_ir_mailbox_wait(&mb, 3, 0));
    ax_ir_mailbox_deinit(&mb);
}

AX_TEST(writer_never_touches_the_held_snapshot)
{
    ax_ir_mailbox mb;
    const ax_render_ir *held;
    uint64_t n;
    ax_ir_mailbox_init(&mb, ax_default_allocator());
    record(&mb, 500);
    AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    held = ax_ir_mailbox_acquire(&mb);
    AX_REQUIRE(held != NULL);
    /* The writer keeps publishing without the reader taking anything: it cycles through the two
     * other slots and never waits. */
    for (n = 501; n < 2000; ++n) {
        record(&mb, n);
        AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
        AX_REQUIRE(held->frame_time_ns == 500 && held->sequence == 1);
    }
    AX_CHECK(snapshot_is_whole(held));
    held = ax_ir_mailbox_acquire(&mb);
    AX_REQUIRE(held != NULL);
    AX_CHECK_EQ_U(held->frame_time_ns, 1999);
    AX_CHECK(snapshot_is_whole(held));
    ax_ir_mailbox_deinit(&mb);
}

AX_TEST(failed_snapshot_is_not_published)
{
    ax_ir_mailbox mb;
    ax_ir_builder *b;
    const ax_render_ir *ir;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    ax_ir_mailbox_init(&mb, ax_default_allocator());
    record(&mb, 1);
    AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    b = ax_ir_mailbox_begin(&mb, 1, 1, 2, ax_color_make(0, 0, 0, 1));
    b->error = AX_ERR_OUT_OF_MEMORY; /* as if the arena had failed */
    ax_ir_push_layer(b, &d);
    AX_CHECK_RESULT(ax_ir_mailbox_publish(&mb), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK_EQ_U(ax_ir_mailbox_published(&mb), 1);
    ir = ax_ir_mailbox_acquire(&mb);
    AX_REQUIRE(ir != NULL);
    AX_CHECK_EQ_U(ir->frame_time_ns, 1);
    /* The next snapshot gets the next sequence number. */
    record(&mb, 3);
    AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    ir = ax_ir_mailbox_acquire(&mb);
    AX_REQUIRE(ir != NULL);
    AX_CHECK_EQ_U(ir->sequence, 2);
    ax_ir_mailbox_deinit(&mb);
}

typedef struct stress {
    ax_ir_mailbox mb;
    uint64_t count;
    uint64_t pause_every; /* the writer pauses briefly every so often, so the reader also sleeps */
} stress;

static int writer_thread(void *arg)
{
    stress *s = (stress *)arg;
    uint64_t n;
    for (n = 1; n <= s->count; ++n) {
        record(&s->mb, n);
        if (ax_ir_mailbox_publish(&s->mb) != AX_OK) {
            return 1;
        }
        if (s->pause_every != 0 && n % s->pause_every == 0) {
            sleep_ns(200000);
        }
    }
    return 0;
}

AX_TEST(concurrent_reader_sees_whole_increasing_snapshots)
{
    static stress s;
    ax_thread t;
    uint64_t last = 0;
    uint64_t taken = 0, waits = 0;
    const uint64_t deadline = ax_time_monotonic_ns() + 60ull * 1000000000ull;

    if (!ax_threads_supported()) {
        AX_SKIP("no threads");
    }
    ax_ir_mailbox_init(&s.mb, ax_default_allocator());
    s.count = 100000;
    s.pause_every = 997;
    AX_REQUIRE(ax_thread_create(&t, writer_thread, &s) == AX_OK);
    while (last < s.count && ax_time_monotonic_ns() < deadline) {
        const ax_render_ir *ir = ax_ir_mailbox_acquire(&s.mb);
        if (ir != NULL) {
            AX_REQUIRE(ir->sequence >= last);
            AX_REQUIRE(ir->sequence == ir->frame_time_ns); /* writer numbers from 1 too */
            if (ir->sequence != last) {
                AX_REQUIRE(snapshot_is_whole(ir));
                last = ir->sequence;
                ++taken;
            }
        }
        if (ax_ir_mailbox_wait(&s.mb, last, 1000000ull)) {
            ++waits;
        }
    }
    AX_CHECK_EQ_I(ax_thread_join(&t), 0);
    AX_CHECK_EQ_U(last, s.count);
    AX_CHECK(taken > 1);
    printf("taken %llu of %llu snapshots, %llu successful waits\n", (unsigned long long)taken,
           (unsigned long long)s.count, (unsigned long long)waits);
    ax_ir_mailbox_deinit(&s.mb);
}

AX_TEST(wait_times_out_and_wakes)
{
    static stress s;
    ax_thread t;
    uint64_t start, elapsed;

    if (!ax_threads_supported()) {
        AX_SKIP("no threads");
    }
    ax_ir_mailbox_init(&s.mb, ax_default_allocator());
    start = ax_time_monotonic_ns();
    AX_CHECK(!ax_ir_mailbox_wait(&s.mb, 0, 20000000ull));
    AX_CHECK(ax_time_monotonic_ns() - start >= 20000000ull);

    /* A publish from another thread wakes the reader well before the timeout. */
    s.count = 1;
    start = ax_time_monotonic_ns();
    AX_REQUIRE(ax_thread_create(&t, writer_thread, &s) == AX_OK);
    AX_CHECK(ax_ir_mailbox_wait(&s.mb, 0, 10ull * 1000000000ull));
    elapsed = ax_time_monotonic_ns() - start;
    AX_CHECK(elapsed < 5ull * 1000000000ull);
    AX_CHECK_EQ_I(ax_thread_join(&t), 0);
    AX_CHECK_EQ_U(ax_ir_mailbox_published(&s.mb), 1);
    ax_ir_mailbox_deinit(&s.mb);
}

AX_TEST(steady_state_publishing_allocates_nothing)
{
    ax_tracking_allocator tr;
    ax_allocator a;
    ax_ir_mailbox mb;
    size_t warm;
    uint64_t n;
    ax_tracking_allocator_init(&tr, ax_default_allocator());
    a = ax_tracking_allocator_get(&tr);
    ax_ir_mailbox_init(&mb, &a);
    for (n = 0; n < 12; ++n) { /* every slot recorded at the largest size, twice */
        record(&mb, 22 + 23 * n);
        AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
        if (n % 2 == 0) {
            (void)ax_ir_mailbox_acquire(&mb);
        }
    }
    warm = tr.alloc_count;
    for (n = 0; n < 1000; ++n) {
        record(&mb, n);
        AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
        if (n % 3 == 0) {
            (void)ax_ir_mailbox_acquire(&mb);
        }
    }
    AX_CHECK_EQ_U(tr.alloc_count, warm);
    ax_ir_mailbox_deinit(&mb);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&tr), 0);
}
