/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: linear (bump) arena, the building block of per-thread frame arenas.
 *
 *     ax_arena frame;
 *     ax_arena_init(&frame, allocator, 64 * 1024);
 *     for (;;) {
 *         float *xs = AX_ARENA_NEW_ARRAY(&frame, float, n);
 *         ...
 *         ax_arena_reset(&frame);     // O(1): every block is kept for the next frame
 *     }
 *     ax_arena_deinit(&frame);        // returns the blocks to the parent allocator
 *
 * Memory comes from the parent allocator in blocks of at least block_size bytes, allocated on
 * first use and chained. When the current block is full the arena moves to the next block in the
 * chain, or allocates a new one (larger than block_size if a single request needs it). Reset and
 * restore never free blocks, so a steady-state frame allocates nothing from the parent.
 *
 * Individual allocations are never freed; save/restore marks free everything allocated after the
 * mark at once, in LIFO order, for scratch work inside a frame.
 *
 * An ax_arena is owned by one thread at a time; it does no locking.
 */
#ifndef AX_BASE_ARENA_H
#define AX_BASE_ARENA_H

#include <stddef.h>

#include "../ax_allocator.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

typedef struct ax_arena_block ax_arena_block;

typedef struct ax_arena {
    ax_allocator parent;
    ax_arena_block *first;   /* head of the chain, NULL until the first allocation */
    ax_arena_block *current; /* block being bumped into */
    size_t offset;           /* bytes used in current */
    size_t block_size;       /* minimum payload of a new block */
} ax_arena;

/* A position in the arena, to return to with ax_arena_restore. Plain value; copy freely. */
typedef struct ax_arena_mark {
    ax_arena_block *block;
    size_t offset;
} ax_arena_mark;

#define AX_ARENA_DEFAULT_BLOCK_SIZE ((size_t)64 * 1024)

/* parent is copied and must outlive the arena. block_size 0 selects the default. Never fails:
 * nothing is allocated until the first ax_arena_alloc. */
AX_API void ax_arena_init(ax_arena *arena, const ax_allocator *parent, size_t block_size);

/* Returns every block to the parent. The arena can be initialized again afterwards. */
AX_API void ax_arena_deinit(ax_arena *arena);

/* size bytes aligned to align (a power of two), uninitialized; NULL if the parent fails.
 * size 0 returns a valid, aligned, non-NULL pointer that must not be dereferenced. */
AX_API void *ax_arena_alloc(ax_arena *arena, size_t size, size_t align);

/* Like ax_arena_alloc, with the bytes zeroed. */
AX_API void *ax_arena_alloc_zero(ax_arena *arena, size_t size, size_t align);

/* Forgets every allocation in O(1), keeping all blocks for reuse. */
AX_API void ax_arena_reset(ax_arena *arena);

/* Saves the current position. */
AX_API ax_arena_mark ax_arena_save(const ax_arena *arena);

/* Forgets every allocation made since mark was saved. Marks must be restored in LIFO order, and
 * a mark is invalidated by restoring an older one or by ax_arena_reset. */
AX_API void ax_arena_restore(ax_arena *arena, ax_arena_mark mark);

/* Total bytes of blocks owned by the arena (capacity, not usage). */
AX_API size_t ax_arena_capacity(const ax_arena *arena);

/*
 * An ax_allocator view of the arena, for code that takes an allocator. free() does nothing;
 * resize() grows or shrinks in place when ptr is the most recent allocation and otherwise copies
 * into a new allocation. The view refers to arena, which must not move while it is in use.
 */
AX_API ax_allocator ax_arena_allocator(ax_arena *arena);

#define AX_ARENA_NEW(arena, T) ((T *)ax_arena_alloc_zero((arena), sizeof(T), AX_ALIGNOF(T)))

/* Zeroed array of n elements of T; NULL on failure or if n * sizeof(T) overflows. */
#define AX_ARENA_NEW_ARRAY(arena, T, n) \
    ((T *)ax_arena_alloc_array((arena), sizeof(T), AX_ALIGNOF(T), (n)))

AX_API void *ax_arena_alloc_array(ax_arena *arena, size_t elem_size, size_t elem_align,
                                  size_t count);

AX_EXTERN_C_END

#endif /* AX_BASE_ARENA_H */
