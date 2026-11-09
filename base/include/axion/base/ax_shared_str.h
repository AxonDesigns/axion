/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: an immutable, reference-counted string (decision 99). String property values
 * (text content, font families) are shared by the scene, the transactions' undo, the mirror's
 * copies and text's paragraphs without copying; the last release frees it with the allocator it
 * was made with (which must be thread-safe when references cross threads).
 */
#ifndef AX_BASE_SHARED_STR_H
#define AX_BASE_SHARED_STR_H

#include <stddef.h>
#include <stdint.h>

#include "../ax_allocator.h"
#include "../ax_types.h"
#include "ax_atomic.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

typedef struct ax_shared_str {
    ax_atomic_u32 refs;
    uint32_t len; /* bytes, without the terminating NUL */
    ax_allocator allocator;
    char data[]; /* len bytes and a NUL */
} ax_shared_str;

/* A copy of `len` bytes with one reference, or NULL when out of memory or len >= 2^32 - 1.
 * thread: any */
AX_API ax_shared_str *ax_shared_str_create(const ax_allocator *allocator, const char *bytes,
                                           size_t len);
/* thread: any (NULL is ignored) */
AX_API void ax_shared_str_retain(const ax_shared_str *s);
AX_API void ax_shared_str_release(const ax_shared_str *s);

static inline ax_str ax_shared_str_view(const ax_shared_str *s)
{
    ax_str v;
    v.ptr = s != NULL ? s->data : "";
    v.len = s != NULL ? s->len : 0u;
    return v;
}

AX_EXTERN_C_END

#endif /* AX_BASE_SHARED_STR_H */
