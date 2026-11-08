/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Glyph rasterization and the glyph cache (decision 100).
 *
 * Rasterizing. FreeType renders 8-bit coverage at the instance's exact size: light hinting
 * (vertical only, FreeType's autohinter in light mode, as KDE's "slight") up to
 * AX_TEXT_SUBPIXEL_MAX_SIZE pixels and none above, the outline moved right by bin/4 of a pixel
 * before rendering for the subpixel positions. FreeType's faces are not thread-safe, so each
 * rasterizer context (one per thread at a time, from a pool) has its own library and faces over the
 * same font bytes. FreeType computes in fixed point, so a glyph is the same bytes everywhere.
 *
 * The cache. Entries are created pending, inserted at once so no glyph is rasterized twice, and
 * become ready (release order) with their pixels. Each use for a snapshot stamps the snapshot's
 * epoch; eviction (ax_text_glyphs_retire) takes only entries last used before the oldest snapshot
 * that may still be drawn, least recently used first, so a renderer drawing any live snapshot
 * always finds its glyphs (the owner's rule). Inserting never evicts.
 *
 * Background work. Glyphs past the caller's synchronous budget are rasterized in background jobs;
 * each batch calls the ready callback when done, which a world turns into a frame request (a
 * resting window has no commit to wake it, decision 95).
 */
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

#include "text_internal.h"

/* ---- Entries --------------------------------------------------------------------------------- */

enum { GLYPH_PENDING = 0, GLYPH_READY = 1, GLYPH_FAILED = 2 };

typedef struct tx_glyph {
    tx_entry entry;
    ax_glyph_key key;
    ax_atomic_u32 state;
    ax_atomic_u64 last_epoch;
    ax_glyph_bitmap bitmap; /* written before state becomes READY */
    size_t pixel_bytes;
} tx_glyph;

void tx_glyph_destroy(tx_entry *e, void *user);
bool tx_glyph_equal(const tx_entry *e, const void *key);

bool tx_glyph_equal(const tx_entry *e, const void *key)
{
    const tx_glyph *g = (const tx_glyph *)(const void *)e;
    const ax_glyph_key *k = (const ax_glyph_key *)key;
    return g->key.instance == k->instance && g->key.glyph == k->glyph && g->key.bin == k->bin;
}

void tx_glyph_destroy(tx_entry *e, void *user)
{
    ax_text_system *ts = (ax_text_system *)user;
    tx_glyph *g = (tx_glyph *)(void *)e;
    if (g->pixel_bytes > 0) {
        ax_mem_free(&ts->allocator, (void *)g->bitmap.pixels, g->pixel_bytes);
        (void)ax_atomic_fetch_add_u64(&ts->c_glyph_bytes, (uint64_t)0 - g->pixel_bytes,
                                      AX_MO_RELAXED);
    }
    ax_mem_free(&ts->allocator, g, sizeof *g);
}

static uint64_t glyph_hash(const ax_glyph_key *k)
{
    uint64_t h = TX_HASH_SEED;
    h = tx_hash_bytes(h, &k->instance, sizeof k->instance);
    h = tx_hash_bytes(h, &k->glyph, sizeof k->glyph);
    return tx_hash_bytes(h, &k->bin, sizeof k->bin);
}

/* ---- Rasterizer contexts --------------------------------------------------------------------- */

typedef struct tx_rasterizer {
    struct tx_rasterizer *next_free;
    struct tx_rasterizer *next_all;
    FT_Library lib;
    FT_Face faces[256];
    ax_font_instance current[256]; /* the instance each face is set up for */
} tx_rasterizer;

static tx_rasterizer *raster_take(ax_text_system *ts)
{
    tx_rasterizer *r;
    ax_mutex_lock(&ts->raster_mutex);
    r = ts->raster_free;
    if (r != NULL) {
        ts->raster_free = r->next_free;
        ax_mutex_unlock(&ts->raster_mutex);
        return r;
    }
    ax_mutex_unlock(&ts->raster_mutex);
    r = (tx_rasterizer *)ax_mem_alloc(&ts->allocator, sizeof *r, AX_ALIGNOF(tx_rasterizer));
    if (r == NULL) {
        return NULL;
    }
    memset(r, 0, sizeof *r);
    if (FT_Init_FreeType(&r->lib) != 0) {
        ax_mem_free(&ts->allocator, r, sizeof *r);
        return NULL;
    }
    ax_mutex_lock(&ts->raster_mutex);
    r->next_all = ts->raster_all;
    ts->raster_all = r;
    ax_mutex_unlock(&ts->raster_mutex);
    return r;
}

static void raster_give(ax_text_system *ts, tx_rasterizer *r)
{
    ax_mutex_lock(&ts->raster_mutex);
    r->next_free = ts->raster_free;
    ts->raster_free = r;
    ax_mutex_unlock(&ts->raster_mutex);
}

/* The context's face for an instance, set to its size and variation coordinates. */
static FT_Face face_for(ax_text_system *ts, tx_rasterizer *r, ax_font_instance id)
{
    const tx_instance *in = tx_instance_get(ts, id);
    const tx_font *font;
    const tx_face *fc;
    const tx_blob *blob;
    FT_Face face;
    if (in == NULL || (font = tx_font_get(ts, in->font)) == NULL) {
        return NULL;
    }
    fc = tx_face_get(ts, font->face);
    face = r->faces[font->face];
    if (face == NULL) {
        blob = tx_blob_get(ts, fc->blob);
        if (FT_New_Memory_Face(r->lib, blob->data, (FT_Long)blob->size, (FT_Long)fc->index,
                               &face) != 0) {
            return NULL;
        }
        r->faces[font->face] = face;
        r->current[font->face] = 0;
    }
    if (r->current[font->face] != id) {
        if (fc->has_wght || fc->has_wdth) {
            FT_MM_Var *mm = NULL;
            if (FT_Get_MM_Var(face, &mm) == 0) {
                FT_Fixed coords[16];
                FT_UInt i, n = mm->num_axis < 16u ? mm->num_axis : 16u;
                for (i = 0; i < n; ++i) {
                    const FT_ULong tag = mm->axis[i].tag;
                    coords[i] = mm->axis[i].def;
                    if (tag == FT_MAKE_TAG('w', 'g', 'h', 't') && fc->has_wght) {
                        coords[i] = (FT_Fixed)(font->wght * 65536.0f + 0.5f);
                    } else if (tag == FT_MAKE_TAG('w', 'd', 't', 'h') && fc->has_wdth) {
                        coords[i] = (FT_Fixed)(font->wdth * 65536.0f + 0.5f);
                    }
                }
                (void)FT_Set_Var_Design_Coordinates(face, n, coords);
                FT_Done_MM_Var(r->lib, mm);
            }
        }
        if (FT_Set_Char_Size(face, 0, (FT_F26Dot6)in->size64, 72, 72) != 0) {
            return NULL;
        }
        r->current[font->face] = id;
    }
    return face;
}

/* Rasterizes one pending glyph and publishes it (READY or FAILED). */
static void rasterize(ax_text_system *ts, tx_rasterizer *r, tx_glyph *g)
{
    const tx_instance *in = tx_instance_get(ts, g->key.instance);
    FT_Face face = r != NULL ? face_for(ts, r, g->key.instance) : NULL;
    FT_Int32 flags = FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM;
    FT_GlyphSlot slot;
    uint32_t state = GLYPH_FAILED;
    if (face == NULL || in == NULL) {
        goto done;
    }
    flags |= in->hinted ? FT_LOAD_TARGET_LIGHT : FT_LOAD_NO_HINTING;
    if (FT_Load_Glyph(face, (FT_UInt)g->key.glyph, flags) != 0) {
        goto done;
    }
    slot = face->glyph;
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE && g->key.bin > 0) {
        FT_Outline_Translate(&slot->outline, (FT_Pos)(g->key.bin * (64u / AX_TEXT_SUBPIXEL_BINS)), 0);
    }
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
        goto done;
    }
    {
        const FT_Bitmap *bm = &slot->bitmap;
        const uint32_t w = bm->width, h = bm->rows;
        memset(&g->bitmap, 0, sizeof g->bitmap);
        g->bitmap.left = (int16_t)slot->bitmap_left;
        g->bitmap.top = (int16_t)slot->bitmap_top;
        if (w > 0 && h > 0 && w <= 4096u && h <= 4096u) {
            uint8_t *px = (uint8_t *)ax_mem_alloc(&ts->allocator, (size_t)w * h, 16);
            uint32_t y;
            if (px == NULL) {
                goto done;
            }
            for (y = 0; y < h; ++y) {
                const unsigned char *src = bm->pitch >= 0
                                               ? bm->buffer + (size_t)y * (size_t)bm->pitch
                                               : bm->buffer + (size_t)(h - 1u - y) * (size_t)(-bm->pitch);
                memcpy(px + (size_t)y * w, src, w);
            }
            g->bitmap.width = (uint16_t)w;
            g->bitmap.height = (uint16_t)h;
            g->bitmap.stride = w;
            g->bitmap.pixels = px;
            g->pixel_bytes = (size_t)w * h;
            tx_cache_add_bytes(&ts->glyph_cache, &g->entry, g->pixel_bytes);
            tx_count(&ts->c_glyph_bytes, g->pixel_bytes);
        }
        state = GLYPH_READY;
    }
done:
    tx_count(&ts->c_glyphs, 1);
    ax_atomic_store_u32(&g->state, state, AX_MO_RELEASE);
}

static void rasterize_list(ax_text_system *ts, tx_glyph *const *list, uint32_t n)
{
    tx_rasterizer *r = raster_take(ts);
    uint32_t i;
    for (i = 0; i < n; ++i) {
        rasterize(ts, r, list[i]);
    }
    if (r != NULL) {
        raster_give(ts, r);
    }
}

void tx_glyphs_init(ax_text_system *ts)
{
    ax_mutex_init(&ts->raster_mutex);
    ax_atomic_init_u32(&ts->background_pending, 0);
}

void tx_glyphs_deinit(ax_text_system *ts)
{
    tx_rasterizer *r;
    /* Background batches hold references to entries and use the contexts: let them finish. */
    while (ax_atomic_load_u32(&ts->background_pending, AX_MO_ACQUIRE) != 0) {
        if (ts->last_jobs != NULL) {
            ax_jobs_wait_idle(ts->last_jobs);
        } else {
            ax_thread_yield();
        }
    }
    r = ts->raster_all;
    while (r != NULL) {
        tx_rasterizer *next = r->next_all;
        uint32_t i;
        for (i = 0; i < 256u; ++i) {
            if (r->faces[i] != NULL) {
                FT_Done_Face(r->faces[i]);
            }
        }
        FT_Done_FreeType(r->lib);
        ax_mem_free(&ts->allocator, r, sizeof *r);
        r = next;
    }
    ts->raster_all = ts->raster_free = NULL;
    ax_mutex_deinit(&ts->raster_mutex);
}

/* ---- Lookup ---------------------------------------------------------------------------------- */

bool ax_text_glyph_lookup(ax_text_system *ts, const ax_glyph_key *key, ax_glyph_bitmap *out)
{
    tx_entry *e = tx_cache_get(&ts->glyph_cache, glyph_hash(key), key);
    bool ok = false;
    if (e != NULL) {
        tx_glyph *g = (tx_glyph *)(void *)e;
        if (ax_atomic_load_u32(&g->state, AX_MO_ACQUIRE) == GLYPH_READY) {
            *out = g->bitmap;
            ok = true;
        }
        tx_cache_release(&ts->glyph_cache, e);
    }
    return ok;
}

/* ---- Making glyphs available ------------------------------------------------------------------ */

typedef struct raster_batch {
    ax_text_system *ts;
    ax_job job;
    uint32_t count;
    tx_glyph *glyphs[]; /* references held until the batch is done */
} raster_batch;

static void background_job(void *arg)
{
    raster_batch *b = (raster_batch *)arg;
    ax_text_system *ts = b->ts;
    uint32_t i;
    void (*fn)(void *);
    void *user;
    rasterize_list(ts, b->glyphs, b->count);
    tx_count(&ts->c_glyphs_bg, b->count);
    for (i = 0; i < b->count; ++i) {
        tx_cache_release(&ts->glyph_cache, &b->glyphs[i]->entry);
    }
    ax_mem_free(&ts->allocator, b, sizeof *b + sizeof(tx_glyph *) * b->count);
    ax_mutex_lock(&ts->raster_mutex);
    fn = ts->ready_fn;
    user = ts->ready_user;
    ax_mutex_unlock(&ts->raster_mutex);
    if (fn != NULL) {
        fn(user);
    }
    (void)ax_atomic_fetch_add_u32(&ts->background_pending, UINT32_MAX, AX_MO_ACQ_REL);
}

typedef struct frame_chunk {
    ax_text_system *ts;
    tx_glyph *const *glyphs;
    uint32_t count;
} frame_chunk;

static void frame_job(void *arg)
{
    frame_chunk *c = (frame_chunk *)arg;
    rasterize_list(c->ts, c->glyphs, c->count);
}

#define SYNC_CHUNK 32u
#define BG_CHUNK   64u

/* A background job counter per system: jobs outlive no counter (the system waits for them). */
static ax_job_counter *bg_counter(ax_text_system *ts)
{
    return &ts->bg_counter;
}

uint32_t ax_text_glyphs_ensure(ax_text_system *ts, const ax_glyph_key *keys, uint32_t count,
                               uint64_t epoch, ax_job_system *jobs, uint32_t *sync_budget)
{
    tx_glyph *stack_list[128];
    tx_glyph **todo = stack_list;
    uint32_t todo_count = 0, todo_cap = 128;
    uint32_t missing = 0;
    uint32_t i;
    for (i = 0; i < count; ++i) {
        const uint64_t hash = glyph_hash(&keys[i]);
        tx_entry *e = tx_cache_get(&ts->glyph_cache, hash, &keys[i]);
        tx_glyph *g;
        uint64_t last;
        if (e == NULL) {
            tx_glyph *fresh = (tx_glyph *)ax_mem_alloc(&ts->allocator, sizeof *fresh,
                                                        AX_ALIGNOF(tx_glyph));
            if (fresh == NULL) {
                missing++;
                continue;
            }
            memset(fresh, 0, sizeof *fresh);
            fresh->entry.hash = hash;
            fresh->entry.bytes = sizeof *fresh;
            fresh->key = keys[i];
            ax_atomic_init_u32(&fresh->state, GLYPH_PENDING);
            ax_atomic_init_u64(&fresh->last_epoch, epoch);
            e = tx_cache_put(&ts->glyph_cache, &fresh->entry, &keys[i]);
            if (e != &fresh->entry) {
                tx_glyph_destroy(&fresh->entry, ts); /* another thread inserted it first */
            } else {
                /* Ours to rasterize: keep the reference for the work list. */
                if (todo_count == todo_cap) {
                    uint32_t cap = todo_cap * 2u;
                    tx_glyph **bigger = (tx_glyph **)ax_mem_alloc(
                        &ts->allocator, sizeof(tx_glyph *) * cap, AX_ALIGNOF(tx_glyph *));
                    if (bigger == NULL) {
                        /* Out of memory: rasterize it now. */
                        rasterize_list(ts, (tx_glyph *const *)&fresh, 1);
                        tx_cache_release(&ts->glyph_cache, e);
                        continue;
                    }
                    memcpy(bigger, todo, sizeof(tx_glyph *) * todo_count);
                    if (todo != stack_list) {
                        ax_mem_free(&ts->allocator, todo, sizeof(tx_glyph *) * todo_cap);
                    }
                    todo = bigger;
                    todo_cap = cap;
                }
                todo[todo_count++] = fresh;
                continue;
            }
        }
        g = (tx_glyph *)(void *)e;
        last = ax_atomic_load_u64(&g->last_epoch, AX_MO_RELAXED);
        while (last < epoch &&
               !ax_atomic_compare_exchange_u64(&g->last_epoch, &last, epoch, AX_MO_RELAXED,
                                               AX_MO_RELAXED)) {
        }
        if (ax_atomic_load_u32(&g->state, AX_MO_ACQUIRE) == GLYPH_PENDING) {
            missing++; /* someone else is rasterizing it */
        }
        tx_cache_release(&ts->glyph_cache, e);
    }
    if (todo_count > 0) {
        uint32_t sync = todo_count;
        if (sync_budget != NULL && jobs != NULL) {
            sync = *sync_budget < todo_count ? *sync_budget : todo_count;
            *sync_budget -= sync;
        }
        /* Synchronous part: frame jobs for large batches, else this thread. */
        if (sync > 0) {
            if (jobs != NULL && sync > SYNC_CHUNK && !ax_jobs_is_inline(jobs)) {
                frame_chunk chunks[64];
                ax_job fjobs[64];
                ax_job_counter done = AX_JOB_COUNTER_INIT;
                uint32_t at = 0;
                while (at < sync) {
                    uint32_t n = 0;
                    while (n < 64u && at < sync) {
                        uint32_t c = sync - at < SYNC_CHUNK ? sync - at : SYNC_CHUNK;
                        chunks[n].ts = ts;
                        chunks[n].glyphs = todo + at;
                        chunks[n].count = c;
                        fjobs[n].fn = frame_job;
                        fjobs[n].arg = &chunks[n];
                        at += c;
                        n++;
                    }
                    ax_jobs_submit(jobs, fjobs, n, &done);
                    ax_jobs_wait(jobs, &done);
                }
            } else {
                rasterize_list(ts, todo, sync);
            }
            for (i = 0; i < sync; ++i) {
                if (ax_atomic_load_u32(&todo[i]->state, AX_MO_ACQUIRE) == GLYPH_PENDING) {
                    missing++;
                }
                tx_cache_release(&ts->glyph_cache, &todo[i]->entry);
            }
        }
        /* The rest in background batches, which keep their references. */
        i = sync;
        while (i < todo_count) {
            const uint32_t n = todo_count - i < BG_CHUNK ? todo_count - i : BG_CHUNK;
            raster_batch *b = (raster_batch *)ax_mem_alloc(
                &ts->allocator, sizeof *b + sizeof(tx_glyph *) * n, AX_ALIGNOF(raster_batch));
            missing += n;
            if (b == NULL) {
                uint32_t k;
                rasterize_list(ts, todo + i, n);
                for (k = 0; k < n; ++k) {
                    tx_cache_release(&ts->glyph_cache, &todo[i + k]->entry);
                }
                missing -= n;
                i += n;
                continue;
            }
            b->ts = ts;
            b->count = n;
            memcpy(b->glyphs, todo + i, sizeof(tx_glyph *) * n);
            b->job.fn = background_job;
            b->job.arg = b;
            ts->last_jobs = jobs;
            (void)ax_atomic_fetch_add_u32(&ts->background_pending, 1u, AX_MO_ACQ_REL);
            ax_jobs_submit_background(jobs, &b->job, 1, bg_counter(ts));
            i += n;
        }
    }
    if (todo != stack_list) {
        ax_mem_free(&ts->allocator, todo, sizeof(tx_glyph *) * todo_cap);
    }
    return missing;
}

void ax_text_set_glyphs_ready_callback(ax_text_system *ts, void (*fn)(void *user), void *user)
{
    ax_mutex_lock(&ts->raster_mutex);
    ts->ready_fn = fn;
    ts->ready_user = user;
    ax_mutex_unlock(&ts->raster_mutex);
}

/* ---- Eviction -------------------------------------------------------------------------------- */

typedef struct retire_arg {
    uint64_t oldest_live;
} retire_arg;

static bool evictable(const tx_entry *e, void *arg)
{
    const tx_glyph *g = (const tx_glyph *)(const void *)e;
    const retire_arg *r = (const retire_arg *)arg;
    return ax_atomic_load_u32(&g->state, AX_MO_ACQUIRE) != GLYPH_PENDING &&
           ax_atomic_load_u64(&g->last_epoch, AX_MO_RELAXED) < r->oldest_live;
}

uint32_t ax_text_glyphs_retire(ax_text_system *ts, uint64_t oldest_live_epoch)
{
    retire_arg arg;
    uint64_t refused = 0;
    uint32_t n;
    arg.oldest_live = oldest_live_epoch;
    n = tx_cache_evict(&ts->glyph_cache, ts->glyph_budget, evictable, &arg, &refused);
    tx_count(&ts->c_evicted, n);
    tx_count(&ts->c_pinned, refused);
    return n;
}
