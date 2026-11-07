/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: a lock-free single-producer, single-consumer ring of fixed-size elements.
 *
 *     ax_spsc_ring r;
 *     ax_spsc_ring_init(&r, allocator, 256, sizeof(uint64_t));
 *     ax_spsc_ring_push(&r, &value);    // producer thread; false when full
 *     ax_spsc_ring_pop(&r, &value);     // consumer thread; false when empty
 *
 * Elements are copied in and out. The producer writes a slot and then publishes the tail with
 * release order; the consumer reads the tail with acquire order, copies the slot out and then
 * publishes the head with release order, so neither side ever reads a slot the other is writing.
 * Memory is reserved at init; push and pop never allocate, block or fail except on full/empty.
 * One producer thread and one consumer thread at a time.
 */
#ifndef AX_BASE_SPSC_RING_H
#define AX_BASE_SPSC_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_allocator.h"
#include "../ax_types.h"
#include "ax_atomic.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

typedef struct ax_spsc_ring {
    ax_allocator allocator;
    unsigned char *slots;
    uint32_t mask; /* capacity - 1 */
    uint32_t elem_size;
    /* Padding keeps the producer's and the consumer's indices on separate cache lines without an
     * alignment specifier (which MSVC warns about in every includer). */
    unsigned char pad0_[64];
    ax_atomic_u32 head; /* next slot to read; written by the consumer */
    unsigned char pad1_[64];
    ax_atomic_u32 tail; /* next slot to write; written by the producer */
    unsigned char pad2_[64];
} ax_spsc_ring;

/* capacity is rounded up to a power of two (at least 2); elem_size > 0. Errors:
 * AX_ERR_OUT_OF_MEMORY (with ax_last_error). */
AX_API ax_result ax_spsc_ring_init(ax_spsc_ring *r, const ax_allocator *allocator,
                                   uint32_t capacity, uint32_t elem_size);
AX_API void ax_spsc_ring_deinit(ax_spsc_ring *r);

static inline uint32_t ax_spsc_ring_capacity(const ax_spsc_ring *r)
{
    return r->mask + 1u;
}

/* Producer: copies elem in. False when the ring is full. */
AX_API bool ax_spsc_ring_push(ax_spsc_ring *r, const void *elem);

/* Consumer: copies the oldest element out. False when the ring is empty. */
AX_API bool ax_spsc_ring_pop(ax_spsc_ring *r, void *out);

/* Elements queued, as seen from either side (approximate while the other side runs). */
AX_API uint32_t ax_spsc_ring_count(const ax_spsc_ring *r);

AX_EXTERN_C_END

#endif /* AX_BASE_SPSC_RING_H */
