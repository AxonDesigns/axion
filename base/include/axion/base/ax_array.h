/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: typed dynamic array over an ax_allocator.
 *
 * AX_ARRAY_DEFINE(name, T) defines the struct `name` and type-checked static inline functions
 * named name_init, name_deinit, name_reserve, name_resize, name_push, name_pop, name_at,
 * name_clear and name_remove_swap. Define each array type once, in a header or .c file:
 *
 *     AX_ARRAY_DEFINE(ax_u32_array, uint32_t);
 *
 *     ax_u32_array xs;
 *     ax_u32_array_init(&xs, allocator);
 *     if (ax_u32_array_push(&xs, 7) != AX_OK) { ... }     // AX_ERR_OUT_OF_MEMORY
 *     for (size_t i = 0; i < xs.len; ++i) use(xs.data[i]);
 *     ax_u32_array_deinit(&xs);
 *
 * The array keeps a pointer to its allocator, which must outlive it. Capacity grows
 * geometrically; growth failure leaves the array unchanged. Element addresses change when the
 * array grows: store indices, not pointers. Elements are moved with memcpy semantics (the
 * allocator's resize), so T must be trivially relocatable, as every plain C type is.
 */
#ifndef AX_BASE_ARRAY_H
#define AX_BASE_ARRAY_H

#include <stddef.h>
#include <string.h>

#include "../ax_allocator.h"
#include "../ax_types.h"
#include "ax_assert.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

/* Grows *data to hold at least min_capacity elements; used by the generated functions. */
AX_API ax_result ax_array_grow_raw(void **data, size_t *capacity, size_t min_capacity,
                                   size_t elem_size, size_t elem_align,
                                   const ax_allocator *allocator);

/* Frees an array buffer of capacity elements (data may be NULL). */
AX_API void ax_array_free_raw(void *data, size_t capacity, size_t elem_size,
                              const ax_allocator *allocator);

AX_EXTERN_C_END

#define AX_ARRAY_DEFINE(name, T)                                                                  \
    typedef struct name {                                                                         \
        T *data;                                                                                  \
        size_t len;                                                                               \
        size_t cap;                                                                               \
        const ax_allocator *allocator;                                                            \
    } name;                                                                                       \
                                                                                                  \
    AX_MAYBE_UNUSED static inline void name##_init(name *a, const ax_allocator *allocator)        \
    {                                                                                             \
        AX_ASSERT(a != NULL && allocator != NULL);                                                \
        a->data = NULL;                                                                           \
        a->len = 0;                                                                               \
        a->cap = 0;                                                                               \
        a->allocator = allocator;                                                                 \
    }                                                                                             \
                                                                                                  \
    AX_MAYBE_UNUSED static inline void name##_deinit(name *a)                                     \
    {                                                                                             \
        ax_array_free_raw(a->data, a->cap, sizeof(T), a->allocator);                              \
        a->data = NULL;                                                                           \
        a->len = 0;                                                                               \
        a->cap = 0;                                                                               \
    }                                                                                             \
                                                                                                  \
    /* Ensures room for capacity elements without further allocation. */                          \
    AX_MAYBE_UNUSED static inline ax_result name##_reserve(name *a, size_t capacity)              \
    {                                                                                             \
        void *data = a->data;                                                                     \
        ax_result r;                                                                              \
        if (capacity <= a->cap) {                                                                 \
            return AX_OK;                                                                         \
        }                                                                                         \
        r = ax_array_grow_raw(&data, &a->cap, capacity, sizeof(T), AX_ALIGNOF(T), a->allocator);  \
        a->data = (T *)data;                                                                      \
        return r;                                                                                 \
    }                                                                                             \
                                                                                                  \
    /* Sets the length; new elements are zeroed. */                                               \
    AX_MAYBE_UNUSED static inline ax_result name##_resize(name *a, size_t len)                    \
    {                                                                                             \
        if (len > a->len) {                                                                       \
            ax_result r = name##_reserve(a, len);                                                 \
            if (r != AX_OK) {                                                                     \
                return r;                                                                         \
            }                                                                                     \
            memset(a->data + a->len, 0, (len - a->len) * sizeof(T));                              \
        }                                                                                         \
        a->len = len;                                                                             \
        return AX_OK;                                                                             \
    }                                                                                             \
                                                                                                  \
    AX_MAYBE_UNUSED static inline ax_result name##_push(name *a, T value)                         \
    {                                                                                             \
        if (a->len == a->cap) {                                                                   \
            void *data = a->data;                                                                 \
            ax_result r = ax_array_grow_raw(&data, &a->cap, a->len + 1, sizeof(T), AX_ALIGNOF(T), \
                                            a->allocator);                                        \
            if (r != AX_OK) {                                                                     \
                return r;                                                                         \
            }                                                                                     \
            a->data = (T *)data;                                                                  \
        }                                                                                         \
        a->data[a->len++] = value;                                                                \
        return AX_OK;                                                                             \
    }                                                                                             \
                                                                                                  \
    /* Removes and returns the last element; the array must not be empty. */                      \
    AX_MAYBE_UNUSED static inline T name##_pop(name *a)                                           \
    {                                                                                             \
        AX_ASSERT_MSG(a->len > 0, "pop from an empty array");                                     \
        return a->data[--a->len];                                                                 \
    }                                                                                             \
                                                                                                  \
    /* Bounds-checked (in debug builds) element access. */                                        \
    AX_MAYBE_UNUSED static inline T *name##_at(const name *a, size_t i)                           \
    {                                                                                             \
        AX_ASSERT_MSG(i < a->len, "array index out of bounds");                                   \
        return &a->data[i];                                                                       \
    }                                                                                             \
                                                                                                  \
    /* Sets the length to 0, keeping the capacity. */                                             \
    AX_MAYBE_UNUSED static inline void name##_clear(name *a)                                      \
    {                                                                                             \
        a->len = 0;                                                                               \
    }                                                                                             \
                                                                                                  \
    /* Removes element i in O(1) by moving the last element into its place. */                    \
    AX_MAYBE_UNUSED static inline void name##_remove_swap(name *a, size_t i)                      \
    {                                                                                             \
        AX_ASSERT_MSG(i < a->len, "array index out of bounds");                                   \
        a->data[i] = a->data[a->len - 1];                                                         \
        a->len--;                                                                                 \
    }                                                                                             \
    typedef int name##_ax_array_defined /* swallows the trailing semicolon */

#endif /* AX_BASE_ARRAY_H */
