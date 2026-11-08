/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Paragraphs: itemization, breaks and shaping (decision 99).
 */
#include <string.h>

#include "text_internal.h"

/* A shaped run in the cache. */
typedef struct tx_shaped {
    tx_entry entry;
} tx_shaped;

bool tx_shape_equal(const tx_entry *e, const void *key);
void tx_shape_destroy(tx_entry *e, void *user);

bool tx_shape_equal(const tx_entry *e, const void *key)
{
    (void)e;
    (void)key;
    return false;
}

void tx_shape_destroy(tx_entry *e, void *user)
{
    ax_text_system *ts = (ax_text_system *)user;
    ax_mem_free(&ts->allocator, e, e->bytes);
}
