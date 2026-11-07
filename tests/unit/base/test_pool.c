/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_pool.h"
#include "axion/base/ax_tracking_allocator.h"

typedef struct item {
    uint32_t value;
    double payload[3];
} item;

AX_DECLARE_HANDLE(test_item);

typedef struct pool_fixture {
    ax_tracking_allocator tracker;
    ax_allocator allocator;
    ax_pool pool;
} pool_fixture;

static void fixture_init(pool_fixture *f, uint32_t per_page)
{
    ax_tracking_allocator_init(&f->tracker, ax_default_allocator());
    f->allocator = ax_tracking_allocator_get(&f->tracker);
    ax_pool_init(&f->pool, &f->allocator, sizeof(item), AX_ALIGNOF(item), per_page);
}

static size_t fixture_deinit(pool_fixture *f)
{
    ax_pool_deinit(&f->pool);
    return ax_tracking_allocator_deinit(&f->tracker);
}

AX_TEST(alloc_get_free)
{
    pool_fixture f;
    test_item h;
    item *p;
    item *q;
    fixture_init(&f, 0);

    AX_CHECK_RESULT(ax_pool_alloc(&f.pool, &h.h, (void **)&p), AX_OK);
    AX_REQUIRE(p != NULL);
    AX_CHECK(!AX_HANDLE_IS_NULL(h));
    AX_CHECK_EQ_U(p->value, 0); /* zeroed */
    AX_CHECK(AX_IS_ALIGNED(p, AX_ALIGNOF(item)));
    p->value = 42;

    AX_CHECK_RESULT(ax_pool_get(&f.pool, h.h, (void **)&q), AX_OK);
    AX_CHECK_EQ_P(q, p);
    AX_CHECK_EQ_U(q->value, 42);
    AX_CHECK_EQ_U(ax_pool_live_count(&f.pool), 1);

    AX_CHECK_RESULT(ax_pool_free(&f.pool, h.h), AX_OK);
    AX_CHECK_EQ_U(ax_pool_live_count(&f.pool), 0);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(stale_handle)
{
    pool_fixture f;
    uint64_t h;
    item *p;
    fixture_init(&f, 0);

    AX_REQUIRE(ax_pool_alloc(&f.pool, &h, NULL) == AX_OK);
    AX_REQUIRE(ax_pool_free(&f.pool, h) == AX_OK);

    p = (item *)(uintptr_t)1;
    AX_CHECK_RESULT(ax_pool_get(&f.pool, h, (void **)&p), AX_ERR_INVALID_HANDLE);
    AX_CHECK(p == NULL);
    AX_CHECK_RESULT(ax_pool_free(&f.pool, h), AX_ERR_INVALID_HANDLE); /* double free */
    AX_CHECK(!ax_pool_is_valid(&f.pool, h));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(recycled_slot_rejects_old_handle)
{
    pool_fixture f;
    uint64_t old_h;
    uint64_t new_h;
    item *p;
    fixture_init(&f, 0);

    AX_REQUIRE(ax_pool_alloc(&f.pool, &old_h, (void **)&p) == AX_OK);
    p->value = 1;
    AX_REQUIRE(ax_pool_free(&f.pool, old_h) == AX_OK);

    /* The freed slot is reused for the next object, with a higher generation. */
    AX_REQUIRE(ax_pool_alloc(&f.pool, &new_h, (void **)&p) == AX_OK);
    AX_CHECK_EQ_U(ax_handle_slot(new_h), ax_handle_slot(old_h));
    AX_CHECK_EQ_U(ax_handle_generation(new_h), ax_handle_generation(old_h) + 1);
    p->value = 2;

    /* The old handle must not reach the new occupant. */
    AX_CHECK_RESULT(ax_pool_get(&f.pool, old_h, (void **)&p), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_pool_free(&f.pool, old_h), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_pool_get(&f.pool, new_h, (void **)&p), AX_OK);
    AX_REQUIRE(p != NULL);
    AX_CHECK_EQ_U(p->value, 2);
    AX_CHECK_EQ_U(ax_pool_live_count(&f.pool), 1);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(invalid_handles)
{
    pool_fixture f;
    uint64_t h;
    item *p;
    fixture_init(&f, 0);

    AX_CHECK_RESULT(ax_pool_get(&f.pool, 0, (void **)&p), AX_ERR_INVALID_HANDLE); /* empty */
    AX_REQUIRE(ax_pool_alloc(&f.pool, &h, NULL) == AX_OK);

    AX_CHECK_RESULT(ax_pool_get(&f.pool, 0, (void **)&p), AX_ERR_INVALID_HANDLE); /* null */
    AX_CHECK_RESULT(ax_pool_free(&f.pool, 0), AX_ERR_INVALID_HANDLE);
    /* Generation 0 on a live slot. */
    AX_CHECK_RESULT(ax_pool_get(&f.pool, ax_handle_make(ax_handle_slot(h), 0), (void **)&p),
                    AX_ERR_INVALID_HANDLE);
    /* A future generation. */
    AX_CHECK_RESULT(ax_pool_get(&f.pool, h + (1ull << 32), (void **)&p), AX_ERR_INVALID_HANDLE);
    /* A slot that was never created. */
    AX_CHECK_RESULT(ax_pool_get(&f.pool, ax_handle_make(1000, 1), (void **)&p),
                    AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_pool_free(&f.pool, ax_handle_make(0xFFFFFFFFu, 0xFFFFFFFFu)),
                    AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(ax_pool_live_count(&f.pool), 1);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(addresses_stay_stable_while_growing)
{
    enum { N = 1000 };
    pool_fixture f;
    static uint64_t handles[N];
    static item *ptrs[N];
    size_t i;
    fixture_init(&f, 8); /* small pages: many page allocations and slot-array growth */

    for (i = 0; i < N; ++i) {
        AX_REQUIRE(ax_pool_alloc(&f.pool, &handles[i], (void **)&ptrs[i]) == AX_OK);
        ptrs[i]->value = (uint32_t)i;
    }
    for (i = 0; i < N; ++i) {
        item *p;
        AX_REQUIRE(ax_pool_get(&f.pool, handles[i], (void **)&p) == AX_OK);
        AX_REQUIRE(p == ptrs[i]);
        AX_REQUIRE(p->value == (uint32_t)i);
    }
    /* Free every other element, then refill: freed slots are reused, no new slots. */
    for (i = 0; i < N; i += 2) {
        AX_REQUIRE(ax_pool_free(&f.pool, handles[i]) == AX_OK);
    }
    AX_CHECK_EQ_U(ax_pool_live_count(&f.pool), N / 2);
    for (i = 0; i < N; i += 2) {
        uint64_t h;
        AX_REQUIRE(ax_pool_alloc(&f.pool, &h, NULL) == AX_OK);
        AX_REQUIRE(ax_handle_slot(h) < N);
        AX_REQUIRE(!ax_pool_is_valid(&f.pool, handles[i]));
    }
    AX_CHECK_EQ_U(f.pool.slot_count, N);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(exhausted_generation_retires_slot)
{
    pool_fixture f;
    uint64_t h;
    uint64_t next;
    fixture_init(&f, 0);

    AX_REQUIRE(ax_pool_alloc(&f.pool, &h, NULL) == AX_OK);
    AX_REQUIRE(ax_pool_free(&f.pool, h) == AX_OK);
    /* Skip ahead instead of freeing 2^32 times: the struct is visible for exactly this. */
    f.pool.slots[ax_handle_slot(h)].generation = 0xFFFFFFFFu;
    AX_REQUIRE(ax_pool_alloc(&f.pool, &h, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_handle_generation(h), 0xFFFFFFFFu);
    AX_REQUIRE(ax_pool_free(&f.pool, h) == AX_OK);

    /* The slot is retired: the next allocation gets a fresh slot. */
    AX_CHECK_EQ_U(f.pool.slots[ax_handle_slot(h)].next_free, AX_POOL_RETIRED);
    AX_REQUIRE(ax_pool_alloc(&f.pool, &next, NULL) == AX_OK);
    AX_CHECK(ax_handle_slot(next) != ax_handle_slot(h));
    AX_CHECK_EQ_U(ax_handle_generation(next), 1);
    AX_CHECK(!ax_pool_is_valid(&f.pool, h));
    AX_CHECK(!ax_pool_is_valid(&f.pool, ax_handle_make(ax_handle_slot(h), 0)));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* Fails every allocation after `budget` successful ones. */
typedef struct budget_allocator {
    const ax_allocator *parent;
    int budget;
} budget_allocator;

static void *budget_alloc(void *ctx, size_t size, size_t align)
{
    budget_allocator *b = (budget_allocator *)ctx;
    if (b->budget <= 0) {
        return NULL;
    }
    b->budget--;
    return b->parent->alloc(b->parent->ctx, size, align);
}

static void *budget_resize(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align)
{
    budget_allocator *b = (budget_allocator *)ctx;
    if (b->budget <= 0) {
        return NULL;
    }
    b->budget--;
    return b->parent->resize(b->parent->ctx, ptr, old_size, new_size, align);
}

static void budget_free(void *ctx, void *ptr, size_t size)
{
    budget_allocator *b = (budget_allocator *)ctx;
    b->parent->free(b->parent->ctx, ptr, size);
}

AX_TEST(out_of_memory_leaves_pool_unchanged)
{
    ax_tracking_allocator t;
    ax_allocator tracked;
    budget_allocator budget;
    ax_allocator limited;
    ax_pool pool;
    uint64_t h = 123;
    int b;

    ax_tracking_allocator_init(&t, ax_default_allocator());
    tracked = ax_tracking_allocator_get(&t);
    /* Fail at each of the three allocations the first alloc needs (slots, page table, page). */
    for (b = 0; b < 3; ++b) {
        budget.parent = &tracked;
        budget.budget = b;
        limited.alloc = budget_alloc;
        limited.resize = budget_resize;
        limited.free = budget_free;
        limited.ctx = &budget;
        ax_pool_init(&pool, &limited, sizeof(item), AX_ALIGNOF(item), 4);
        AX_CHECK_RESULT(ax_pool_alloc(&pool, &h, NULL), AX_ERR_OUT_OF_MEMORY);
        AX_CHECK_EQ_U(h, 0);
        AX_CHECK_EQ_U(ax_pool_live_count(&pool), 0);
        AX_CHECK_EQ_U(pool.slot_count, 0);
        budget.budget = 100;
        AX_CHECK_RESULT(ax_pool_alloc(&pool, &h, NULL), AX_OK); /* recovers */
        ax_pool_deinit(&pool);
    }
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}
