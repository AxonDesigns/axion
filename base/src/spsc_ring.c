/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lock-free single-producer, single-consumer ring of fixed-size elements.
 */
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_spsc_ring.h"

ax_result ax_spsc_ring_init(ax_spsc_ring *r, const ax_allocator *allocator, uint32_t capacity,
                            uint32_t elem_size)
{
    uint32_t cap = 2;
    AX_ASSERT(r != NULL && elem_size > 0);
    memset(r, 0, sizeof *r);
    r->allocator = *(allocator != NULL ? allocator : ax_default_allocator());
    while (cap < capacity && cap < 0x80000000u) {
        cap <<= 1;
    }
    r->slots = (unsigned char *)ax_mem_alloc(&r->allocator, (size_t)cap * elem_size, AX_MAX_ALIGN);
    if (r->slots == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_spsc_ring_init: out of memory");
    }
    r->mask = cap - 1u;
    r->elem_size = elem_size;
    ax_atomic_init_u32(&r->head, 0);
    ax_atomic_init_u32(&r->tail, 0);
    return AX_OK;
}

void ax_spsc_ring_deinit(ax_spsc_ring *r)
{
    if (r->slots != NULL) {
        ax_mem_free(&r->allocator, r->slots, (size_t)(r->mask + 1u) * r->elem_size);
    }
    memset(r, 0, sizeof *r);
}

bool ax_spsc_ring_push(ax_spsc_ring *r, const void *elem)
{
    uint32_t tail = ax_atomic_load_u32(&r->tail, AX_MO_RELAXED);
    uint32_t head = ax_atomic_load_u32(&r->head, AX_MO_ACQUIRE);
    if (tail - head > r->mask) {
        return false;
    }
    memcpy(r->slots + (size_t)(tail & r->mask) * r->elem_size, elem, r->elem_size);
    ax_atomic_store_u32(&r->tail, tail + 1u, AX_MO_RELEASE);
    return true;
}

bool ax_spsc_ring_pop(ax_spsc_ring *r, void *out)
{
    uint32_t head = ax_atomic_load_u32(&r->head, AX_MO_RELAXED);
    uint32_t tail = ax_atomic_load_u32(&r->tail, AX_MO_ACQUIRE);
    if (head == tail) {
        return false;
    }
    memcpy(out, r->slots + (size_t)(head & r->mask) * r->elem_size, r->elem_size);
    ax_atomic_store_u32(&r->head, head + 1u, AX_MO_RELEASE);
    return true;
}

uint32_t ax_spsc_ring_count(const ax_spsc_ring *r)
{
    uint32_t tail = ax_atomic_load_u32(&r->tail, AX_MO_ACQUIRE);
    uint32_t head = ax_atomic_load_u32(&r->head, AX_MO_ACQUIRE);
    return tail - head;
}
