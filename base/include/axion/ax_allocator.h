/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: the allocator interface every AXION module takes.
 *
 * AXION never allocates behind the caller's back: every module that needs memory receives an
 * ax_allocator, so a host (a game engine, a test harness) can account for every byte.
 *
 * Contract. An implementation may rely on it; AXION checks it with assertions in debug builds.
 *
 *   alloc(ctx, size, align)
 *     - size > 0, and align is a power of two (1, 2, 4, ...).
 *     - Returns a block of at least size bytes aligned to align, or NULL on failure.
 *       Contents are unspecified.
 *
 *   resize(ctx, ptr, old_size, new_size, align)
 *     - ptr is a live block from this allocator, old_size is its current size and align the
 *       alignment it was allocated with. new_size > 0.
 *     - Returns a block of new_size bytes holding the first min(old_size, new_size) bytes of the
 *       old one, aligned to align; it may be ptr itself. ptr is no longer valid unless returned.
 *     - On failure returns NULL, and ptr stays valid and unchanged.
 *
 *   free(ctx, ptr, size)
 *     - size is exactly the size the block was allocated (or last resized) with.
 *     - free(ctx, NULL, size) does nothing.
 *
 *   ctx is passed back untouched to every call.
 *
 * Thread safety is up to each implementation and documented with it. The default allocator may
 * be used from any thread.
 */
#ifndef AX_ALLOCATOR_H
#define AX_ALLOCATOR_H

#include <stddef.h>

#include "ax_config.h"

AX_EXTERN_C_BEGIN

typedef struct ax_allocator {
    void *(*alloc)(void *ctx, size_t size, size_t align);
    void *(*resize)(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align);
    void (*free)(void *ctx, void *ptr, size_t size);
    void *ctx;
} ax_allocator;

/*
 * The process-wide allocator over the C heap. It is the only place AXION calls malloc, realloc
 * and free. Hosts that want to account for memory pass their own allocator instead.
 * Never NULL; the pointed-to struct is immutable and lives for the whole process.
 */
AX_API const ax_allocator *ax_default_allocator(void);

AX_EXTERN_C_END

#endif /* AX_ALLOCATOR_H */
