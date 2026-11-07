/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_pool.h"

/* Debug builds fill freed elements with this byte so use of a stale pointer shows up quickly. */
#define AX_POOL_POISON 0xDD

static uint32_t log2_u32(uint32_t x)
{
    uint32_t n = 0;
    while (x > 1u) {
        x >>= 1;
        ++n;
    }
    return n;
}

static unsigned char *element_at(const ax_pool *pool, uint32_t slot)
{
    uint32_t page = slot >> pool->page_shift;
    uint32_t index = slot & ((1u << pool->page_shift) - 1u);
    return pool->pages[page] + (size_t)index * pool->stride;
}

void ax_pool_init(ax_pool *pool, const ax_allocator *allocator, size_t elem_size, size_t elem_align,
                  uint32_t elems_per_page)
{
    AX_ASSERT(pool != NULL && allocator != NULL);
    AX_ASSERT_MSG(elem_size > 0, "pool: elem_size must be > 0");
    AX_ASSERT_MSG(ax_is_pow2(elem_align), "pool: elem_align must be a power of two");
    if (elems_per_page == 0) {
        elems_per_page = AX_POOL_DEFAULT_ELEMS_PER_PAGE;
    }
    AX_ASSERT_MSG(ax_is_pow2(elems_per_page), "pool: elems_per_page must be a power of two");

    memset(pool, 0, sizeof *pool);
    pool->allocator = *allocator;
    pool->elem_size = elem_size;
    pool->elem_align = elem_align;
    pool->stride = ax_align_up(elem_size, elem_align);
    pool->page_shift = log2_u32(elems_per_page);
    pool->free_head = AX_POOL_NIL;
}

void ax_pool_deinit(ax_pool *pool)
{
    uint32_t i;
    size_t page_bytes = pool->stride << pool->page_shift;
    for (i = 0; i < pool->page_count; ++i) {
        ax_mem_free(&pool->allocator, pool->pages[i], page_bytes);
    }
    if (pool->pages != NULL) {
        ax_mem_free(&pool->allocator, pool->pages, sizeof(*pool->pages) * pool->page_capacity);
    }
    if (pool->slots != NULL) {
        ax_mem_free(&pool->allocator, pool->slots, sizeof(*pool->slots) * pool->slot_capacity);
    }
    pool->pages = NULL;
    pool->slots = NULL;
    pool->page_count = pool->page_capacity = 0;
    pool->slot_count = pool->slot_capacity = 0;
    pool->live_count = 0;
    pool->free_head = AX_POOL_NIL;
}

/* Grows a uint32-counted array to hold at least `needed` items. */
static bool grow_array(const ax_allocator *a, void **array, uint32_t *capacity, uint32_t needed,
                       size_t item_size, size_t item_align)
{
    uint32_t new_cap;
    size_t old_bytes;
    size_t new_bytes;
    void *p;

    if (needed <= *capacity) {
        return true;
    }
    new_cap = *capacity != 0 ? *capacity : 16u;
    while (new_cap < needed) {
        new_cap = new_cap > 0x7FFFFFFFu ? AX_POOL_MAX_SLOTS : new_cap * 2u;
    }
    if (!ax_size_mul(new_cap, item_size, &new_bytes)) {
        return false;
    }
    old_bytes = (size_t)*capacity * item_size;
    p = *array != NULL ? ax_mem_resize(a, *array, old_bytes, new_bytes, item_align)
                       : ax_mem_alloc(a, new_bytes, item_align);
    if (p == NULL) {
        return false;
    }
    *array = p;
    *capacity = new_cap;
    return true;
}

/* Creates slot number pool->slot_count, with its page if needed. Leaves the pool unchanged on
 * failure (a grown array is kept; that is capacity, not state). */
static bool create_slot(ax_pool *pool)
{
    uint32_t slot = pool->slot_count;
    uint32_t page = slot >> pool->page_shift;
    void *slots = pool->slots;
    void *pages = pool->pages;

    if (slot >= AX_POOL_MAX_SLOTS) {
        return false;
    }
    if (!grow_array(&pool->allocator, &slots, &pool->slot_capacity, slot + 1u, sizeof(ax_pool_slot),
                    AX_ALIGNOF(ax_pool_slot))) {
        return false;
    }
    pool->slots = (ax_pool_slot *)slots;

    if (page == pool->page_count) {
        size_t page_bytes;
        unsigned char *mem;
        if (!ax_size_mul(pool->stride, (size_t)1 << pool->page_shift, &page_bytes)) {
            return false;
        }
        if (!grow_array(&pool->allocator, &pages, &pool->page_capacity, page + 1u,
                        sizeof(unsigned char *), AX_ALIGNOF(unsigned char *))) {
            return false;
        }
        pool->pages = (unsigned char **)pages;
        mem = (unsigned char *)ax_mem_alloc(&pool->allocator, page_bytes, pool->elem_align);
        if (mem == NULL) {
            return false;
        }
        pool->pages[page] = mem;
        pool->page_count++;
    }

    pool->slots[slot].generation = 1u;
    pool->slots[slot].next_free = AX_POOL_NIL;
    pool->slot_count++;
    return true;
}

ax_result ax_pool_alloc(ax_pool *pool, uint64_t *out_handle, void **out_ptr)
{
    uint32_t slot;
    unsigned char *elem;

    AX_ASSERT(pool != NULL && out_handle != NULL);
    *out_handle = 0;
    if (out_ptr != NULL) {
        *out_ptr = NULL;
    }

    if (pool->free_head != AX_POOL_NIL) {
        slot = pool->free_head;
        pool->free_head = pool->slots[slot].next_free;
    } else {
        if (!create_slot(pool)) {
            return AX_ERR_OUT_OF_MEMORY;
        }
        slot = pool->slot_count - 1u;
    }

    pool->slots[slot].next_free = AX_POOL_LIVE;
    pool->live_count++;
    elem = element_at(pool, slot);
    memset(elem, 0, pool->elem_size);

    *out_handle = ax_handle_make(slot, pool->slots[slot].generation);
    if (out_ptr != NULL) {
        *out_ptr = elem;
    }
    return AX_OK;
}

/* The slot index of handle if it is live in pool, else AX_POOL_NIL. */
static uint32_t live_slot(const ax_pool *pool, uint64_t handle)
{
    uint32_t slot = ax_handle_slot(handle);
    uint32_t generation = ax_handle_generation(handle);
    if (generation == 0 || slot >= pool->slot_count) {
        return AX_POOL_NIL;
    }
    if (pool->slots[slot].next_free != AX_POOL_LIVE || pool->slots[slot].generation != generation) {
        return AX_POOL_NIL;
    }
    return slot;
}

ax_result ax_pool_free(ax_pool *pool, uint64_t handle)
{
    uint32_t slot;
    ax_pool_slot *s;

    AX_ASSERT(pool != NULL);
    slot = live_slot(pool, handle);
    if (slot == AX_POOL_NIL) {
        return AX_ERR_INVALID_HANDLE;
    }
    s = &pool->slots[slot];

#if AX_ENABLE_ASSERTS
    memset(element_at(pool, slot), AX_POOL_POISON, pool->elem_size);
#endif

    pool->live_count--;
    s->generation++;
    if (s->generation == 0) {
        s->next_free = AX_POOL_RETIRED; /* every generation used: never hand this slot out again */
    } else {
        s->next_free = pool->free_head;
        pool->free_head = slot;
    }
    return AX_OK;
}

ax_result ax_pool_get(const ax_pool *pool, uint64_t handle, void **out_ptr)
{
    uint32_t slot;
    AX_ASSERT(pool != NULL && out_ptr != NULL);
    slot = live_slot(pool, handle);
    if (slot == AX_POOL_NIL) {
        *out_ptr = NULL;
        return AX_ERR_INVALID_HANDLE;
    }
    *out_ptr = element_at(pool, slot);
    return AX_OK;
}

bool ax_pool_is_valid(const ax_pool *pool, uint64_t handle)
{
    return live_slot(pool, handle) != AX_POOL_NIL;
}
