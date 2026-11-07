/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "axion/base/ax_array.h"
#include "axion/base/ax_tracking_allocator.h"

AX_ARRAY_DEFINE(u32_array, uint32_t);

typedef struct wide {
    AX_ALIGNAS(64) float v[4];
} wide;
AX_ARRAY_DEFINE(wide_array, wide);

AX_TEST(push_pop_and_growth)
{
    ax_tracking_allocator t;
    ax_allocator a;
    u32_array xs;
    uint32_t i;

    ax_tracking_allocator_init(&t, ax_default_allocator());
    a = ax_tracking_allocator_get(&t);
    u32_array_init(&xs, &a);
    AX_CHECK_EQ_U(t.alloc_count, 0); /* init allocates nothing */

    for (i = 0; i < 10000; ++i) {
        AX_REQUIRE(u32_array_push(&xs, i * 3u) == AX_OK);
    }
    AX_CHECK_EQ_U(xs.len, 10000);
    AX_CHECK(xs.cap >= 10000);
    AX_CHECK(t.resize_count < 20); /* geometric growth */
    for (i = 0; i < 10000; ++i) {
        AX_REQUIRE(xs.data[i] == i * 3u);
    }
    AX_CHECK_EQ_U(u32_array_pop(&xs), 9999u * 3u);
    AX_CHECK_EQ_U(xs.len, 9999);
    AX_CHECK_EQ_U(*u32_array_at(&xs, 10), 30);

    u32_array_deinit(&xs);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

AX_TEST(reserve_resize_clear_remove)
{
    ax_tracking_allocator t;
    ax_allocator a;
    u32_array xs;
    size_t allocs;

    ax_tracking_allocator_init(&t, ax_default_allocator());
    a = ax_tracking_allocator_get(&t);
    u32_array_init(&xs, &a);

    AX_REQUIRE(u32_array_reserve(&xs, 100) == AX_OK);
    AX_CHECK(xs.cap >= 100);
    allocs = t.alloc_count + t.resize_count;
    AX_REQUIRE(u32_array_push(&xs, 1) == AX_OK);
    AX_REQUIRE(u32_array_push(&xs, 2) == AX_OK);
    AX_REQUIRE(u32_array_push(&xs, 3) == AX_OK);
    AX_CHECK_EQ_U(t.alloc_count + t.resize_count, allocs); /* no reallocation within capacity */

    AX_REQUIRE(u32_array_resize(&xs, 6) == AX_OK); /* grows with zeroes */
    AX_CHECK(xs.data[3] == 0 && xs.data[4] == 0 && xs.data[5] == 0);
    AX_REQUIRE(u32_array_resize(&xs, 2) == AX_OK);
    AX_CHECK_EQ_U(xs.len, 2);

    u32_array_remove_swap(&xs, 0); /* [1, 2] -> [2] */
    AX_CHECK_EQ_U(xs.len, 1);
    AX_CHECK_EQ_U(xs.data[0], 2);

    u32_array_clear(&xs);
    AX_CHECK_EQ_U(xs.len, 0);
    AX_CHECK(xs.cap >= 100);
    u32_array_deinit(&xs);
    u32_array_deinit(&xs); /* deinit twice is harmless */
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

AX_TEST(over_aligned_elements)
{
    ax_tracking_allocator t;
    ax_allocator a;
    wide_array ws;
    int i;

    ax_tracking_allocator_init(&t, ax_default_allocator());
    a = ax_tracking_allocator_get(&t);
    wide_array_init(&ws, &a);
    for (i = 0; i < 100; ++i) {
        wide w = {{0, 0, 0, 0}};
        w.v[0] = (float)i;
        AX_REQUIRE(wide_array_push(&ws, w) == AX_OK);
        AX_REQUIRE(AX_IS_ALIGNED(ws.data, 64));
    }
    AX_CHECK(ws.data[99].v[0] == 99.0f);
    wide_array_deinit(&ws);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

static void *null_alloc(void *ctx, size_t size, size_t align)
{
    (void)ctx;
    (void)size;
    (void)align;
    return NULL;
}

AX_TEST(out_of_memory_leaves_array_unchanged)
{
    ax_allocator failing = *ax_default_allocator();
    u32_array xs;
    failing.alloc = null_alloc;

    u32_array_init(&xs, &failing);
    AX_CHECK_RESULT(u32_array_push(&xs, 1), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK_RESULT(u32_array_reserve(&xs, 10), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK_RESULT(u32_array_resize(&xs, 10), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK(xs.data == NULL);
    AX_CHECK_EQ_U(xs.len, 0);
    AX_CHECK_EQ_U(xs.cap, 0);
    AX_CHECK_RESULT(u32_array_reserve(&xs, SIZE_MAX), AX_ERR_OUT_OF_MEMORY); /* bytes overflow */
    u32_array_deinit(&xs);
}
