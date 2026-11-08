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
/* Charges `bytes` more to an entry the cache holds (a glyph's pixels, known once rasterized). */
void tx_cache_add_bytes(tx_cache *c, tx_entry *e, size_t bytes);

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
    ax_job_counter bg_counter; /* every background batch's: the system outlives its jobs */
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

/* ---- Shaping (paragraph.c) ------------------------------------------------------------------- */

/* A shaped run in the shaping cache: HarfBuzz's output in font units, immutable. */
typedef struct tx_shaped {
    tx_entry entry;
    /* The key. */
    ax_font_id font;
    uint32_t script, direction;
    uint32_t feature_count;
    ax_text_feature features[AX_TEXT_MAX_FEATURES];
    uint32_t text_len;
    char *text;
    /* The glyphs, in visual order (left to right). */
    uint32_t count;
    uint32_t *ids;
    uint32_t *clusters; /* byte offsets in `text` */
    int32_t *advance, *x_offset, *y_offset;
    uint8_t *unsafe; /* HarfBuzz's UNSAFE_TO_BREAK: breaking before this glyph needs reshaping */
} tx_shaped;

typedef struct tx_shape_key {
    ax_font_id font;
    uint32_t script, direction;
    uint32_t feature_count;
    const ax_text_feature *features;
    const char *text;
    uint32_t text_len;
} tx_shape_key;

/* Shapes `key` through the cache; the caller holds the reference it returns (tx_cache_release on
 * ts->shape_cache). NULL when out of memory. `fragment` counts it as a line-end fragment. */
tx_shaped *tx_shape(ax_text_system *ts, const tx_shape_key *key, bool fragment);

/* ---- Paragraphs (paragraph.c, layout.c) ------------------------------------------------------ */

/* A run: a range of the text with one style and one font. Itemization's later stages (bidi level,
 * script, per-cluster font fallback) refine runs further; the fields are already here. */
typedef struct tx_run {
    uint32_t start, end; /* bytes */
    uint32_t style;
    ax_font_id font;
    uint32_t script;    /* hb_script_t */
    uint32_t direction; /* hb_direction_t: left to right until bidi (M3) */
    uint8_t level;      /* bidi embedding level: 0 until bidi */
    tx_shaped *shaped;
} tx_run;

/* A glyph of the shaped paragraph, flattened over its runs. */
typedef struct tx_pglyph {
    uint32_t id;
    uint32_t cluster; /* absolute byte offset */
    uint32_t run;
    float advance, x_offset, y_offset; /* logical units */
    bool unsafe;
} tx_pglyph;

#define TX_LAYOUT_CACHE 4u

typedef struct tx_layout {
    ax_text_layout pub;
    ax_atomic_u32 refs;
    ax_text_system *ts;
    size_t bytes;
} tx_layout;

struct ax_text_paragraph {
    ax_atomic_u32 refs;
    ax_text_system *ts;
    char *text;
    uint32_t len;
    ax_text_style *styles;
    uint32_t style_count;
    uint32_t align;
    tx_run *runs;
    uint32_t run_count;
    char *breaks;    /* libunibreak line breaks, per byte (after byte i) */
    char *graphemes; /* grapheme breaks, per byte */
    ax_mutex mutex;  /* shaping once; the layout cache */
    ax_atomic_u32 state; /* 0: not shaped, 1: shaped, 2: failed */
    tx_pglyph *glyphs;
    uint32_t glyph_count;
    tx_layout *cache[TX_LAYOUT_CACHE];
    uint32_t cache_next;
};

/* glyphs.c */
void tx_glyphs_init(ax_text_system *ts);
void tx_glyphs_deinit(ax_text_system *ts);

#endif /* AX_TEXT_INTERNAL_H */
