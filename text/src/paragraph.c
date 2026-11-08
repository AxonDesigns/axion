/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Paragraphs: itemization, breaks and shaping (decision 99).
 */
#include <string.h>

#include <graphemebreak.h>
#include <linebreak.h>

#include "text_internal.h"

#include <axion/base/ax_error_set.h>

/* ---- Features -------------------------------------------------------------------------------- */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

uint32_t ax_text_parse_features(ax_str css, ax_text_feature *out, uint32_t max,
                                ax_result *out_result)
{
    size_t i = 0;
    uint32_t n = 0;
    if (out_result != NULL) {
        *out_result = AX_OK;
    }
    for (;;) {
        char tag[4];
        uint32_t value = 1;
        size_t k;
        char quote = 0;
        while (i < css.len && is_space(css.ptr[i])) {
            i++;
        }
        if (i == css.len) {
            if (n > 0 && css.len > 0 && css.ptr[css.len - 1] == ',') {
                goto bad; /* a trailing comma */
            }
            return n;
        }
        if (css.ptr[i] == '"' || css.ptr[i] == '\'') {
            quote = css.ptr[i++];
        }
        for (k = 0; k < 4; ++k) {
            char c = i < css.len ? css.ptr[i] : '\0';
            if (c < 0x20 || c > 0x7E || c == '"' || c == '\'' || c == ',' || (!quote && is_space(c))) {
                goto bad;
            }
            tag[k] = c;
            i++;
        }
        if (quote) {
            if (i >= css.len || css.ptr[i] != quote) {
                goto bad;
            }
            i++;
        } else if (i < css.len && !is_space(css.ptr[i]) && css.ptr[i] != ',') {
            goto bad; /* a tag longer than four characters */
        }
        while (i < css.len && is_space(css.ptr[i])) {
            i++;
        }
        if (i < css.len && css.ptr[i] != ',') {
            if (css.len - i >= 2 && memcmp(css.ptr + i, "on", 2) == 0) {
                value = 1;
                i += 2;
            } else if (css.len - i >= 3 && memcmp(css.ptr + i, "off", 3) == 0) {
                value = 0;
                i += 3;
            } else if (css.ptr[i] >= '0' && css.ptr[i] <= '9') {
                value = 0;
                while (i < css.len && css.ptr[i] >= '0' && css.ptr[i] <= '9') {
                    if (value > 65535u) {
                        goto bad;
                    }
                    value = value * 10u + (uint32_t)(css.ptr[i] - '0');
                    i++;
                }
            } else {
                goto bad;
            }
            while (i < css.len && is_space(css.ptr[i])) {
                i++;
            }
        }
        if (n == max) {
            goto bad;
        }
        if (out != NULL) {
            out[n].tag = ((uint32_t)(unsigned char)tag[0] << 24) |
                         ((uint32_t)(unsigned char)tag[1] << 16) |
                         ((uint32_t)(unsigned char)tag[2] << 8) | (uint32_t)(unsigned char)tag[3];
            out[n].value = value;
        }
        n++;
        if (i < css.len) {
            if (css.ptr[i] != ',') {
                goto bad;
            }
            i++;
        }
    }
bad:
    if (out_result != NULL) {
        *out_result = AX_ERR_INVALID_ARGUMENT;
    }
    return 0;
}


/* ---- The shaping cache ----------------------------------------------------------------------- */

bool tx_shape_equal(const tx_entry *e, const void *key);
void tx_shape_destroy(tx_entry *e, void *user);

static uint64_t shape_hash(const tx_shape_key *k)
{
    uint64_t h = TX_HASH_SEED;
    h = tx_hash_bytes(h, &k->font, sizeof k->font);
    h = tx_hash_bytes(h, &k->script, sizeof k->script);
    h = tx_hash_bytes(h, &k->direction, sizeof k->direction);
    h = tx_hash_bytes(h, &k->feature_count, sizeof k->feature_count);
    if (k->feature_count > 0) {
        h = tx_hash_bytes(h, k->features, sizeof(ax_text_feature) * k->feature_count);
    }
    return tx_hash_bytes(h, k->text, k->text_len);
}

bool tx_shape_equal(const tx_entry *e, const void *key)
{
    const tx_shaped *s = (const tx_shaped *)(const void *)e;
    const tx_shape_key *k = (const tx_shape_key *)key;
    return s->font == k->font && s->script == k->script && s->direction == k->direction &&
           s->feature_count == k->feature_count && s->text_len == k->text_len &&
           (k->feature_count == 0 ||
            memcmp(s->features, k->features, sizeof(ax_text_feature) * k->feature_count) == 0) &&
           (k->text_len == 0 || memcmp(s->text, k->text, k->text_len) == 0);
}

void tx_shape_destroy(tx_entry *e, void *user)
{
    ax_text_system *ts = (ax_text_system *)user;
    ax_mem_free(&ts->allocator, e, e->bytes);
}

/* One allocation: the entry, its key's text and its glyph arrays. */
static tx_shaped *shaped_alloc(ax_text_system *ts, const tx_shape_key *k, uint32_t count)
{
    size_t bytes = sizeof(tx_shaped);
    size_t text_at, ids_at, clusters_at, adv_at, xo_at, yo_at, unsafe_at;
    tx_shaped *s;
    unsigned char *base;
    bytes = ax_align_up(bytes, 8);
    text_at = bytes;
    bytes = ax_align_up(bytes + k->text_len + 1u, 8);
    ids_at = bytes;
    bytes += sizeof(uint32_t) * count;
    clusters_at = bytes;
    bytes += sizeof(uint32_t) * count;
    adv_at = bytes;
    bytes += sizeof(int32_t) * count;
    xo_at = bytes;
    bytes += sizeof(int32_t) * count;
    yo_at = bytes;
    bytes += sizeof(int32_t) * count;
    unsafe_at = bytes;
    bytes = ax_align_up(bytes + count + 1u, 8);
    base = (unsigned char *)ax_mem_alloc(&ts->allocator, bytes, 8);
    if (base == NULL) {
        return NULL;
    }
    s = (tx_shaped *)(void *)base;
    memset(s, 0, sizeof *s);
    s->entry.bytes = bytes;
    s->font = k->font;
    s->script = k->script;
    s->direction = k->direction;
    s->feature_count = k->feature_count;
    if (k->feature_count > 0) {
        memcpy(s->features, k->features, sizeof(ax_text_feature) * k->feature_count);
    }
    s->text = (char *)(base + text_at);
    s->text_len = k->text_len;
    if (k->text_len > 0) {
        memcpy(s->text, k->text, k->text_len);
    }
    s->text[k->text_len] = '\0';
    s->count = count;
    s->ids = (uint32_t *)(void *)(base + ids_at);
    s->clusters = (uint32_t *)(void *)(base + clusters_at);
    s->advance = (int32_t *)(void *)(base + adv_at);
    s->x_offset = (int32_t *)(void *)(base + xo_at);
    s->y_offset = (int32_t *)(void *)(base + yo_at);
    s->unsafe = base + unsafe_at;
    return s;
}

tx_shaped *tx_shape(ax_text_system *ts, const tx_shape_key *k, bool fragment)
{
    const uint64_t hash = shape_hash(k);
    const tx_font *font = tx_font_get(ts, k->font);
    tx_entry *e = tx_cache_get(&ts->shape_cache, hash, k);
    hb_buffer_t *buf;
    hb_feature_t features[AX_TEXT_MAX_FEATURES];
    hb_glyph_info_t *info;
    hb_glyph_position_t *pos;
    unsigned int count = 0, i;
    tx_shaped *s;
    if (e != NULL) {
        tx_count(&ts->c_runs_cached, 1);
        return (tx_shaped *)(void *)e;
    }
    if (font == NULL) {
        return NULL;
    }
    buf = hb_buffer_create();
    if (!hb_buffer_allocation_successful(buf)) {
        hb_buffer_destroy(buf);
        return NULL;
    }
    hb_buffer_add_utf8(buf, k->text, (int)k->text_len, 0, (int)k->text_len);
    hb_buffer_set_direction(buf, (hb_direction_t)k->direction);
    hb_buffer_set_script(buf, (hb_script_t)k->script);
    hb_buffer_set_language(buf, hb_language_from_string("en", 2));
    for (i = 0; i < k->feature_count; ++i) {
        features[i].tag = k->features[i].tag;
        features[i].value = k->features[i].value;
        features[i].start = HB_FEATURE_GLOBAL_START;
        features[i].end = HB_FEATURE_GLOBAL_END;
    }
    hb_shape(font->hb, buf, features, k->feature_count);
    info = hb_buffer_get_glyph_infos(buf, &count);
    pos = hb_buffer_get_glyph_positions(buf, NULL);
    s = shaped_alloc(ts, k, count);
    if (s == NULL || !hb_buffer_allocation_successful(buf)) {
        hb_buffer_destroy(buf);
        if (s != NULL) {
            ax_mem_free(&ts->allocator, s, s->entry.bytes);
        }
        return NULL;
    }
    s->entry.hash = hash;
    for (i = 0; i < count; ++i) {
        s->ids[i] = info[i].codepoint;
        s->clusters[i] = info[i].cluster;
        s->advance[i] = pos[i].x_advance;
        s->x_offset[i] = pos[i].x_offset;
        s->y_offset[i] = pos[i].y_offset;
        s->unsafe[i] =
            (hb_glyph_info_get_glyph_flags(&info[i]) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0 ? 1 : 0;
    }
    hb_buffer_destroy(buf);
    tx_count(fragment ? &ts->c_fragments : &ts->c_runs_shaped, 1);
    e = tx_cache_put(&ts->shape_cache, &s->entry, k);
    if (e != &s->entry) {
        tx_shape_destroy(&s->entry, ts); /* another thread shaped it first */
    }
    return (tx_shaped *)(void *)e;
}

/* ---- Creating a paragraph: itemization and breaks ------------------------------------------- */

static void paragraph_free(ax_text_paragraph *p)
{
    const ax_allocator *a = &p->ts->allocator;
    uint32_t i;
    for (i = 0; i < TX_LAYOUT_CACHE; ++i) {
        if (p->cache[i] != NULL) {
            ax_text_layout_release(&p->cache[i]->pub);
        }
    }
    for (i = 0; i < p->run_count; ++i) {
        if (p->runs[i].shaped != NULL) {
            tx_cache_release(&p->ts->shape_cache, &p->runs[i].shaped->entry);
        }
    }
    ax_mem_free(a, p->glyphs, sizeof(tx_pglyph) * p->glyph_count);
    ax_mem_free(a, p->runs, sizeof(tx_run) * (p->len + 2u));
    ax_mem_free(a, p->breaks, p->len + 1u);
    ax_mem_free(a, p->graphemes, p->len + 1u);
    ax_mem_free(a, p->styles, sizeof(ax_text_style) * p->style_count);
    ax_mem_free(a, p->text, p->len + 1u);
    ax_mutex_deinit(&p->mutex);
    ax_mem_free(a, p, sizeof *p);
}

void ax_text_paragraph_retain(ax_text_paragraph *p)
{
    (void)ax_atomic_fetch_add_u32(&p->refs, 1u, AX_MO_RELAXED);
}

void ax_text_paragraph_release(ax_text_paragraph *p)
{
    if (p != NULL && ax_atomic_fetch_add_u32(&p->refs, UINT32_MAX, AX_MO_ACQ_REL) == 1u) {
        paragraph_free(p);
    }
}

ax_str ax_text_paragraph_text(const ax_text_paragraph *p)
{
    ax_str s;
    s.ptr = p->text;
    s.len = p->len;
    return s;
}

/*
 * Itemization: a chain of stages, each of which only splits runs. A stage gets the runs so far and
 * writes the refined ones; runs carry what every later stage needs. Today: style spans, then the
 * font of each run's style. Bidi (levels and direction), script and font fallback per grapheme
 * cluster are stages to insert in M3; layout reads runs only through their fields.
 */
typedef uint32_t (*itemize_stage)(const ax_text_paragraph *p, const ax_text_paragraph_desc *d,
                                  const tx_run *in, uint32_t n, tx_run *out);

static uint32_t stage_spans(const ax_text_paragraph *p, const ax_text_paragraph_desc *d,
                            const tx_run *in, uint32_t n, tx_run *out)
{
    uint32_t i, k = 0;
    (void)p;
    for (i = 0; i < n; ++i) {
        uint32_t j;
        if (d->spans == NULL) {
            out[k++] = in[i];
            continue;
        }
        for (j = 0; j < d->span_count; ++j) {
            uint32_t a = d->spans[j].start > in[i].start ? d->spans[j].start : in[i].start;
            uint32_t b = d->spans[j].end < in[i].end ? d->spans[j].end : in[i].end;
            if (a < b) {
                out[k] = in[i];
                out[k].start = a;
                out[k].end = b;
                out[k].style = d->spans[j].style;
                k++;
            }
        }
    }
    return k;
}

static uint32_t stage_fonts(const ax_text_paragraph *p, const ax_text_paragraph_desc *d,
                            const tx_run *in, uint32_t n, tx_run *out)
{
    uint32_t i, k = 0;
    (void)d;
    for (i = 0; i < n; ++i) {
        out[k] = in[i];
        out[k].font = p->styles[in[i].style].font;
        /* Adjacent runs with the same style and font merge (two spans of one style). */
        if (k > 0 && out[k - 1].end == out[k].start && out[k - 1].style == out[k].style &&
            out[k - 1].font == out[k].font) {
            out[k - 1].end = out[k].end;
            continue;
        }
        k++;
    }
    return k;
}

/* Hard breaks end runs, so no run is shaped across a newline. */
static uint32_t stage_hard_breaks(const ax_text_paragraph *p, const ax_text_paragraph_desc *d,
                                  const tx_run *in, uint32_t n, tx_run *out)
{
    uint32_t i, k = 0;
    (void)d;
    for (i = 0; i < n; ++i) {
        uint32_t start = in[i].start, b;
        for (b = in[i].start; b + 1u < in[i].end; ++b) {
            if (p->breaks[b] == LINEBREAK_MUSTBREAK) {
                out[k] = in[i];
                out[k].start = start;
                out[k].end = b + 1u;
                k++;
                start = b + 1u;
            }
        }
        out[k] = in[i];
        out[k].start = start;
        k++;
    }
    return k;
}

static const itemize_stage STAGES[] = {stage_spans, stage_fonts, stage_hard_breaks};

ax_result ax_text_paragraph_create(ax_text_system *ts, const ax_text_paragraph_desc *d,
                                   ax_text_paragraph **out)
{
    const ax_allocator *a = &ts->allocator;
    ax_text_paragraph *p;
    tx_run *scratch;
    uint32_t i, n;
    *out = NULL;
    if (d->style_count == 0 || d->length >= UINT32_MAX - 2u ||
        (d->length > 0 && d->text == NULL)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_text_paragraph_create: no style or text");
    }
    for (i = 0; i < d->style_count; ++i) {
        if (tx_font_get(ts, d->styles[i].font) == NULL || !(d->styles[i].size > 0.0f) ||
            d->styles[i].feature_count > AX_TEXT_MAX_FEATURES) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "ax_text_paragraph_create: style %u has no font or no size",
                                (unsigned)i);
        }
    }
    if (d->spans != NULL) {
        uint32_t at = 0;
        for (i = 0; i < d->span_count; ++i) {
            if (d->spans[i].start != at || d->spans[i].end < d->spans[i].start ||
                d->spans[i].end > d->length || d->spans[i].style >= d->style_count) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "ax_text_paragraph_create: span %u is out of order or range",
                                    (unsigned)i);
            }
            at = d->spans[i].end;
        }
        if (at != d->length) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "ax_text_paragraph_create: the spans do not cover the text");
        }
    }
    p = (ax_text_paragraph *)ax_mem_alloc(a, sizeof *p, AX_ALIGNOF(ax_text_paragraph));
    if (p == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_text_paragraph_create: out of memory");
    }
    memset(p, 0, sizeof *p);
    ax_atomic_init_u32(&p->refs, 1u);
    ax_atomic_init_u32(&p->state, 0u);
    ax_mutex_init(&p->mutex);
    p->ts = ts;
    p->len = (uint32_t)d->length;
    p->align = d->align;
    p->style_count = d->style_count;
    p->text = (char *)ax_mem_alloc(a, p->len + 1u, 1);
    p->styles = (ax_text_style *)ax_mem_alloc(a, sizeof(ax_text_style) * d->style_count,
                                              AX_ALIGNOF(ax_text_style));
    p->breaks = (char *)ax_mem_alloc(a, p->len + 1u, 1);
    p->graphemes = (char *)ax_mem_alloc(a, p->len + 1u, 1);
    /* Runs: at most one per byte, plus one (an empty paragraph has none). */
    p->runs = (tx_run *)ax_mem_alloc(a, sizeof(tx_run) * (p->len + 2u), AX_ALIGNOF(tx_run));
    scratch = (tx_run *)ax_mem_alloc(a, sizeof(tx_run) * (p->len + 2u), AX_ALIGNOF(tx_run));
    if (p->text == NULL || p->styles == NULL || p->breaks == NULL || p->graphemes == NULL ||
        p->runs == NULL || scratch == NULL) {
        ax_mem_free(a, scratch, sizeof(tx_run) * (p->len + 2u));
        paragraph_free(p);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_text_paragraph_create: out of memory");
    }
    if (p->len > 0) {
        memcpy(p->text, d->text, p->len);
    }
    p->text[p->len] = '\0';
    memcpy(p->styles, d->styles, sizeof(ax_text_style) * d->style_count);

    /* Breaks (UAX #14) and graphemes (UAX #29), once. */
    memset(p->breaks, LINEBREAK_NOBREAK, p->len + 1u);
    memset(p->graphemes, GRAPHEMEBREAK_NOBREAK, p->len + 1u);
    if (p->len > 0) {
        set_linebreaks_utf8((const utf8_t *)p->text, p->len, "en", p->breaks);
        set_graphemebreaks_utf8((const utf8_t *)p->text, p->len, "en", p->graphemes);
    }

    /* Itemize. */
    n = 0;
    if (p->len > 0) {
        tx_run *in = scratch, *outr = p->runs;
        memset(&in[0], 0, sizeof in[0]);
        in[0].start = 0;
        in[0].end = p->len;
        in[0].style = 0;
        in[0].script = (uint32_t)HB_SCRIPT_LATIN;
        in[0].direction = (uint32_t)HB_DIRECTION_LTR;
        n = 1;
        for (i = 0; i < sizeof STAGES / sizeof STAGES[0]; ++i) {
            n = STAGES[i](p, d, in, n, outr);
            {
                tx_run *t = in;
                in = outr;
                outr = t;
            }
        }
        if (in != p->runs) {
            memcpy(p->runs, in, sizeof(tx_run) * n);
        }
    }
    p->run_count = n;
    ax_mem_free(a, scratch, sizeof(tx_run) * (p->len + 2u));
    tx_count(&ts->c_paragraphs, 1);
    *out = p;
    return AX_OK;
}

/* ---- Shaping --------------------------------------------------------------------------------- */

static bool shape_runs(ax_text_paragraph *p)
{
    ax_text_system *ts = p->ts;
    uint32_t i, total = 0, g;
    for (i = 0; i < p->run_count; ++i) {
        tx_run *r = &p->runs[i];
        const ax_text_style *st = &p->styles[r->style];
        tx_shape_key k;
        k.font = r->font;
        k.script = r->script;
        k.direction = r->direction;
        k.feature_count = st->feature_count;
        k.features = st->features;
        k.text = p->text + r->start;
        k.text_len = r->end - r->start;
        r->shaped = tx_shape(ts, &k, false);
        if (r->shaped == NULL) {
            return false;
        }
        total += r->shaped->count;
    }
    if (total > 0) {
        p->glyphs = (tx_pglyph *)ax_mem_alloc(&ts->allocator, sizeof(tx_pglyph) * total,
                                              AX_ALIGNOF(tx_pglyph));
        if (p->glyphs == NULL) {
            return false;
        }
    }
    p->glyph_count = total;
    g = 0;
    for (i = 0; i < p->run_count; ++i) {
        const tx_run *r = &p->runs[i];
        const tx_shaped *s = r->shaped;
        const float scale = p->styles[r->style].size / tx_face_get(ts, tx_font_get(ts, r->font)->face)->upem;
        uint32_t k;
        for (k = 0; k < s->count; ++k, ++g) {
            tx_pglyph *pg = &p->glyphs[g];
            pg->id = s->ids[k];
            pg->cluster = r->start + s->clusters[k];
            pg->run = i;
            pg->advance = (float)s->advance[k] * scale;
            pg->x_offset = (float)s->x_offset[k] * scale;
            pg->y_offset = (float)s->y_offset[k] * scale;
            pg->unsafe = s->unsafe[k] != 0;
        }
    }
    return true;
}

bool ax_text_paragraph_shape(ax_text_paragraph *p)
{
    uint32_t state = ax_atomic_load_u32(&p->state, AX_MO_ACQUIRE);
    if (state != 0) {
        return state == 1u;
    }
    ax_mutex_lock(&p->mutex);
    state = ax_atomic_load_u32(&p->state, AX_MO_RELAXED);
    if (state == 0) {
        state = shape_runs(p) ? 1u : 2u;
        if (state == 2u) {
            p->glyph_count = 0; /* lays out as empty */
        }
        ax_atomic_store_u32(&p->state, state, AX_MO_RELEASE);
    }
    ax_mutex_unlock(&p->mutex);
    return state == 1u;
}

bool ax_text_paragraph_is_shaped(const ax_text_paragraph *p)
{
    return ax_atomic_load_u32(&p->state, AX_MO_ACQUIRE) != 0;
}
