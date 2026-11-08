/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Glyph rasterization and the glyph cache (decision 100).
 */
#include <string.h>

#include "text_internal.h"

void tx_glyph_destroy(tx_entry *e, void *user);
bool tx_glyph_equal(const tx_entry *e, const void *key);

void tx_glyphs_init(ax_text_system *ts)
{
    ax_mutex_init(&ts->raster_mutex);
}

void tx_glyphs_deinit(ax_text_system *ts)
{
    ax_mutex_deinit(&ts->raster_mutex);
}

bool tx_glyph_equal(const tx_entry *e, const void *key)
{
    (void)e;
    (void)key;
    return false;
}

void tx_glyph_destroy(tx_entry *e, void *user)
{
    ax_text_system *ts = (ax_text_system *)user;
    ax_mem_free(&ts->allocator, e, e->bytes);
}
