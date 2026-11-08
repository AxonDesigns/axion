/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * text internals: the system's font tables, the shared caches, paragraphs and the rasterizer.
 */
#ifndef AX_TEXT_INTERNAL_H
#define AX_TEXT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hb-ot.h>
#include <hb.h>

#include <axion/base/ax_atomic.h>
#include <axion/base/ax_memory.h>
#include <axion/base/ax_thread.h>
#include <axion/text/ax_text.h>

/* ---- A sharded LRU cache (cache.c) ----------------------------------------------------------- */

#define TX_CACHE_SHARDS 16u

typedef struct tx_entry tx_entry;

/* Every cached object starts with this. */
struct tx_entry {
    uint64_t hash;
    tx_entry *next;             /* in its bucket */
    tx_entry *lru_prev, *lru_next; /* most recent at the head */
    size_t bytes;               /* charged to the cache */
    ax_atomic_u32 refs;         /* the cache's reference counts as one */
    bool in_cache;
};

typedef struct tx_shard {
    ax_mutex mutex;
    tx_entry **buckets;
    uint32_t bucket_count; /* power of two */
    uint32_t count;
    size_t bytes;
    tx_entry *lru_head, *lru_tail;
} tx_shard;

typedef struct tx_cache {
    const ax_allocator *allocator;
    size_t budget; /* per shard */
    bool (*equal)(const tx_entry *e, const void *key);
    void (*destroy)(tx_entry *e, void *user); /* the last reference went */
    void *user;
    tx_shard shards[TX_CACHE_SHARDS];
} tx_cache;

void tx_cache_init(tx_cache *c, const ax_allocator *allocator, size_t budget,
                   bool (*equal)(const tx_entry *, const void *), void (*destroy)(tx_entry *, void *),
                   void *user);
void tx_cache_deinit(tx_cache *c); /* releases the cache's references */

/* Finds an entry and takes a reference (moving it to the front), or NULL. */
tx_entry *tx_cache_get(tx_cache *c, uint64_t hash, const void *key);
/* Inserts a new entry (one reference stays with the caller) unless an equal one is already there:
 * then returns that one with a reference taken, and the caller destroys its own. Evicts from the
 * back while the shard is above its budget (entries the cache alone holds). */
tx_entry *tx_cache_put(tx_cache *c, tx_entry *e, const void *key);
void tx_cache_release(tx_cache *c, tx_entry *e);

/* Evicts entries the cache alone holds, from each shard's least recently used end, while the
 * cache's total is above `target` and `evictable` agrees; *refused counts entries it skipped
 * because `evictable` said no. Returns the number evicted. */
uint32_t tx_cache_evict(tx_cache *c, size_t target, bool (*evictable)(const tx_entry *, void *),
                        void *arg, uint64_t *refused);
size_t tx_cache_bytes(tx_cache *c);

uint64_t tx_hash_bytes(uint64_t h, const void *data, size_t size);
#define TX_HASH_SEED 0xcbf29ce484222325ull

/* ---- Fonts (system.c) ------------------------------------------------------------------------ */

typedef struct tx_blob {
    const uint8_t *data;
    size_t size;
    bool owned; /* read from a file into memory */
    char path[512];
} tx_blob;

typedef struct tx_face {
    uint32_t blob;
    uint32_t index;
    hb_face_t *hb;
    float upem;
    char family[64];
    bool has_wght, has_wdth;
    float wght_min, wght_max, wdth_min, wdth_max;
} tx_face;

typedef struct tx_font {
    uint32_t face;
    float wght, wdth; /* applied coordinates (NaN when the face has no such axis) */
    hb_font_t *hb;    /* at upem scale, with the variations; immutable */
    ax_font_metrics metrics;
} tx_font;

typedef struct tx_instance {
    ax_font_id font;
    uint32_t size64; /* pixels per em, 26.6 */
    bool hinted;     /* light hinting (sizes up to AX_TEXT_SUBPIXEL_MAX_SIZE) */
} tx_instance;

typedef struct tx_match {
    char family[64];
    uint32_t family_len;
    float weight, width;
    uint32_t style;
    ax_font_id font;
} tx_match;

#define TX_MAX_FONTS     1024u
#define TX_MAX_INSTANCES 4096u
#define TX_MAX_MATCHES   256u

struct tx_rasterizer;

struct ax_text_system {
    ax_allocator allocator;
    ax_font_source *sources[9];
    uint32_t source_count;

    /* Fonts: append-only tables under `fonts_mutex`; entries never move once published (fixed
     * capacity), so readers index them without the lock once they hold an id. */
    ax_mutex fonts_mutex;
    tx_blob *blobs;
    uint32_t blob_count;
    tx_face *faces;
    uint32_t face_count;
    tx_font *fonts; /* index id - 1 */
    ax_atomic_u32 font_count;
    tx_instance *instances; /* index id - 1 */
    ax_atomic_u32 instance_count;
    tx_match *matches;
    uint32_t match_count;

    /* Caches. */
    tx_cache shape_cache;
    tx_cache glyph_cache;
    size_t glyph_budget;

    /* Rasterizer contexts (glyphs.c), each with its own FreeType library: a pool. */
    ax_mutex raster_mutex;
    struct tx_rasterizer *raster_free;
    struct tx_rasterizer *raster_all;
    ax_atomic_u32 background_pending;
    void (*ready_fn)(void *user);
    void *ready_user;
    ax_job_system *last_jobs;

    /* Counters (atomic, relaxed). */
    ax_atomic_u64 c_paragraphs, c_runs_shaped, c_runs_cached, c_fragments, c_layouts,
        c_layouts_cached, c_glyphs, c_glyphs_bg, c_evicted, c_glyph_bytes, c_pinned;
};

const tx_font *tx_font_get(const ax_text_system *ts, ax_font_id id);
const tx_face *tx_face_get(const ax_text_system *ts, uint32_t face);
const tx_blob *tx_blob_get(const ax_text_system *ts, uint32_t blob);
const tx_instance *tx_instance_get(const ax_text_system *ts, ax_font_instance id);

static inline void tx_count(ax_atomic_u64 *c, uint64_t n)
{
    (void)ax_atomic_fetch_add_u64(c, n, AX_MO_RELAXED);
}

/* glyphs.c */
void tx_glyphs_init(ax_text_system *ts);
void tx_glyphs_deinit(ax_text_system *ts);

#endif /* AX_TEXT_INTERNAL_H */
