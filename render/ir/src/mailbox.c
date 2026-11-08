/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Render IR publication: three builders behind a triple buffer.
 *
 * Waking the reader is a Dekker handshake, as in the platform's event queue (decision 31): the
 * writer stores the new sequence, then reads `reader_waits`; the reader sets `reader_waits`,
 * then reads the sequence. Both with sequentially consistent operations, so at least one of them
 * sees the other's store: either the reader sees the new snapshot and does not sleep, or the
 * writer sees the reader and signals. The writer takes the mutex only when a reader is actually
 * waiting, and only for the handoff.
 */
#include <string.h>

#include "axion/render/ax_ir_mailbox.h"

void ax_ir_mailbox_init(ax_ir_mailbox *mb, const ax_allocator *parent)
{
    int i;
    memset(mb, 0, sizeof *mb);
    for (i = 0; i < 3; ++i) {
        ax_ir_builder_init(&mb->slots[i], parent);
    }
    ax_tb_init(&mb->tb);
    mb->next_sequence = 1;
    ax_atomic_init_u64(&mb->published, 0);
    ax_atomic_init_u32(&mb->reader_waits, 0);
    ax_mutex_init(&mb->mutex);
    ax_cond_init(&mb->cond);
}

void ax_ir_mailbox_deinit(ax_ir_mailbox *mb)
{
    int i;
    for (i = 0; i < 3; ++i) {
        ax_ir_builder_deinit(&mb->slots[i]);
    }
    ax_cond_deinit(&mb->cond);
    ax_mutex_deinit(&mb->mutex);
    memset(mb, 0, sizeof *mb);
}

ax_ir_builder *ax_ir_mailbox_begin(ax_ir_mailbox *mb, float logical_width, float logical_height,
                                   uint64_t frame_time_ns, ax_color background)
{
    const uint32_t slot = ax_tb_write_index(&mb->tb);
    ax_ir_builder *b = &mb->slots[slot];
    mb->snapshot[slot] = NULL; /* the writer owns this slot; its old snapshot is gone */
    ax_ir_builder_begin(b, logical_width, logical_height, frame_time_ns, background);
    return b;
}

ax_result ax_ir_mailbox_publish(ax_ir_mailbox *mb)
{
    const uint32_t slot = ax_tb_write_index(&mb->tb);
    ax_ir_builder *b = &mb->slots[slot];
    const ax_render_ir *ir;
    const uint64_t seq = mb->next_sequence;
    ax_result r = ax_ir_builder_end(b, &ir);
    if (r != AX_OK) {
        return r;
    }
    b->ir.sequence = seq; /* ir points at b->ir */
    mb->snapshot[slot] = ir;
    mb->next_sequence++;
    ax_tb_publish(&mb->tb); /* seq_cst exchange: releases the snapshot to the reader */
    ax_atomic_store_u64(&mb->published, seq, AX_MO_SEQ_CST);
    if (ax_atomic_load_u32(&mb->reader_waits, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&mb->mutex);
        ax_cond_signal(&mb->cond);
        ax_mutex_unlock(&mb->mutex);
    }
    return AX_OK;
}

const ax_render_ir *ax_ir_mailbox_acquire(ax_ir_mailbox *mb)
{
    return mb->snapshot[ax_tb_acquire(&mb->tb)];
}

uint64_t ax_ir_mailbox_published(const ax_ir_mailbox *mb)
{
    return ax_atomic_load_u64(&mb->published, AX_MO_ACQUIRE);
}

bool ax_ir_mailbox_wait(ax_ir_mailbox *mb, uint64_t after_sequence, uint64_t timeout_ns)
{
    uint64_t start, now;
    bool available;
    if (ax_atomic_load_u64(&mb->published, AX_MO_SEQ_CST) > after_sequence) {
        return true;
    }
    if (timeout_ns == 0 || !ax_threads_supported()) {
        return false;
    }
    start = ax_time_monotonic_ns();
    ax_mutex_lock(&mb->mutex);
    ax_atomic_store_u32(&mb->reader_waits, 1, AX_MO_SEQ_CST);
    for (;;) {
        available = ax_atomic_load_u64(&mb->published, AX_MO_SEQ_CST) > after_sequence;
        if (available) {
            break;
        }
        now = ax_time_monotonic_ns();
        if (now - start >= timeout_ns) {
            break;
        }
        ax_cond_wait_timeout(&mb->cond, &mb->mutex, timeout_ns - (now - start));
    }
    ax_atomic_store_u32(&mb->reader_waits, 0, AX_MO_RELAXED);
    ax_mutex_unlock(&mb->mutex);
    return available;
}

uint64_t ax_ir_mailbox_oldest_epoch(const ax_ir_mailbox *mb)
{
    /* The writer alone writes snapshot[]: a slot keeps its snapshot until the writer begins in it,
     * which the triple buffer allows only once the reader has moved to another slot. */
    uint64_t oldest = UINT64_MAX;
    int i;
    for (i = 0; i < 3; ++i) {
        const ax_render_ir *ir = mb->snapshot[i];
        if (ir != NULL && ir->resource_epoch < oldest) {
            oldest = ir->resource_epoch;
        }
    }
    return oldest;
}
