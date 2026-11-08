/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The text system: its sources, the font tables (files, faces, fonts with their variation
 * coordinates, raster instances), matching and metrics.
 *
 * The tables are append-only arrays of fixed capacity, written under `fonts_mutex`. An id is
 * published (its count stored with release order) only once its entry is complete, and entries
 * never move, so a thread holding an id reads its entry without the lock.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "text_internal.h"

#include <axion/base/ax_error_set.h>

#define MAX_BLOBS 256u
#define MAX_FACES 256u
#define INSTANCE_HASH 8192u /* open addressing over instance ids, under fonts_mutex */

/* Hash slots of the instance table, kept beside it. */
typedef struct instance_hash {
    uint32_t slots[INSTANCE_HASH]; /* instance id, 0: empty */
} instance_hash;

static instance_hash *ihash(const ax_text_system *ts)
{
    return (instance_hash *)(void *)(ts->instances + TX_MAX_INSTANCES);
}

/* ---- Lifetime -------------------------------------------------------------------------------- */

static void shape_destroy(tx_entry *e, void *user);
static bool shape_equal(const tx_entry *e, const void *key);
void tx_glyph_destroy(tx_entry *e, void *user);
bool tx_glyph_equal(const tx_entry *e, const void *key);

ax_result ax_text_system_create(const ax_text_system_desc *desc, ax_text_system **out)
{
    const ax_allocator *a = desc != NULL && desc->allocator != NULL ? desc->allocator
                                                                     : ax_default_allocator();
    ax_text_system *ts;
    uint32_t i;
    *out = NULL;
    ts = (ax_text_system *)ax_mem_alloc(a, sizeof *ts, AX_ALIGNOF(ax_text_system));
    if (ts == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_text_system_create: out of memory");
    }
    memset(ts, 0, sizeof *ts);
    ts->allocator = *a;
    if (desc != NULL) {
        for (i = 0; i < desc->source_count && ts->source_count < 8u; ++i) {
            if (desc->sources[i] != NULL && desc->sources[i] != ax_font_source_bundled()) {
                ts->sources[ts->source_count++] = desc->sources[i];
            }
        }
    }
    ts->sources[ts->source_count++] = ax_font_source_bundled(); /* always last */
    ax_mutex_init(&ts->fonts_mutex);
    ts->blobs = (tx_blob *)ax_mem_alloc(a, sizeof(tx_blob) * MAX_BLOBS, AX_ALIGNOF(tx_blob));
    ts->faces = (tx_face *)ax_mem_alloc(a, sizeof(tx_face) * MAX_FACES, AX_ALIGNOF(tx_face));
    ts->fonts = (tx_font *)ax_mem_alloc(a, sizeof(tx_font) * TX_MAX_FONTS, AX_ALIGNOF(tx_font));
    ts->instances = (tx_instance *)ax_mem_alloc(
        a, sizeof(tx_instance) * TX_MAX_INSTANCES + sizeof(instance_hash), 8u);
    ts->matches = (tx_match *)ax_mem_alloc(a, sizeof(tx_match) * TX_MAX_MATCHES,
                                           AX_ALIGNOF(tx_match));
    if (ts->blobs == NULL || ts->faces == NULL || ts->fonts == NULL || ts->instances == NULL ||
        ts->matches == NULL) {
        ax_text_system_destroy(ts);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_text_system_create: out of memory");
    }
    memset(ihash(ts), 0, sizeof(instance_hash));
    tx_cache_init(&ts->shape_cache, &ts->allocator,
                  desc != NULL && desc->shape_cache_bytes > 0 ? desc->shape_cache_bytes
                                                              : (size_t)8u << 20,
                  shape_equal, shape_destroy, ts);
    /* The glyph cache never evicts on insertion: only ax_text_glyphs_retire does, below the
     * oldest live epoch (decision 100). */
    tx_cache_init(&ts->glyph_cache, &ts->allocator, (size_t)SIZE_MAX, tx_glyph_equal,
                  tx_glyph_destroy, ts);
    ts->glyph_budget = desc != NULL && desc->glyph_cache_bytes > 0 ? desc->glyph_cache_bytes
                                                                   : (size_t)16u << 20;
    tx_glyphs_init(ts);
    *out = ts;
    return AX_OK;
}

void ax_text_system_destroy(ax_text_system *ts)
{
    const ax_allocator a = ts->allocator;
    uint32_t i, n;
    if (ts->instances != NULL) {
        tx_glyphs_deinit(ts); /* waits for background jobs */
        tx_cache_deinit(&ts->glyph_cache);
        tx_cache_deinit(&ts->shape_cache);
    }
    n = ts->fonts != NULL ? ax_atomic_load_u32(&ts->font_count, AX_MO_ACQUIRE) : 0;
    for (i = 0; i < n; ++i) {
        hb_font_destroy(ts->fonts[i].hb);
    }
    for (i = 0; ts->faces != NULL && i < ts->face_count; ++i) {
        hb_face_destroy(ts->faces[i].hb);
    }
    for (i = 0; ts->blobs != NULL && i < ts->blob_count; ++i) {
        if (ts->blobs[i].owned) {
            ax_mem_free(&a, (void *)ts->blobs[i].data, ts->blobs[i].size);
        }
    }
    ax_mem_free(&a, ts->blobs, sizeof(tx_blob) * MAX_BLOBS);
    ax_mem_free(&a, ts->faces, sizeof(tx_face) * MAX_FACES);
    ax_mem_free(&a, ts->fonts, sizeof(tx_font) * TX_MAX_FONTS);
    ax_mem_free(&a, ts->instances, sizeof(tx_instance) * TX_MAX_INSTANCES + sizeof(instance_hash));
    ax_mem_free(&a, ts->matches, sizeof(tx_match) * TX_MAX_MATCHES);
    ax_mutex_deinit(&ts->fonts_mutex);
    ax_mem_free(&a, ts, sizeof *ts);
}

/* ---- Tables ---------------------------------------------------------------------------------- */

const tx_font *tx_font_get(const ax_text_system *ts, ax_font_id id)
{
    if (id == 0 || id > ax_atomic_load_u32(&ts->font_count, AX_MO_ACQUIRE)) {
        return NULL;
    }
    return &ts->fonts[id - 1u];
}

const tx_face *tx_face_get(const ax_text_system *ts, uint32_t face)
{
    return &ts->faces[face];
}

const tx_blob *tx_blob_get(const ax_text_system *ts, uint32_t blob)
{
    return &ts->blobs[blob];
}

const tx_instance *tx_instance_get(const ax_text_system *ts, ax_font_instance id)
{
    if (id == 0 || id > ax_atomic_load_u32(&ts->instance_count, AX_MO_ACQUIRE)) {
        return NULL;
    }
    return &ts->instances[id - 1u];
}

/* The blob of a file a source named (loaded once). Under fonts_mutex. */
static ax_result blob_for(ax_text_system *ts, const ax_font_file *f, uint32_t *out)
{
    uint32_t i;
    tx_blob *b;
    for (i = 0; i < ts->blob_count; ++i) {
        b = &ts->blobs[i];
        if (f->data != NULL ? (!b->owned && b->data == f->data)
                            : (b->owned && strcmp(b->path, f->path) == 0)) {
            *out = i;
            return AX_OK;
        }
    }
    if (ts->blob_count == MAX_BLOBS) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "text: too many font files");
    }
    b = &ts->blobs[ts->blob_count];
    memset(b, 0, sizeof *b);
    if (f->data != NULL) {
        b->data = f->data;
        b->size = f->size;
    } else {
        FILE *fp = fopen(f->path, "rb");
        long len;
        uint8_t *data;
        if (fp == NULL) {
            return ax_set_error(AX_ERR_IO, "text: cannot open font file %s", f->path);
        }
        if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            return ax_set_error(AX_ERR_IO, "text: cannot read font file %s", f->path);
        }
        data = (uint8_t *)ax_mem_alloc(&ts->allocator, (size_t)len, 16u);
        if (data == NULL) {
            fclose(fp);
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "text: out of memory reading %s", f->path);
        }
        if (fread(data, 1, (size_t)len, fp) != (size_t)len) {
            fclose(fp);
            ax_mem_free(&ts->allocator, data, (size_t)len);
            return ax_set_error(AX_ERR_IO, "text: cannot read font file %s", f->path);
        }
        fclose(fp);
        b->data = data;
        b->size = (size_t)len;
        b->owned = true;
        (void)snprintf(b->path, sizeof b->path, "%s", f->path);
    }
    *out = ts->blob_count++;
    return AX_OK;
}

/* The face of a blob (created once). Under fonts_mutex. */
static ax_result face_for(ax_text_system *ts, uint32_t blob, uint32_t index, uint32_t *out)
{
    uint32_t i;
    tx_face *f;
    hb_blob_t *hb_blob;
    hb_ot_var_axis_info_t axis;
    unsigned int len;
    for (i = 0; i < ts->face_count; ++i) {
        if (ts->faces[i].blob == blob && ts->faces[i].index == index) {
            *out = i;
            return AX_OK;
        }
    }
    if (ts->face_count == MAX_FACES) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "text: too many font faces");
    }
    f = &ts->faces[ts->face_count];
    memset(f, 0, sizeof *f);
    hb_blob = hb_blob_create((const char *)ts->blobs[blob].data,
                             (unsigned int)ts->blobs[blob].size, HB_MEMORY_MODE_READONLY, NULL,
                             NULL);
    f->hb = hb_face_create(hb_blob, index);
    hb_blob_destroy(hb_blob);
    if (hb_face_get_glyph_count(f->hb) == 0) {
        hb_face_destroy(f->hb);
        return ax_set_error(AX_ERR_IO, "text: %s is not a usable font (face %u)",
                            ts->blobs[blob].owned ? ts->blobs[blob].path : "bundled font",
                            (unsigned)index);
    }
    hb_face_make_immutable(f->hb);
    f->blob = blob;
    f->index = index;
    f->upem = (float)hb_face_get_upem(f->hb);
    if (hb_ot_var_find_axis_info(f->hb, HB_TAG('w', 'g', 'h', 't'), &axis)) {
        f->has_wght = true;
        f->wght_min = axis.min_value;
        f->wght_max = axis.max_value;
    }
    if (hb_ot_var_find_axis_info(f->hb, HB_TAG('w', 'd', 't', 'h'), &axis)) {
        f->has_wdth = true;
        f->wdth_min = axis.min_value;
        f->wdth_max = axis.max_value;
    }
    len = (unsigned int)sizeof f->family;
    if (hb_ot_name_get_utf8(f->hb, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY, HB_LANGUAGE_INVALID, &len,
                            f->family) == 0) {
        len = (unsigned int)sizeof f->family;
        (void)hb_ot_name_get_utf8(f->hb, HB_OT_NAME_ID_FONT_FAMILY, HB_LANGUAGE_INVALID, &len,
                                  f->family);
    }
    *out = ts->face_count++;
    return AX_OK;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* The font of a face at the query's weight and width (created once). Under fonts_mutex. */
static ax_result font_for(ax_text_system *ts, uint32_t face, float weight, float width,
                          ax_font_id *out)
{
    const tx_face *fc = &ts->faces[face];
    const float wght = fc->has_wght ? clampf(weight, fc->wght_min, fc->wght_max) : NAN;
    const float wdth = fc->has_wdth ? clampf(width, fc->wdth_min, fc->wdth_max) : NAN;
    uint32_t n = ax_atomic_load_u32(&ts->font_count, AX_MO_RELAXED);
    uint32_t i;
    tx_font *f;
    hb_variation_t vars[2];
    unsigned int nvars = 0;
    hb_position_t pos;
    for (i = 0; i < n; ++i) {
        f = &ts->fonts[i];
        if (f->face == face && (f->wght == wght || (isnan(f->wght) && isnan(wght))) &&
            (f->wdth == wdth || (isnan(f->wdth) && isnan(wdth)))) {
            *out = i + 1u;
            return AX_OK;
        }
    }
    if (n == TX_MAX_FONTS) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "text: too many fonts");
    }
    f = &ts->fonts[n];
    memset(f, 0, sizeof *f);
    f->face = face;
    f->wght = wght;
    f->wdth = wdth;
    f->hb = hb_font_create(fc->hb);
    if (!isnan(wght)) {
        vars[nvars].tag = HB_TAG('w', 'g', 'h', 't');
        vars[nvars++].value = wght;
    }
    if (!isnan(wdth)) {
        vars[nvars].tag = HB_TAG('w', 'd', 't', 'h');
        vars[nvars++].value = wdth;
    }
    if (nvars > 0) {
        hb_font_set_variations(f->hb, vars, nvars);
    }
    hb_font_make_immutable(f->hb);
    f->metrics.units_per_em = fc->upem;
    (void)hb_ot_metrics_get_position_with_fallback(f->hb, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER,
                                                   &pos);
    f->metrics.ascent = (float)pos / fc->upem;
    (void)hb_ot_metrics_get_position_with_fallback(f->hb, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER,
                                                   &pos);
    f->metrics.descent = (float)-pos / fc->upem;
    (void)hb_ot_metrics_get_position_with_fallback(f->hb, HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP,
                                                   &pos);
    f->metrics.line_gap = pos > 0 ? (float)pos / fc->upem : 0.0f;
    ax_atomic_store_u32(&ts->font_count, n + 1u, AX_MO_RELEASE);
    *out = n + 1u;
    return AX_OK;
}

/* ---- Matching -------------------------------------------------------------------------------- */

ax_result ax_text_match_font(ax_text_system *ts, const ax_font_query *query, ax_font_id *out)
{
    ax_font_query q = *query;
    ax_result last = AX_OK;
    uint32_t i;
    uint32_t fallback_blob = UINT32_MAX, fallback_index = 0;
    bool fallback_set = false;
    *out = 0;
    if (q.weight <= 0.0f || !(q.weight == q.weight)) {
        q.weight = 400.0f;
    }
    if (q.width <= 0.0f || !(q.width == q.width)) {
        q.width = 100.0f;
    }
    q.weight = clampf(q.weight, 1.0f, 1000.0f);
    q.width = clampf(q.width, 50.0f, 200.0f);
    if (q.family.len >= sizeof ts->matches[0].family) {
        q.family.len = sizeof ts->matches[0].family - 1u;
    }
    ax_mutex_lock(&ts->fonts_mutex);
    for (i = 0; i < ts->match_count; ++i) {
        const tx_match *m = &ts->matches[i];
        if (m->family_len == q.family.len && memcmp(m->family, q.family.ptr, q.family.len) == 0 &&
            m->weight == q.weight && m->width == q.width && m->style == q.style) {
            *out = m->font;
            ax_mutex_unlock(&ts->fonts_mutex);
            return AX_OK;
        }
    }
    for (i = 0; i < ts->source_count && *out == 0; ++i) {
        ax_font_source *src = ts->sources[i];
        ax_font_file file;
        uint32_t blob = 0, face = 0;
        memset(&file, 0, sizeof file);
        if (!src->vtbl->match(src, &q, &file)) {
            continue;
        }
        last = blob_for(ts, &file, &blob);
        if (last != AX_OK) {
            continue;
        }
        if (!file.exact_family) {
            if (!fallback_set) {
                fallback_set = true;
                fallback_blob = blob;
                fallback_index = file.face_index;
            }
            continue;
        }
        last = face_for(ts, blob, file.face_index, &face);
        if (last == AX_OK) {
            last = font_for(ts, face, q.weight, q.width, out);
        }
    }
    if (*out == 0 && fallback_set) {
        uint32_t face = 0;
        last = face_for(ts, fallback_blob, fallback_index, &face);
        if (last == AX_OK) {
            last = font_for(ts, face, q.weight, q.width, out);
        }
    }
    if (*out != 0 && ts->match_count < TX_MAX_MATCHES) {
        tx_match *m = &ts->matches[ts->match_count++];
        memcpy(m->family, q.family.ptr, q.family.len);
        m->family[q.family.len] = '\0';
        m->family_len = (uint32_t)q.family.len;
        m->weight = q.weight;
        m->width = q.width;
        m->style = q.style;
        m->font = *out;
    }
    ax_mutex_unlock(&ts->fonts_mutex);
    if (*out == 0) {
        return last != AX_OK ? last : ax_set_error(AX_ERR_IO, "text: no font matches");
    }
    return AX_OK;
}

void ax_text_font_metrics(ax_text_system *ts, ax_font_id font, ax_font_metrics *out)
{
    const tx_font *f = tx_font_get(ts, font);
    if (f == NULL) {
        memset(out, 0, sizeof *out);
        return;
    }
    *out = f->metrics;
}

const char *ax_text_font_family(ax_text_system *ts, ax_font_id font)
{
    const tx_font *f = tx_font_get(ts, font);
    return f != NULL ? ts->faces[f->face].family : "";
}

/* ---- Raster instances ------------------------------------------------------------------------ */

ax_font_instance ax_text_font_instance(ax_text_system *ts, ax_font_id font, float pixel_size,
                                       float *out_scale)
{
    float size = pixel_size;
    uint32_t size64, h, n, i;
    instance_hash *hash = ihash(ts);
    ax_font_instance found = 0;
    if (out_scale != NULL) {
        *out_scale = 1.0f;
    }
    if (tx_font_get(ts, font) == NULL || !(pixel_size > 0.0f)) {
        return 0;
    }
    if (size > AX_TEXT_RASTER_MAX_SIZE) {
        if (out_scale != NULL) {
            *out_scale = size / AX_TEXT_RASTER_MAX_SIZE;
        }
        size = AX_TEXT_RASTER_MAX_SIZE;
    }
    size64 = (uint32_t)(size * 64.0f + 0.5f);
    if (size64 == 0) {
        size64 = 1;
    }
    h = (font * 2654435761u) ^ (size64 * 40503u);
    ax_mutex_lock(&ts->fonts_mutex);
    for (i = 0; i < INSTANCE_HASH; ++i) {
        uint32_t slot = (h + i) & (INSTANCE_HASH - 1u);
        uint32_t id = hash->slots[slot];
        if (id == 0) {
            n = ax_atomic_load_u32(&ts->instance_count, AX_MO_RELAXED);
            if (n == TX_MAX_INSTANCES) {
                break;
            }
            ts->instances[n].font = font;
            ts->instances[n].size64 = size64;
            ts->instances[n].hinted = size <= AX_TEXT_SUBPIXEL_MAX_SIZE;
            hash->slots[slot] = n + 1u;
            ax_atomic_store_u32(&ts->instance_count, n + 1u, AX_MO_RELEASE);
            found = n + 1u;
            break;
        }
        if (ts->instances[id - 1u].font == font && ts->instances[id - 1u].size64 == size64) {
            found = id;
            break;
        }
    }
    ax_mutex_unlock(&ts->fonts_mutex);
    return found;
}

float ax_text_instance_size(ax_text_system *ts, ax_font_instance instance)
{
    const tx_instance *in = tx_instance_get(ts, instance);
    return in != NULL ? (float)in->size64 / 64.0f : 0.0f;
}

/* ---- Counters -------------------------------------------------------------------------------- */

static uint64_t take(ax_atomic_u64 *c, bool reset)
{
    uint64_t v = ax_atomic_load_u64(c, AX_MO_RELAXED);
    if (reset) {
        ax_atomic_store_u64(c, 0, AX_MO_RELAXED);
    }
    return v;
}

void ax_text_get_counters(ax_text_system *ts, ax_text_counters *out, bool reset)
{
    out->paragraphs = take(&ts->c_paragraphs, reset);
    out->runs_shaped = take(&ts->c_runs_shaped, reset);
    out->runs_cached = take(&ts->c_runs_cached, reset);
    out->fragments_shaped = take(&ts->c_fragments, reset);
    out->layouts = take(&ts->c_layouts, reset);
    out->layouts_cached = take(&ts->c_layouts_cached, reset);
    out->glyphs_rasterized = take(&ts->c_glyphs, reset);
    out->glyphs_background = take(&ts->c_glyphs_bg, reset);
    out->glyphs_evicted = take(&ts->c_evicted, reset);
    out->glyph_bytes = ax_atomic_load_u64(&ts->c_glyph_bytes, AX_MO_RELAXED);
    out->glyph_pinned_skips = take(&ts->c_pinned, reset);
}

/* ---- The shaping cache's entries live in paragraph.c ----------------------------------------- */

bool tx_shape_equal(const tx_entry *e, const void *key);
void tx_shape_destroy(tx_entry *e, void *user);

static bool shape_equal(const tx_entry *e, const void *key)
{
    return tx_shape_equal(e, key);
}

static void shape_destroy(tx_entry *e, void *user)
{
    tx_shape_destroy(e, user);
}
