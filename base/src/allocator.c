/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The default allocator over the C heap. This file is the only place in AXION that calls malloc,
 * realloc or free.
 *
 * ax_allocator's free() does not receive the alignment, so free() cannot tell whether a block
 * was over-aligned. Every block therefore carries the same small header right before the user
 * pointer, whatever its alignment:
 *
 *     raw (from malloc)                 user (returned, aligned to max(align, header align))
 *     |<-- padding -->|<-- header -->|<-- size bytes -->|
 *
 * The header records the raw pointer (to hand back to free/realloc) and the requested size (to
 * check the contract in debug builds).
 */
#include <stdlib.h>
#include <string.h>

#include "axion/ax_allocator.h"
#include "axion/base/ax_assert.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_memory.h"

typedef struct ax_heap_header {
    void *raw;
    size_t size;
} ax_heap_header;

#define AX_HEAP_HEADER_ALIGN AX_ALIGNOF(ax_heap_header)

AX_STATIC_ASSERT(sizeof(ax_heap_header) % AX_ALIGNOF(ax_heap_header) == 0,
                 "header size keeps the header aligned right below the user pointer");

static ax_heap_header *heap_header(void *user)
{
    return (ax_heap_header *)((unsigned char *)user - sizeof(ax_heap_header));
}

/* Bytes to request from malloc for size bytes at alignment ea, or 0 on overflow. */
static size_t heap_total(size_t size, size_t ea)
{
    size_t total;
    if (!ax_size_add(size, sizeof(ax_heap_header), &total) || !ax_size_add(total, ea - 1, &total)) {
        return 0;
    }
    return total;
}

static void *heap_place(void *raw, size_t ea)
{
    uintptr_t first = (uintptr_t)raw + sizeof(ax_heap_header);
    return (void *)ax_align_up_uptr(first, ea);
}

static void *heap_alloc(void *ctx, size_t size, size_t align)
{
    size_t ea;
    size_t total;
    void *raw;
    void *user;

    (void)ctx;
    AX_ASSERT_MSG(size > 0, "allocator contract: size must be > 0");
    AX_ASSERT_MSG(ax_is_pow2(align), "allocator contract: align must be a power of two");
    if (size == 0 || !ax_is_pow2(align)) {
        return NULL;
    }

    ea = align > AX_HEAP_HEADER_ALIGN ? align : AX_HEAP_HEADER_ALIGN;
    total = heap_total(size, ea);
    if (total == 0) {
        return NULL;
    }
    raw = malloc(total);
    if (raw == NULL) {
        return NULL;
    }
    user = heap_place(raw, ea);
    heap_header(user)->raw = raw;
    heap_header(user)->size = size;
    return user;
}

static void *heap_resize(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align)
{
    ax_heap_header header;
    size_t ea;
    size_t total;
    size_t old_offset;
    size_t new_offset;
    unsigned char *new_raw;
    unsigned char *user;

    (void)ctx;
    AX_ASSERT_MSG(ptr != NULL, "allocator contract: resize needs a live block");
    AX_ASSERT_MSG(new_size > 0, "allocator contract: size must be > 0");
    AX_ASSERT_MSG(ax_is_pow2(align), "allocator contract: align must be a power of two");
    if (ptr == NULL || new_size == 0 || !ax_is_pow2(align)) {
        return NULL;
    }
    header = *heap_header(ptr);
    AX_ASSERT_MSG(header.size == old_size, "allocator contract: old_size differs from the block's");
    old_size = header.size; /* trust the recorded size in release builds */

    ea = align > AX_HEAP_HEADER_ALIGN ? align : AX_HEAP_HEADER_ALIGN;
    total = heap_total(new_size, ea);
    if (total == 0) {
        return NULL;
    }

    old_offset = (size_t)((unsigned char *)ptr - (unsigned char *)header.raw);
    new_raw = (unsigned char *)realloc(header.raw, total);
    if (new_raw == NULL) {
        return NULL; /* realloc left the original block intact */
    }

    /* realloc keeps bytes, not alignment: if the aligned position moved within the new block,
     * slide the payload there. old_offset + min(old, new) always fits inside total. */
    user = (unsigned char *)heap_place(new_raw, ea);
    new_offset = (size_t)(user - new_raw);
    if (new_offset != old_offset) {
        memmove(user, new_raw + old_offset, old_size < new_size ? old_size : new_size);
    }
    heap_header(user)->raw = new_raw;
    heap_header(user)->size = new_size;
    return user;
}

static void heap_free(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    (void)size;
    if (ptr == NULL) {
        return;
    }
    AX_ASSERT_MSG(heap_header(ptr)->size == size, "allocator contract: free size differs");
    free(heap_header(ptr)->raw);
}

static const ax_allocator ax_heap_allocator = {heap_alloc, heap_resize, heap_free, NULL};

const ax_allocator *ax_default_allocator(void)
{
    return &ax_heap_allocator;
}
