/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: alignment arithmetic and checked calls through an ax_allocator.
 *
 * Module code calls allocators through ax_mem_alloc / ax_mem_resize / ax_mem_free rather than
 * through the function pointers, so the contract in ax_allocator.h is asserted at every call site
 * in debug builds, whichever allocator the host supplied.
 */
#ifndef AX_BASE_MEMORY_H
#define AX_BASE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../ax_allocator.h"
#include "ax_assert.h"
#include "ax_compiler.h"

static inline bool ax_is_pow2(size_t x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

/* Rounds x up to a multiple of align (a power of two). Returns false on overflow. */
static inline bool ax_align_up_checked(size_t x, size_t align, size_t *out)
{
    AX_ASSERT(ax_is_pow2(align));
    if (x > SIZE_MAX - (align - 1)) {
        return false;
    }
    *out = (x + (align - 1)) & ~(align - 1);
    return true;
}

/* Rounds x up to a multiple of align (a power of two). The caller guarantees no overflow. */
static inline size_t ax_align_up(size_t x, size_t align)
{
    AX_ASSERT(ax_is_pow2(align));
    AX_ASSERT(x <= SIZE_MAX - (align - 1));
    return (x + (align - 1)) & ~(align - 1);
}

static inline uintptr_t ax_align_up_uptr(uintptr_t x, size_t align)
{
    AX_ASSERT(ax_is_pow2(align));
    return (x + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
}

static inline bool ax_is_aligned(const void *ptr, size_t align)
{
    return ((uintptr_t)ptr & (uintptr_t)(align - 1)) == 0;
}

/* *out = a + b; false on overflow. */
static inline bool ax_size_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

/* *out = a * b; false on overflow. */
static inline bool ax_size_mul(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

static inline void *ax_mem_alloc(const ax_allocator *a, size_t size, size_t align)
{
    AX_ASSERT(a != NULL);
    AX_ASSERT_MSG(size > 0, "allocator contract: size must be > 0");
    AX_ASSERT_MSG(ax_is_pow2(align), "allocator contract: align must be a power of two");
    return a->alloc(a->ctx, size, align);
}

static inline void *ax_mem_resize(const ax_allocator *a, void *ptr, size_t old_size,
                                  size_t new_size, size_t align)
{
    AX_ASSERT(a != NULL);
    AX_ASSERT_MSG(ptr != NULL, "allocator contract: resize needs a live block");
    AX_ASSERT_MSG(old_size > 0 && new_size > 0, "allocator contract: sizes must be > 0");
    AX_ASSERT_MSG(ax_is_pow2(align), "allocator contract: align must be a power of two");
    AX_ASSERT_MSG(ax_is_aligned(ptr, align), "allocator contract: align differs from the block's");
    return a->resize(a->ctx, ptr, old_size, new_size, align);
}

static inline void ax_mem_free(const ax_allocator *a, void *ptr, size_t size)
{
    AX_ASSERT(a != NULL);
    if (ptr != NULL) {
        a->free(a->ctx, ptr, size);
    }
}

/* Allocates one object of type T, uninitialized. */
#define AX_MEM_NEW(allocator, T)      ((T *)ax_mem_alloc((allocator), sizeof(T), AX_ALIGNOF(T)))
#define AX_MEM_DELETE(allocator, ptr) ax_mem_free((allocator), (ptr), sizeof *(ptr))

#endif /* AX_BASE_MEMORY_H */
