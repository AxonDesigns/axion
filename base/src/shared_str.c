/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include <axion/base/ax_memory.h>
#include <axion/base/ax_shared_str.h>

ax_shared_str *ax_shared_str_create(const ax_allocator *allocator, const char *bytes, size_t len)
{
    ax_shared_str *s;
    if (len >= UINT32_MAX) {
        return NULL;
    }
    s = (ax_shared_str *)ax_mem_alloc(allocator, sizeof(ax_shared_str) + len + 1u,
                                      AX_ALIGNOF(ax_shared_str));
    if (s == NULL) {
        return NULL;
    }
    ax_atomic_init_u32(&s->refs, 1u);
    s->len = (uint32_t)len;
    s->allocator = *allocator;
    if (len > 0) {
        memcpy(s->data, bytes, len);
    }
    s->data[len] = '\0';
    return s;
}

void ax_shared_str_retain(const ax_shared_str *s)
{
    if (s != NULL) {
        (void)ax_atomic_fetch_add_u32(&((ax_shared_str *)s)->refs, 1u, AX_MO_RELAXED);
    }
}

void ax_shared_str_release(const ax_shared_str *s)
{
    ax_shared_str *m = (ax_shared_str *)s;
    if (m != NULL && ax_atomic_fetch_add_u32(&m->refs, UINT32_MAX, AX_MO_ACQ_REL) == 1u) {
        ax_allocator a = m->allocator;
        ax_mem_free(&a, m, sizeof(ax_shared_str) + m->len + 1u);
    }
}
