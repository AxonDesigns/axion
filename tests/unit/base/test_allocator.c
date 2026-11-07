/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_allocator.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_tracking_allocator.h"

static void fill(unsigned char *p, size_t n, unsigned seed)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        p[i] = (unsigned char)(seed + i * 31u);
    }
}

static int matches(const unsigned char *p, size_t n, unsigned seed)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (p[i] != (unsigned char)(seed + i * 31u)) {
            return 0;
        }
    }
    return 1;
}

AX_TEST(default_allocator_respects_alignment)
{
    const ax_allocator *a = ax_default_allocator();
    size_t align;
    for (align = 1; align <= 8192; align *= 2) {
        void *p = ax_mem_alloc(a, 24, align);
        AX_REQUIRE(p != NULL);
        AX_CHECK(AX_IS_ALIGNED(p, align));
        memset(p, 0xAB, 24);
        ax_mem_free(a, p, 24);
    }
}

AX_TEST(default_allocator_resize_keeps_contents_and_alignment)
{
    const ax_allocator *a = ax_default_allocator();
    size_t sizes[] = {16, 4000, 64, 100000, 7, 333};
    size_t align;

    for (align = 1; align <= 4096; align *= 4) {
        size_t cur = sizes[0];
        size_t i;
        unsigned char *p = (unsigned char *)ax_mem_alloc(a, cur, align);
        AX_REQUIRE(p != NULL);
        fill(p, cur, (unsigned)align);
        for (i = 1; i < sizeof sizes / sizeof sizes[0]; ++i) {
            size_t keep = cur < sizes[i] ? cur : sizes[i];
            unsigned char *q = (unsigned char *)ax_mem_resize(a, p, cur, sizes[i], align);
            AX_REQUIRE(q != NULL);
            AX_CHECK(AX_IS_ALIGNED(q, align));
            AX_CHECK(matches(q, keep, (unsigned)align));
            fill(q, sizes[i], (unsigned)align);
            p = q;
            cur = sizes[i];
        }
        ax_mem_free(a, p, cur);
    }
}

AX_TEST(free_null_is_noop)
{
    const ax_allocator *a = ax_default_allocator();
    a->free(a->ctx, NULL, 0);
    a->free(a->ctx, NULL, 123);

    ax_tracking_allocator t;
    ax_tracking_allocator_init(&t, a);
    ax_allocator ta = ax_tracking_allocator_get(&t);
    ta.free(ta.ctx, NULL, 8);
    AX_CHECK_EQ_U(t.misuse_count, 0);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

AX_TEST(tracking_counts_bytes)
{
    ax_tracking_allocator t;
    ax_tracking_allocator_init(&t, ax_default_allocator());
    ax_allocator a = ax_tracking_allocator_get(&t);

    void *p = ax_mem_alloc(&a, 100, 16);
    void *q = ax_mem_alloc(&a, 28, 64);
    AX_REQUIRE(p != NULL && q != NULL);
    AX_CHECK(AX_IS_ALIGNED(p, 16));
    AX_CHECK(AX_IS_ALIGNED(q, 64));
    AX_CHECK_EQ_U(t.live_count, 2);
    AX_CHECK_EQ_U(t.live_bytes, 128);

    p = ax_mem_resize(&a, p, 100, 300, 16);
    AX_REQUIRE(p != NULL);
    AX_CHECK_EQ_U(t.live_bytes, 328);
    AX_CHECK_EQ_U(t.peak_bytes, 328);

    ax_mem_free(&a, q, 28);
    ax_mem_free(&a, p, 300);
    AX_CHECK_EQ_U(t.live_count, 0);
    AX_CHECK_EQ_U(t.live_bytes, 0);
    AX_CHECK_EQ_U(t.peak_bytes, 328);
    AX_CHECK_EQ_U(t.alloc_count, 2);
    AX_CHECK_EQ_U(t.resize_count, 1);
    AX_CHECK_EQ_U(t.free_count, 2);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

AX_TEST(tracking_detects_leaks)
{
    ax_tracking_allocator t;
    ax_tracking_allocator_init(&t, ax_default_allocator());
    ax_allocator a = ax_tracking_allocator_get(&t);

    void *kept = ax_mem_alloc(&a, 40, 8);
    void *leak1 = ax_mem_alloc(&a, 10, 8);
    void *leak2 = ax_mem_alloc(&a, 20, 256);
    AX_REQUIRE(kept != NULL && leak1 != NULL && leak2 != NULL);
    ax_mem_free(&a, kept, 40);

    AX_CHECK_EQ_U(t.live_count, 2);
    AX_CHECK_EQ_U(t.live_bytes, 30);
    /* deinit reports both leaks and returns them to the parent, so ASan sees no leak. */
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 2);
}

AX_TEST(tracking_detects_size_mismatch_and_double_free)
{
    ax_tracking_allocator t;
    ax_tracking_allocator_init(&t, ax_default_allocator());
    t.abort_on_misuse = false;
    ax_allocator a = ax_tracking_allocator_get(&t);

    void *p = a.alloc(a.ctx, 64, 8);
    AX_REQUIRE(p != NULL);
    a.free(a.ctx, p, 32); /* wrong size: reported, freed with the recorded size */
    AX_CHECK_EQ_U(t.misuse_count, 1);
    AX_CHECK_EQ_U(t.live_count, 0);

    a.free(a.ctx, p, 64); /* double free: reported, never reaches the parent */
    AX_CHECK_EQ_U(t.misuse_count, 2);

    AX_CHECK(a.alloc(a.ctx, 0, 8) == NULL);
    AX_CHECK(a.alloc(a.ctx, 8, 3) == NULL);
    AX_CHECK_EQ_U(t.misuse_count, 4);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

/* A parent whose resize always fails, to check that a failed resize keeps the block. */
static void *fail_resize(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align)
{
    (void)ctx;
    (void)ptr;
    (void)old_size;
    (void)new_size;
    (void)align;
    return NULL;
}

AX_TEST(failed_resize_keeps_original_block)
{
    ax_allocator parent = *ax_default_allocator();
    parent.resize = fail_resize;

    ax_tracking_allocator t;
    ax_tracking_allocator_init(&t, &parent);
    ax_allocator a = ax_tracking_allocator_get(&t);

    unsigned char *p = (unsigned char *)ax_mem_alloc(&a, 32, 16);
    AX_REQUIRE(p != NULL);
    fill(p, 32, 7);
    AX_CHECK(ax_mem_resize(&a, p, 32, 64, 16) == NULL);
    AX_CHECK(matches(p, 32, 7));
    AX_CHECK_EQ_U(t.live_count, 1);
    AX_CHECK_EQ_U(t.live_bytes, 32);
    ax_mem_free(&a, p, 32);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

AX_TEST(huge_requests_fail_cleanly)
{
    const ax_allocator *a = ax_default_allocator();
    AX_CHECK(ax_mem_alloc(a, SIZE_MAX, 1) == NULL);
    AX_CHECK(ax_mem_alloc(a, SIZE_MAX - 8, 64) == NULL);
}
