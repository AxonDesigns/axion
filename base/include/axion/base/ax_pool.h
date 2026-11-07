/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: a pool of fixed-size slots addressed by generational handles.
 *
 *     ax_pool nodes;
 *     ax_pool_init(&nodes, allocator, sizeof(node_data), AX_ALIGNOF(node_data), 0);
 *
 *     uint64_t h; node_data *n;
 *     if (ax_pool_alloc(&nodes, &h, (void **)&n) != AX_OK) { ... }   // n is zeroed
 *     ax_node node = {h};
 *
 *     ax_pool_get(&nodes, node.h, (void **)&n);   // AX_ERR_INVALID_HANDLE once freed
 *     ax_pool_free(&nodes, node.h);
 *
 * Storage. Elements live in pages of elems_per_page slots, allocated from the pool's allocator
 * as needed and kept until ax_pool_deinit. An element's address never changes while it is live,
 * but must not be kept across ax_pool_free: hold the handle and look it up again.
 *
 * Handles. Slot generations start at 1 and increase every time the slot is freed. A handle is
 * valid only while its slot is live with the same generation, so a stale handle, including one
 * whose slot has since been recycled for a new object, returns AX_ERR_INVALID_HANDLE from
 * ax_pool_get and ax_pool_free. A slot whose generation would wrap past 0xFFFFFFFF is retired
 * and never reused, so a stale handle can never match again. Freed slots are recycled LIFO.
 *
 * The pool reports AX_ERR_INVALID_HANDLE without asserting: stale handles are an expected event
 * (late releases from garbage-collected bindings), and whether one is a bug is decided by the
 * public API built on top. It does not record ax_last_error either; callers do.
 *
 * An ax_pool is owned by one thread at a time; it does no locking.
 */
#ifndef AX_BASE_POOL_H
#define AX_BASE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../ax_allocator.h"
#include "../ax_handle.h"
#include "../ax_types.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

/* next_free values that are not slot indices. */
#define AX_POOL_NIL     0xFFFFFFFFu /* end of the free list */
#define AX_POOL_LIVE    0xFFFFFFFEu /* the slot holds a live element */
#define AX_POOL_RETIRED 0xFFFFFFFDu /* generation exhausted; never reused */

/* Largest number of slots a pool can create. */
#define AX_POOL_MAX_SLOTS 0xFFFFFFF0u

#define AX_POOL_DEFAULT_ELEMS_PER_PAGE 64u

typedef struct ax_pool_slot {
    uint32_t generation; /* generation of the current (or next) occupant; never 0 */
    uint32_t next_free;  /* next slot in the free list, or one of the AX_POOL_* markers */
} ax_pool_slot;

typedef struct ax_pool {
    ax_allocator allocator;
    size_t elem_size;
    size_t elem_align;
    size_t stride;       /* elem_size rounded up to elem_align */
    uint32_t page_shift; /* log2(elements per page) */

    ax_pool_slot *slots;    /* one per created slot */
    uint32_t slot_count;    /* slots created so far */
    uint32_t slot_capacity; /* length of slots */
    uint32_t free_head;     /* first free slot, or AX_POOL_NIL */
    uint32_t live_count;

    unsigned char **pages;
    uint32_t page_count;
    uint32_t page_capacity;
} ax_pool;

/*
 * allocator is copied and must outlive the pool. elem_size > 0, elem_align a power of two,
 * elems_per_page a power of two (0 selects AX_POOL_DEFAULT_ELEMS_PER_PAGE). Never fails: memory
 * is allocated on the first ax_pool_alloc.
 */
AX_API void ax_pool_init(ax_pool *pool, const ax_allocator *allocator, size_t elem_size,
                         size_t elem_align, uint32_t elems_per_page);

/* Releases all pages, live elements included. Handles from this pool become meaningless. */
AX_API void ax_pool_deinit(ax_pool *pool);

/* Creates a zeroed element. out_ptr may be NULL. On failure (AX_ERR_OUT_OF_MEMORY) the pool is
 * unchanged and *out_handle is 0. */
AX_API ax_result ax_pool_alloc(ax_pool *pool, uint64_t *out_handle, void **out_ptr);

/* Destroys the element; AX_ERR_INVALID_HANDLE if handle is null, stale or from elsewhere. */
AX_API ax_result ax_pool_free(ax_pool *pool, uint64_t handle);

/* Looks up a live element; on AX_ERR_INVALID_HANDLE *out_ptr is NULL. */
AX_API ax_result ax_pool_get(const ax_pool *pool, uint64_t handle, void **out_ptr);

AX_API bool ax_pool_is_valid(const ax_pool *pool, uint64_t handle);

static inline uint32_t ax_pool_live_count(const ax_pool *pool)
{
    return pool->live_count;
}

AX_EXTERN_C_END

#endif /* AX_BASE_POOL_H */
