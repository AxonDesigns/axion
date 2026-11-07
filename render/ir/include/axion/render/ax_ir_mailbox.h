/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: publishing Render IR snapshots from the scene thread to the render thread, as in
 * step 6 of the document's frame pipeline.
 *
 * Three snapshots, each recorded into its own builder (and arena), exchanged through a triple
 * buffer with an atomic index swap, mailbox style:
 *   - the scene (writer) records into the free slot and publishes it, never waiting for the render
 *     thread; a snapshot the render thread has not taken yet is simply replaced by the newer one;
 *   - the render thread (reader) always takes the newest published snapshot, and keeps using it
 *     until it takes another, while the writer can never touch it.
 *
 *     // scene thread                               // render thread
 *     b = ax_ir_mailbox_begin(mb, w, h, t, bg);     ir = ax_ir_mailbox_acquire(mb);
 *     ax_ir_rect(b, ...);                           if (ir != NULL) draw(ir);
 *     ax_ir_mailbox_publish(mb);                    ax_ir_mailbox_wait(mb, ir->sequence, timeout);
 *
 * One writer thread and one reader thread at a time.
 */
#ifndef AX_RENDER_IR_MAILBOX_H
#define AX_RENDER_IR_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/base/ax_atomic.h>
#include <axion/base/ax_thread.h>
#include <axion/base/ax_triple_buffer.h>

#include "ax_render_ir.h"

AX_EXTERN_C_BEGIN

/* Visible so it can be embedded; the fields are private. */
typedef struct ax_ir_mailbox {
    ax_ir_builder slots[3];
    const ax_render_ir *snapshot[3]; /* each slot's last finished snapshot, NULL if none */
    ax_triple_buffer tb;
    uint64_t next_sequence;     /* writer only */
    ax_atomic_u64 published;    /* sequence of the newest published snapshot, 0 = none */
    ax_atomic_u32 reader_waits; /* the reader is (about to be) asleep in ax_ir_mailbox_wait */
    ax_mutex mutex;             /* only for the wait handoff */
    ax_cond cond;
} ax_ir_mailbox;

/* parent is copied. Nothing is allocated until the first snapshot. thread: any */
AX_API void ax_ir_mailbox_init(ax_ir_mailbox *mb, const ax_allocator *parent);

/* Neither side may be using the mailbox. thread: any */
AX_API void ax_ir_mailbox_deinit(ax_ir_mailbox *mb);

/* Writer: starts a snapshot in the free slot and returns its builder (already begun). thread:
 * the writer */
AX_API ax_ir_builder *ax_ir_mailbox_begin(ax_ir_mailbox *mb, float logical_width,
                                          float logical_height, uint64_t frame_time_ns,
                                          ax_color background);

/*
 * Writer: finishes the snapshot (ax_ir_builder_end), gives it the next sequence number (from 1)
 * and publishes it, waking a reader waiting in ax_ir_mailbox_wait. Never waits for the reader.
 * On error nothing is published and the slot stays the writer's.
 * Errors: those of ax_ir_builder_end.
 * thread: the writer
 */
AX_API ax_result ax_ir_mailbox_publish(ax_ir_mailbox *mb);

/* Reader: the newest published snapshot, or the one it already had when nothing newer was
 * published; NULL before the first publish. Valid until the reader's next acquire. thread: the
 * reader */
AX_API const ax_render_ir *ax_ir_mailbox_acquire(ax_ir_mailbox *mb);

/* Sequence of the newest published snapshot (0 before the first). thread: any */
AX_API uint64_t ax_ir_mailbox_published(const ax_ir_mailbox *mb);

/*
 * Reader: waits until a snapshot newer than `after_sequence` is published, or `timeout_ns`
 * passes. Returns true when one is available. Without threads (ax_threads_supported() false) it
 * only checks, since nothing could publish while it waits.
 * thread: the reader
 */
AX_API bool ax_ir_mailbox_wait(ax_ir_mailbox *mb, uint64_t after_sequence, uint64_t timeout_ns);

AX_EXTERN_C_END

#endif /* AX_RENDER_IR_MAILBOX_H */
