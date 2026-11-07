/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: triple buffer. One writer thread publishes values, one reader thread takes the
 * newest, and neither ever waits for the other.
 *
 * The caller owns three value slots (an array of 3); this struct only tracks which slot each
 * side may touch:
 *   - the writer fills slot ax_tb_write_index(), then calls ax_tb_publish();
 *   - the reader calls ax_tb_acquire(), which returns the index of the newest published slot
 *     (or of the slot it already had, if nothing new was published) and reads it.
 * A slot is never touched by both sides at once, so the values need no atomics. The writer may
 * publish any number of times between reads; the reader always sees the latest.
 */
#ifndef AX_BASE_TRIPLE_BUFFER_H
#define AX_BASE_TRIPLE_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include "ax_atomic.h"

/* state: bits 0-1 hold the index of the shared ("middle") slot, bit 2 says it is fresh. */
#define AX_TB_INDEX_MASK 3u
#define AX_TB_FRESH      4u

typedef struct ax_triple_buffer {
    ax_atomic_u32 state;  /* shared */
    uint32_t write_index; /* writer only */
    uint32_t read_index;  /* reader only */
} ax_triple_buffer;

/* Slot 0 starts as the reader's, slot 1 as the writer's, slot 2 in the middle (not fresh). */
static inline void ax_tb_init(ax_triple_buffer *tb)
{
    ax_atomic_init_u32(&tb->state, 2u);
    tb->write_index = 1u;
    tb->read_index = 0u;
}

/* Sequentially consistent, so publish/acquire also order with other seq_cst operations around
 * them (the event queue's coalescing markers rely on this). */
static inline uint32_t ax_tb_exchange(ax_triple_buffer *tb, uint32_t desired)
{
    uint32_t seen = ax_atomic_load_u32(&tb->state, AX_MO_RELAXED);
    while (
        !ax_atomic_compare_exchange_u32(&tb->state, &seen, desired, AX_MO_SEQ_CST, AX_MO_RELAXED)) {
    }
    return seen;
}

/* Writer: the slot to fill next. */
static inline uint32_t ax_tb_write_index(const ax_triple_buffer *tb)
{
    return tb->write_index;
}

/* Writer: makes the filled slot the newest value and takes the old middle slot to write next. */
static inline void ax_tb_publish(ax_triple_buffer *tb)
{
    uint32_t old = ax_tb_exchange(tb, tb->write_index | AX_TB_FRESH);
    tb->write_index = old & AX_TB_INDEX_MASK;
}

/* Reader: true if something was published since the last acquire. */
static inline bool ax_tb_has_new(const ax_triple_buffer *tb)
{
    return (ax_atomic_load_u32(&tb->state, AX_MO_SEQ_CST) & AX_TB_FRESH) != 0;
}

/* Reader: the index of the newest value (taking a freshly published slot if there is one). */
static inline uint32_t ax_tb_acquire(ax_triple_buffer *tb)
{
    if (ax_tb_has_new(tb)) {
        uint32_t old = ax_tb_exchange(tb, tb->read_index);
        tb->read_index = old & AX_TB_INDEX_MASK;
    }
    return tb->read_index;
}

#endif /* AX_BASE_TRIPLE_BUFFER_H */
