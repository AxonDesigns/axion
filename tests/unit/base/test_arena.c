/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_arena.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_tracking_allocator.h"

typedef struct arena_fixture {
    ax_tracking_allocator tracker;
    ax_allocator parent;
    ax_arena arena;
} arena_fixture;

static void fixture_init(arena_fixture *f, size_t block_size)
{
    ax_tracking_allocator_init(&f->tracker, ax_default_allocator());
    f->parent = ax_tracking_allocator_get(&f->tracker);
    ax_arena_init(&f->arena, &f->parent, block_size);
}

static size_t fixture_deinit(arena_fixture *f)
{
    ax_arena_deinit(&f->arena);
    return ax_tracking_allocator_deinit(&f->tracker);
}

AX_TEST(allocations_honor_alignment)
{
    arena_fixture f;
    size_t align;
    fixture_init(&f, 4096);

    for (align = 1; align <= 4096; align *= 2) {
        /* An odd-sized allocation first, so every aligned one needs padding. */
        unsigned char *pad = (unsigned char *)ax_arena_alloc(&f.arena, 3, 1);
        unsigned char *p = (unsigned char *)ax_arena_alloc(&f.arena, 40, align);
        AX_REQUIRE(pad != NULL && p != NULL);
        AX_CHECK(AX_IS_ALIGNED(p, align));
        memset(p, 0x5A, 40);
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(allocations_do_not_overlap)
{
    arena_fixture f;
    unsigned char *ptrs[200];
    size_t i;
    fixture_init(&f, 1024);

    for (i = 0; i < 200; ++i) {
        ptrs[i] = (unsigned char *)ax_arena_alloc(&f.arena, 1 + i % 50, 1u << (i % 6));
        AX_REQUIRE(ptrs[i] != NULL);
        memset(ptrs[i], (int)i, 1 + i % 50);
    }
    for (i = 0; i < 200; ++i) {
        size_t j;
        for (j = 0; j < 1 + i % 50; ++j) {
            AX_REQUIRE(ptrs[i][j] == (unsigned char)i);
        }
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(reset_reuses_blocks_without_allocating)
{
    arena_fixture f;
    void *first;
    void *again;
    size_t blocks;
    size_t frame;
    fixture_init(&f, 256);

    first = ax_arena_alloc(&f.arena, 64, 16);
    AX_REQUIRE(first != NULL);
    /* Spill into several blocks. */
    for (frame = 0; frame < 20; ++frame) {
        AX_REQUIRE(ax_arena_alloc(&f.arena, 100, 8) != NULL);
    }
    blocks = f.tracker.alloc_count;
    AX_CHECK(blocks > 1);

    /* Steady state: the same frame shape again and again allocates nothing new. */
    for (frame = 0; frame < 10; ++frame) {
        size_t i;
        ax_arena_reset(&f.arena);
        again = ax_arena_alloc(&f.arena, 64, 16);
        AX_CHECK_EQ_P(again, first);
        for (i = 0; i < 20; ++i) {
            AX_REQUIRE(ax_arena_alloc(&f.arena, 100, 8) != NULL);
        }
        AX_CHECK_EQ_U(f.tracker.alloc_count, blocks);
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(oversized_request_gets_its_own_block)
{
    arena_fixture f;
    unsigned char *big;
    fixture_init(&f, 128);

    AX_REQUIRE(ax_arena_alloc(&f.arena, 16, 8) != NULL);
    big = (unsigned char *)ax_arena_alloc(&f.arena, 10000, 256);
    AX_REQUIRE(big != NULL);
    AX_CHECK(AX_IS_ALIGNED(big, 256));
    memset(big, 1, 10000);
    AX_CHECK(ax_arena_capacity(&f.arena) >= 10000 + 128);

    /* After a reset the big block is reused for a big request instead of allocating again. */
    ax_arena_reset(&f.arena);
    {
        size_t before = f.tracker.alloc_count;
        AX_REQUIRE(ax_arena_alloc(&f.arena, 16, 8) != NULL);
        AX_REQUIRE(ax_arena_alloc(&f.arena, 10000, 256) != NULL);
        AX_CHECK_EQ_U(f.tracker.alloc_count, before);
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(save_restore_rewinds)
{
    arena_fixture f;
    ax_arena_mark mark;
    ax_arena_mark empty_mark;
    void *after_mark;
    size_t i;
    fixture_init(&f, 256);

    empty_mark = ax_arena_save(&f.arena);
    AX_REQUIRE(ax_arena_alloc(&f.arena, 32, 8) != NULL);
    mark = ax_arena_save(&f.arena);
    after_mark = ax_arena_alloc(&f.arena, 24, 8);
    AX_REQUIRE(after_mark != NULL);

    /* Scratch work that crosses into new blocks. */
    for (i = 0; i < 10; ++i) {
        AX_REQUIRE(ax_arena_alloc(&f.arena, 200, 8) != NULL);
    }
    ax_arena_restore(&f.arena, mark);
    AX_CHECK_EQ_P(ax_arena_alloc(&f.arena, 24, 8), after_mark);

    /* A mark taken before anything was allocated rewinds to the very start. */
    {
        void *start;
        ax_arena_reset(&f.arena);
        start = ax_arena_alloc(&f.arena, 32, 8);
        ax_arena_restore(&f.arena, empty_mark);
        AX_CHECK_EQ_P(ax_arena_alloc(&f.arena, 32, 8), start);
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(zeroed_and_array_allocations)
{
    arena_fixture f;
    uint32_t *xs;
    size_t i;
    fixture_init(&f, 0);

    /* Dirty the memory, rewind, then ask for zeroed memory over it. */
    memset(ax_arena_alloc(&f.arena, 400, 4), 0xFF, 400);
    ax_arena_reset(&f.arena);
    xs = AX_ARENA_NEW_ARRAY(&f.arena, uint32_t, 100);
    AX_REQUIRE(xs != NULL);
    AX_CHECK(AX_IS_ALIGNED(xs, AX_ALIGNOF(uint32_t)));
    for (i = 0; i < 100; ++i) {
        AX_REQUIRE(xs[i] == 0);
    }
    AX_CHECK(AX_ARENA_NEW_ARRAY(&f.arena, uint64_t, SIZE_MAX / 2) == NULL); /* overflow */
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(allocator_view_resizes_last_allocation_in_place)
{
    arena_fixture f;
    ax_allocator view;
    unsigned char *p;
    unsigned char *q;
    fixture_init(&f, 1024);
    view = ax_arena_allocator(&f.arena);

    p = (unsigned char *)ax_mem_alloc(&view, 16, 8);
    AX_REQUIRE(p != NULL);
    memset(p, 7, 16);
    q = (unsigned char *)ax_mem_resize(&view, p, 16, 64, 8);
    AX_CHECK_EQ_P(q, p); /* last allocation: grows in place */

    AX_REQUIRE(ax_mem_alloc(&view, 8, 8) != NULL);
    q = (unsigned char *)ax_mem_resize(&view, p, 64, 128, 8);
    AX_REQUIRE(q != NULL);
    AX_CHECK(q != p); /* no longer the last: copied */
    AX_CHECK(q[0] == 7 && q[15] == 7);
    ax_mem_free(&view, q, 128); /* no-op */
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(parent_failure_returns_null)
{
    ax_tracking_allocator t;
    ax_allocator parent;
    ax_arena arena;
    ax_tracking_allocator_init(&t, ax_default_allocator());
    parent = ax_tracking_allocator_get(&t);
    ax_arena_init(&arena, &parent, 64);

    AX_CHECK(ax_arena_alloc(&arena, SIZE_MAX - 4, 8) == NULL);
    AX_CHECK(ax_arena_alloc(&arena, 8, 8) != NULL); /* still usable */
    ax_arena_deinit(&arena);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}
