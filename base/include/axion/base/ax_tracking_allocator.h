/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: an allocator that counts bytes and finds leaks and contract violations, for tests.
 *
 *     ax_tracking_allocator tracker;
 *     ax_tracking_allocator_init(&tracker, ax_default_allocator());
 *     ax_allocator a = ax_tracking_allocator_get(&tracker);
 *     ... use &a ...
 *     size_t leaked = ax_tracking_allocator_deinit(&tracker);   // 0 when nothing leaked
 *
 * It forwards to a parent allocator and keeps every live block on a list, which lets it check:
 *   - free and resize receive a pointer it handed out and that is still live (catches double
 *     frees and foreign pointers before they reach the parent),
 *   - the size passed to free/resize equals the block's size,
 *   - size > 0 and align is a power of two.
 * A violation is reported to stderr and aborts, unless abort_on_misuse is set to false, in which
 * case it is counted in misuse_count and the call is rejected (or, for a size mismatch, carried
 * out with the recorded size). The checks are on in every build type.
 *
 * Not thread-safe: use one tracker per thread, or serialize access. free and resize cost
 * O(number of live blocks); this is a testing tool.
 */
#ifndef AX_BASE_TRACKING_ALLOCATOR_H
#define AX_BASE_TRACKING_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "../ax_allocator.h"

AX_EXTERN_C_BEGIN

typedef struct ax_tracking_block ax_tracking_block;

typedef struct ax_tracking_allocator {
    ax_allocator parent;
    ax_tracking_block *live; /* most recent first */

    size_t live_bytes;   /* bytes requested by blocks still alive */
    size_t live_count;   /* blocks still alive */
    size_t peak_bytes;   /* highest live_bytes seen */
    size_t alloc_count;  /* successful alloc calls */
    size_t resize_count; /* successful resize calls */
    size_t free_count;   /* free calls that released a block */
    size_t misuse_count; /* contract violations seen with abort_on_misuse == false */

    bool abort_on_misuse; /* true after init */
} ax_tracking_allocator;

/* parent is copied; it must outlive the tracker. */
AX_API void ax_tracking_allocator_init(ax_tracking_allocator *t, const ax_allocator *parent);

/* The ax_allocator interface of t. It refers to t, so t must not move while it is in use. */
AX_API ax_allocator ax_tracking_allocator_get(ax_tracking_allocator *t);

/* Prints every leaked block to stderr, returns them to the parent and returns how many leaked. */
AX_API size_t ax_tracking_allocator_deinit(ax_tracking_allocator *t);

AX_EXTERN_C_END

#endif /* AX_BASE_TRACKING_ALLOCATOR_H */
