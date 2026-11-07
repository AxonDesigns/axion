/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "axion/base/ax_array.h"
#include "axion/base/ax_memory.h"

#define AX_ARRAY_MIN_CAPACITY 8u

ax_result ax_array_grow_raw(void **data, size_t *capacity, size_t min_capacity, size_t elem_size,
                            size_t elem_align, const ax_allocator *allocator)
{
    size_t new_cap;
    size_t new_bytes;
    void *p;

    AX_ASSERT(data != NULL && capacity != NULL && allocator != NULL);
    AX_ASSERT(elem_size > 0);
    if (min_capacity <= *capacity) {
        return AX_OK;
    }

    /* Double, but at least min_capacity and at least AX_ARRAY_MIN_CAPACITY. */
    new_cap = *capacity <= SIZE_MAX / 2 ? *capacity * 2 : SIZE_MAX;
    if (new_cap < min_capacity) {
        new_cap = min_capacity;
    }
    if (new_cap < AX_ARRAY_MIN_CAPACITY) {
        new_cap = AX_ARRAY_MIN_CAPACITY;
    }
    if (!ax_size_mul(new_cap, elem_size, &new_bytes)) {
        /* Doubling overflowed the byte count: fall back to exactly what was asked for. */
        new_cap = min_capacity;
        if (!ax_size_mul(new_cap, elem_size, &new_bytes)) {
            return AX_ERR_OUT_OF_MEMORY;
        }
    }

    if (*data == NULL) {
        p = ax_mem_alloc(allocator, new_bytes, elem_align);
    } else {
        p = ax_mem_resize(allocator, *data, *capacity * elem_size, new_bytes, elem_align);
    }
    if (p == NULL) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    *data = p;
    *capacity = new_cap;
    return AX_OK;
}

void ax_array_free_raw(void *data, size_t capacity, size_t elem_size, const ax_allocator *allocator)
{
    if (data != NULL) {
        ax_mem_free(allocator, data, capacity * elem_size);
    }
}
