/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: typed 64-bit handles.
 *
 * Every public AXION object is a handle, never a pointer: a 64-bit value holding a 32-bit slot
 * index (low bits) and a 32-bit generation (high bits). When an object is destroyed its slot's
 * generation changes, so a stale handle (one a garbage-collected language released late, or from
 * a finalizer) fails a check and returns AX_ERR_INVALID_HANDLE instead of reaching memory that
 * now belongs to another object.
 *
 * Each object type gets its own struct so the compiler rejects passing one kind for another:
 *
 *     AX_DECLARE_HANDLE(ax_node);    // typedef struct ax_node { uint64_t h; } ax_node;
 *
 * The value 0 is the null handle of every type. Valid generations start at 1, so no live object
 * ever has handle 0.
 */
#ifndef AX_HANDLE_H
#define AX_HANDLE_H

#include <stdbool.h>
#include <stdint.h>

#include "ax_platform.h"

#define AX_DECLARE_HANDLE(name) \
    typedef struct name {       \
        uint64_t h;             \
    } name

/* Initializer for a null handle of any type: ax_node n = AX_HANDLE_NULL_INIT; */
#define AX_HANDLE_NULL_INIT {0}

#define AX_HANDLE_IS_NULL(handle) ((handle).h == 0)
#define AX_HANDLE_EQ(a, b)        ((a).h == (b).h)

AX_EXTERN_C_BEGIN

static inline uint64_t ax_handle_make(uint32_t slot, uint32_t generation)
{
    return ((uint64_t)generation << 32) | (uint64_t)slot;
}

static inline uint32_t ax_handle_slot(uint64_t handle)
{
    return (uint32_t)(handle & 0xFFFFFFFFu);
}

static inline uint32_t ax_handle_generation(uint64_t handle)
{
    return (uint32_t)(handle >> 32);
}

AX_EXTERN_C_END

#endif /* AX_HANDLE_H */
