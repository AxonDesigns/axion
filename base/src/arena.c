/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <string.h>

#include "axion/base/ax_arena.h"
#include "axion/base/ax_assert.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_memory.h"

/* Block header; the payload starts at the next max_align_t boundary after it. */
struct ax_arena_block {
    ax_arena_block *next;
    size_t capacity;   /* payload bytes */
    size_t alloc_size; /* bytes requested from the parent, needed to free the block */
};

#define AX_ARENA_PAYLOAD_ALIGN AX_ALIGNOF(max_align_t)
#define AX_ARENA_HEADER_SIZE \
    ((sizeof(ax_arena_block) + AX_ARENA_PAYLOAD_ALIGN - 1) & ~(size_t)(AX_ARENA_PAYLOAD_ALIGN - 1))

static unsigned char *payload(ax_arena_block *b)
{
    return (unsigned char *)b + AX_ARENA_HEADER_SIZE;
}

/* Offset in b at which an allocation of size/align would start, or SIZE_MAX if it doesn't fit. */
static size_t fit(ax_arena_block *b, size_t offset, size_t size, size_t align)
{
    uintptr_t base = (uintptr_t)payload(b);
    uintptr_t start = ax_align_up_uptr(base + offset, align);
    size_t aligned_offset = (size_t)(start - base);
    if (aligned_offset > b->capacity || size > b->capacity - aligned_offset) {
        return SIZE_MAX;
    }
    return aligned_offset;
}

static ax_arena_block *new_block(ax_arena *arena, size_t size, size_t align)
{
    size_t capacity = arena->block_size;
    size_t needed;
    size_t total;
    ax_arena_block *b;

    /* Room for size bytes even in the worst case of alignment padding beyond max_align_t. */
    if (!ax_size_add(size, align > AX_ARENA_PAYLOAD_ALIGN ? align - 1 : 0, &needed)) {
        return NULL;
    }
    if (needed > capacity) {
        capacity = needed;
    }
    if (!ax_size_add(capacity, AX_ARENA_HEADER_SIZE, &total)) {
        return NULL;
    }
    b = (ax_arena_block *)ax_mem_alloc(&arena->parent, total, AX_ARENA_PAYLOAD_ALIGN);
    if (b == NULL) {
        return NULL;
    }
    b->next = NULL;
    b->capacity = capacity;
    b->alloc_size = total;
    return b;
}

void ax_arena_init(ax_arena *arena, const ax_allocator *parent, size_t block_size)
{
    AX_ASSERT(arena != NULL && parent != NULL);
    arena->parent = *parent;
    arena->first = NULL;
    arena->current = NULL;
    arena->offset = 0;
    arena->block_size = block_size != 0 ? block_size : AX_ARENA_DEFAULT_BLOCK_SIZE;
}

void ax_arena_deinit(ax_arena *arena)
{
    ax_arena_block *b = arena->first;
    while (b != NULL) {
        ax_arena_block *next = b->next;
        ax_mem_free(&arena->parent, b, b->alloc_size);
        b = next;
    }
    arena->first = NULL;
    arena->current = NULL;
    arena->offset = 0;
}

void *ax_arena_alloc(ax_arena *arena, size_t size, size_t align)
{
    ax_arena_block *b;
    size_t at;

    AX_ASSERT(arena != NULL);
    AX_ASSERT_MSG(ax_is_pow2(align), "arena: align must be a power of two");
    if (!ax_is_pow2(align)) {
        return NULL;
    }

    /* Fast path: the current block. */
    if (arena->current != NULL) {
        at = fit(arena->current, arena->offset, size, align);
        if (at != SIZE_MAX) {
            arena->offset = at + size;
            return payload(arena->current) + at;
        }
    }

    /* Reuse blocks kept from before a reset or restore, skipping any too small for this request.
     * Skipped blocks stay in the chain and are reused after the next reset. */
    b = arena->current != NULL ? arena->current->next : arena->first;
    while (b != NULL) {
        at = fit(b, 0, size, align);
        if (at != SIZE_MAX) {
            arena->current = b;
            arena->offset = at + size;
            return payload(b) + at;
        }
        b = b->next;
    }

    /* Allocate a new block and link it right after the current one, so the blocks still ahead
     * in the chain stay reachable. */
    b = new_block(arena, size, align);
    if (b == NULL) {
        return NULL;
    }
    if (arena->current == NULL) {
        b->next = arena->first;
        arena->first = b;
    } else {
        b->next = arena->current->next;
        arena->current->next = b;
    }
    at = fit(b, 0, size, align);
    AX_ASSERT(at != SIZE_MAX);
    arena->current = b;
    arena->offset = at + size;
    return payload(b) + at;
}

void *ax_arena_alloc_zero(ax_arena *arena, size_t size, size_t align)
{
    void *p = ax_arena_alloc(arena, size, align);
    if (p != NULL && size != 0) {
        memset(p, 0, size);
    }
    return p;
}

void *ax_arena_alloc_array(ax_arena *arena, size_t elem_size, size_t elem_align, size_t count)
{
    size_t size;
    if (!ax_size_mul(elem_size, count, &size)) {
        return NULL;
    }
    return ax_arena_alloc_zero(arena, size, elem_align);
}

void ax_arena_reset(ax_arena *arena)
{
    arena->current = arena->first;
    arena->offset = 0;
}

ax_arena_mark ax_arena_save(const ax_arena *arena)
{
    ax_arena_mark mark;
    mark.block = arena->current;
    mark.offset = arena->offset;
    return mark;
}

void ax_arena_restore(ax_arena *arena, ax_arena_mark mark)
{
    if (mark.block == NULL) {
        ax_arena_reset(arena); /* saved before the first allocation */
        return;
    }
    arena->current = mark.block;
    arena->offset = mark.offset;
}

size_t ax_arena_capacity(const ax_arena *arena)
{
    size_t total = 0;
    const ax_arena_block *b;
    for (b = arena->first; b != NULL; b = b->next) {
        total += b->capacity;
    }
    return total;
}

/* ---- ax_allocator view ----------------------------------------------------------------------- */

static void *arena_view_alloc(void *ctx, size_t size, size_t align)
{
    return ax_arena_alloc((ax_arena *)ctx, size, align);
}

static void *arena_view_resize(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align)
{
    ax_arena *arena = (ax_arena *)ctx;
    void *q;

    /* In place when ptr is the most recent allocation and the block has room. */
    if (arena->current != NULL) {
        /* Compared as integers: ptr may belong to another block, and relational comparison of
         * pointers into different objects is undefined in C. */
        uintptr_t base = (uintptr_t)payload(arena->current);
        uintptr_t p = (uintptr_t)ptr;
        if (p >= base && p - base + old_size == arena->offset) {
            size_t at = (size_t)(p - base);
            if (new_size <= arena->current->capacity - at) {
                arena->offset = at + new_size;
                return ptr;
            }
        }
    }
    q = ax_arena_alloc(arena, new_size, align);
    if (q != NULL) {
        memcpy(q, ptr, old_size < new_size ? old_size : new_size);
    }
    return q;
}

static void arena_view_free(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    (void)ptr;
    (void)size;
}

ax_allocator ax_arena_allocator(ax_arena *arena)
{
    ax_allocator a;
    a.alloc = arena_view_alloc;
    a.resize = arena_view_resize;
    a.free = arena_view_free;
    a.ctx = arena;
    return a;
}
