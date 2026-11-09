/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Line layout (decision 99): greedy wrapping at UAX #14 opportunities, re-wrapping without
 * re-shaping, alignment and line metrics; measured sizes for layout.
 *
 * White space. Newlines are hard breaks and spaces are kept (CSS's pre-wrap, the usual choice for
 * interface text, which comes from code rather than from markup); spaces at the end of a line hang:
 * they do not count toward its width or make it overflow.
 *
 * Re-wrapping. A paragraph is shaped once. A line that ends inside a run takes the glyphs up to
 * the break as they are when HarfBuzz marks the first glyph after the break safe to break;
 * otherwise (a ligature or a contextual form across the break) the text from the nearest safe glyph
 * before the break to the break is shaped again on its own, and so is the text from the break to
 * the next safe glyph, which starts the next line. Both fragments go through the shaping cache.
 * A line wider than the width without a break opportunity overflows (no emergency breaks).
 */
#include <math.h>
#include <string.h>

#include <linebreak.h>

#include "text_internal.h"

/* ---- Building lines -------------------------------------------------------------------------- */

typedef struct out_glyph {
    uint32_t id;
    uint32_t run;
    uint32_t cluster;
    float advance, x_offset, y_offset;
} out_glyph;

typedef struct builder {
    ax_text_paragraph *p;
    out_glyph *glyphs;
    uint32_t count, capacity;
    ax_text_line *lines;
    uint32_t line_count, line_capacity;
    bool failed;
} builder;

static bool grow(ax_text_system *ts, void **array, uint32_t *capacity, uint32_t need, size_t elem)
{
    uint32_t cap = *capacity < 16u ? 16u : *capacity;
    void *fresh;
    if (need <= *capacity) {
        return true;
    }
    while (cap < need) {
        cap *= 2u;
    }
    fresh = ax_mem_alloc(&ts->allocator, elem * cap, 8);
    if (fresh == NULL) {
        return false;
    }
    if (*array != NULL) {
        memcpy(fresh, *array, elem * *capacity);
        ax_mem_free(&ts->allocator, *array, elem * *capacity);
    }
    *array = fresh;
    *capacity = cap;
    return true;
}

static void push_glyph(builder *b, uint32_t id, uint32_t run, uint32_t cluster, float advance,
                       float xo, float yo)
{
    out_glyph *g;
    if (b->failed ||
        !grow(b->p->ts, (void **)&b->glyphs, &b->capacity, b->count + 1u, sizeof(out_glyph))) {
        b->failed = true;
        return;
    }
    g = &b->glyphs[b->count++];
    g->id = id;
    g->run = run;
    g->cluster = cluster;
    g->advance = advance;
    g->x_offset = xo;
    g->y_offset = yo;
}

static float run_scale(const ax_text_paragraph *p, uint32_t run)
{
    const tx_run *r = &p->runs[run];
    const tx_font *f = tx_font_get(p->ts, r->font);
    return p->styles[r->style].size / tx_face_get(p->ts, f->face)->upem;
}

/* Shapes [start, end) of `run` on its own (a fragment at an unsafe break) and appends it. Falls
 * back to the paragraph's glyphs when out of memory. */
static void push_fragment(builder *b, uint32_t run, uint32_t start, uint32_t end, uint32_t g0,
                          uint32_t g1)
{
    ax_text_paragraph *p = b->p;
    const tx_run *r = &p->runs[run];
    const ax_text_style *st = &p->styles[r->style];
    tx_shape_key k;
    tx_shaped *s;
    uint32_t i;
    if (start >= end) {
        return;
    }
    k.font = r->font;
    k.script = r->script;
    k.direction = r->direction;
    k.feature_count = st->feature_count;
    k.features = st->features;
    k.text = p->text + start;
    k.text_len = end - start;
    s = tx_shape(p->ts, &k, true);
    if (s == NULL) {
        for (i = g0; i < g1; ++i) {
            const tx_pglyph *pg = &p->glyphs[i];
            push_glyph(b, pg->id, pg->run, pg->cluster, pg->advance, pg->x_offset, pg->y_offset);
        }
        return;
    }
    {
        const float scale = run_scale(p, run);
        for (i = 0; i < s->count; ++i) {
            push_glyph(b, s->ids[i], run, start + s->clusters[i], (float)s->advance[i] * scale,
                       (float)s->x_offset[i] * scale, (float)s->y_offset[i] * scale);
        }
    }
    tx_cache_release(&p->ts->shape_cache, &s->entry);
}

/* The first glyph whose cluster is at or after `byte` (glyph_count when none). */
static uint32_t glyph_at(const ax_text_paragraph *p, uint32_t byte)
{
    uint32_t lo = 0, hi = p->glyph_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (p->glyphs[mid].cluster < byte) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* Appends the paragraph's glyphs [g0, g1). */
static void push_glyphs(builder *b, uint32_t g0, uint32_t g1)
{
    const ax_text_paragraph *p = b->p;
    uint32_t i;
    for (i = g0; i < g1; ++i) {
        const tx_pglyph *pg = &p->glyphs[i];
        push_glyph(b, pg->id, pg->run, pg->cluster, pg->advance, pg->x_offset, pg->y_offset);
    }
}

/* The text end of glyphs up to (excluding) glyph m of run `run`, clipped to `end`. */
static uint32_t cluster_end(const ax_text_paragraph *p, uint32_t m, uint32_t run, uint32_t end)
{
    uint32_t e = m < p->glyph_count && p->glyphs[m].run == run ? p->glyphs[m].cluster
                                                               : p->runs[run].end;
    return e < end ? e : end;
}

/*
 * Appends the glyphs of the text [start, end). An edge inside a run is unsafe when the glyph after
 * it carries UNSAFE_TO_BREAK (HarfBuzz: breaking before that glyph changes the shaping). Then the
 * text from the edge to the nearest glyph that is safe to break before is shaped again on its own:
 * after the line's start edge (the head) and before its end edge (the tail).
 */
static void push_range(builder *b, uint32_t start, uint32_t end)
{
    const ax_text_paragraph *p = b->p;
    const uint32_t g0 = glyph_at(p, start), g1 = glyph_at(p, end);
    uint32_t head = g0; /* glyphs [g0, head) are replaced by the head fragment */
    uint32_t tail = g1; /* glyphs [tail, g1) by the tail fragment */
    if (g0 >= g1) {
        return;
    }
    if (g0 > 0 && p->glyphs[g0 - 1u].run == p->glyphs[g0].run && p->glyphs[g0].unsafe) {
        const uint32_t run = p->glyphs[g0].run;
        head = g0 + 1u;
        while (head < g1 && p->glyphs[head].run == run && p->glyphs[head].unsafe) {
            head++;
        }
    }
    if (g1 < p->glyph_count && p->glyphs[g1 - 1u].run == p->glyphs[g1].run &&
        p->glyphs[g1].unsafe) {
        const uint32_t run = p->glyphs[g1].run;
        tail = g1 - 1u;
        while (tail > g0 && p->glyphs[tail].run == run && p->glyphs[tail].unsafe) {
            tail--;
        }
    }
    if (head > g0 && tail < g1 && tail <= head) {
        /* The fragments meet: the whole range (one run) is shaped on its own. */
        push_fragment(b, p->glyphs[g0].run, start, end, g0, g1);
        return;
    }
    if (head > g0) {
        const uint32_t run = p->glyphs[g0].run;
        push_fragment(b, run, start, cluster_end(p, head, run, end), g0, head);
    }
    push_glyphs(b, head, tail);
    if (tail < g1) {
        push_fragment(b, p->glyphs[tail].run, p->glyphs[tail].cluster, end, tail, g1);
    }
}

static bool is_hanging_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Vertical metrics of a run's style: ascent and descent of its line box share. */
static void run_metrics(const ax_text_paragraph *p, uint32_t style, ax_font_id font, float *asc,
                        float *desc)
{
    const ax_text_style *st = &p->styles[style];
    const tx_font *f = tx_font_get(p->ts, font);
    const float a = f->metrics.ascent * st->size;
    const float d = f->metrics.descent * st->size;
    const float lh = st->line_height > 0.0f
                         ? st->line_height * st->size
                         : (f->metrics.ascent + f->metrics.descent + f->metrics.line_gap) * st->size;
    const float half = (lh - (a + d)) * 0.5f;
    *asc = a + half;
    *desc = d + half;
}

/* Finishes a line of text [start, end) whose visible width is `width`. */
static void end_line(builder *b, uint32_t start, uint32_t end, float width)
{
    ax_text_paragraph *p = b->p;
    ax_text_line *l;
    uint32_t first = b->count;
    uint32_t r;
    float asc = 0.0f, desc = 0.0f;
    bool any = false;
    if (b->failed || !grow(p->ts, (void **)&b->lines, &b->line_capacity, b->line_count + 1u,
                           sizeof(ax_text_line))) {
        b->failed = true;
        return;
    }
    push_range(b, start, end);
    if (!b->failed) {
        /* The width of the glyphs as placed (fragments included), without the trailing spaces
         * that hang. */
        uint32_t i = b->count;
        float w = 0.0f;
        bool trailing = true;
        while (i > first) {
            const out_glyph *og = &b->glyphs[--i];
            if (trailing && is_hanging_space(p->text[og->cluster])) {
                continue;
            }
            trailing = false;
            w += og->advance;
        }
        width = w;
    }
    /* The line box: the tallest share of the runs on the line. */
    for (r = 0; r < p->run_count; ++r) {
        if (p->runs[r].end > start && p->runs[r].start < (end > start ? end : start + 1u)) {
            float a, d;
            run_metrics(p, p->runs[r].style, p->runs[r].font, &a, &d);
            asc = any && asc > a ? asc : a;
            desc = any && desc > d ? desc : d;
            any = true;
        }
    }
    if (!any) {
        /* An empty line (after a final newline): the last run's metrics. */
        const tx_run *last = &p->runs[p->run_count - 1u];
        run_metrics(p, last->style, last->font, &asc, &desc);
    }
    l = &b->lines[b->line_count++];
    memset(l, 0, sizeof *l);
    l->start = start;
    l->end = end;
    l->width = width;
    l->first_glyph = first;
    l->glyph_count = b->count - first;
    l->height = asc + desc;
    l->baseline = asc; /* relative for now; positioned in layout() */
}

/* Greedy wrapping of the whole paragraph at max_width (NaN: only hard breaks). */
static void wrap(builder *b, float max_width)
{
    ax_text_paragraph *p = b->p;
    const bool limited = !isnan(max_width);
    uint32_t line_start = 0;
    uint32_t g = 0;
    float width = 0.0f;       /* from line_start, trailing spaces included */
    float hang = 0.0f;        /* advance of the trailing spaces */
    uint32_t cand = UINT32_MAX; /* last break opportunity on the line (byte) */
    uint32_t cand_g = 0;
    float cand_width = 0.0f; /* visible width up to cand */
    if (p->len == 0 || p->run_count == 0) {
        return;
    }
    while (g < p->glyph_count) {
        /* One cluster: the glyphs with the same cluster value. */
        const uint32_t cs = p->glyphs[g].cluster;
        uint32_t ge = g;
        uint32_t ce;
        float adv = 0.0f;
        bool space;
        char brk;
        while (ge < p->glyph_count && p->glyphs[ge].cluster == cs) {
            adv += p->glyphs[ge].advance;
            ge++;
        }
        ce = ge < p->glyph_count ? p->glyphs[ge].cluster : p->len;
        space = is_hanging_space(p->text[cs]);
        if (!space && limited && cand != UINT32_MAX && width + adv > max_width) {
            /* Overflow: the line ends at the last opportunity, and the next one is measured from
             * there (the spaces before it hang on this line). */
            end_line(b, line_start, cand, cand_width);
            line_start = cand;
            width = 0.0f;
            hang = 0.0f;
            cand = UINT32_MAX;
            g = cand_g;
            continue;
        }
        width += adv;
        hang = space ? hang + adv : 0.0f;
        brk = ce > 0 ? p->breaks[ce - 1u] : LINEBREAK_NOBREAK;
        if (ce < p->len && brk == LINEBREAK_MUSTBREAK) {
            end_line(b, line_start, ce, width - hang);
            line_start = ce;
            width = 0.0f;
            hang = 0.0f;
            cand = UINT32_MAX;
        } else if (ce < p->len && brk == LINEBREAK_ALLOWBREAK) {
            cand = ce;
            cand_g = ge;
            cand_width = width - hang;
        }
        g = ge;
    }
    end_line(b, line_start, p->len, width - hang);
    /* Text ending with a hard break has an empty last line. */
    if (p->len > 0 && (p->text[p->len - 1u] == '\n' || p->text[p->len - 1u] == '\r')) {
        end_line(b, p->len, p->len, 0.0f);
    }
}

/* ---- Layouts --------------------------------------------------------------------------------- */

static bool same_width(float a, float b)
{
    return (isnan(a) && isnan(b)) || a == b;
}

static tx_layout *layout_make(ax_text_paragraph *p, float max_width, float box_width)
{
    ax_text_system *ts = p->ts;
    builder b;
    tx_layout *l;
    size_t bytes;
    uint32_t i;
    float top = 0.0f, widest = 0.0f, box;
    ax_text_line *lines;
    ax_text_glyph *glyphs;
    memset(&b, 0, sizeof b);
    b.p = p;
    wrap(&b, max_width);
    if (b.failed) {
        ax_mem_free(&ts->allocator, b.glyphs, sizeof(out_glyph) * b.capacity);
        ax_mem_free(&ts->allocator, b.lines, sizeof(ax_text_line) * b.line_capacity);
        return NULL;
    }
    bytes = ax_align_up(sizeof(tx_layout), 8) + sizeof(ax_text_line) * b.line_count +
            sizeof(ax_text_glyph) * b.count;
    l = (tx_layout *)ax_mem_alloc(&ts->allocator, bytes, 8);
    if (l != NULL) {
        memset(l, 0, sizeof *l);
        l->bytes = bytes;
        l->ts = ts;
        ax_atomic_init_u32(&l->refs, 1u);
        lines = (ax_text_line *)(void *)((unsigned char *)l + ax_align_up(sizeof(tx_layout), 8));
        glyphs = (ax_text_glyph *)(void *)(lines + b.line_count);
        for (i = 0; i < b.line_count; ++i) {
            widest = b.lines[i].width > widest ? b.lines[i].width : widest;
        }
        box = isnan(box_width) ? widest : box_width;
        for (i = 0; i < b.line_count; ++i) {
            ax_text_line *ln = &lines[i];
            uint32_t k;
            float pen;
            *ln = b.lines[i];
            ln->top = top;
            ln->baseline = top + b.lines[i].baseline;
            switch (p->align) {
            case AX_TEXT_ALIGN_CENTER:
                ln->x = (box - ln->width) * 0.5f;
                break;
            case AX_TEXT_ALIGN_END:
                ln->x = box - ln->width;
                break;
            default:
                ln->x = 0.0f;
                break;
            }
            pen = ln->x;
            for (k = 0; k < ln->glyph_count; ++k) {
                const out_glyph *og = &b.glyphs[ln->first_glyph + k];
                ax_text_glyph *tg = &glyphs[ln->first_glyph + k];
                const tx_run *r = &p->runs[og->run];
                tg->id = og->id;
                tg->font = r->font;
                tg->size = p->styles[r->style].size;
                tg->x = pen + og->x_offset;
                tg->y = ln->baseline - og->y_offset;
                tg->cluster = og->cluster;
                tg->style = r->style;
                pen += og->advance;
            }
            top += ln->height;
        }
        l->pub.max_width = max_width;
        l->pub.box_width = box_width; /* as asked (NaN: aligned in the widest line) */
        l->pub.width = widest;
        l->pub.height = top;
        l->pub.line_count = b.line_count;
        l->pub.glyph_count = b.count;
        l->pub.lines = lines;
        l->pub.glyphs = glyphs;
    }
    ax_mem_free(&ts->allocator, b.glyphs, sizeof(out_glyph) * b.capacity);
    ax_mem_free(&ts->allocator, b.lines, sizeof(ax_text_line) * b.line_capacity);
    return l;
}

const ax_text_layout *ax_text_paragraph_layout(ax_text_paragraph *p, float max_width,
                                               float box_width)
{
    tx_layout *l = NULL;
    uint32_t i;
    (void)ax_text_paragraph_shape(p);
    ax_mutex_lock(&p->mutex);
    for (i = 0; i < TX_LAYOUT_CACHE; ++i) {
        tx_layout *c = p->cache[i];
        if (c != NULL && same_width(c->pub.max_width, max_width) &&
            same_width(c->pub.box_width, box_width)) {
            l = c;
            break;
        }
    }
    if (l != NULL) {
        (void)ax_atomic_fetch_add_u32(&l->refs, 1u, AX_MO_RELAXED);
        ax_mutex_unlock(&p->mutex);
        tx_count(&p->ts->c_layouts_cached, 1);
        return &l->pub;
    }
    ax_mutex_unlock(&p->mutex);
    /* Laid out without the lock: other threads may lay out other widths meanwhile. */
    l = layout_make(p, max_width, box_width);
    if (l == NULL) {
        return NULL;
    }
    tx_count(&p->ts->c_layouts, 1);
    ax_mutex_lock(&p->mutex);
    {
        tx_layout *old = p->cache[p->cache_next];
        p->cache[p->cache_next] = l;
        p->cache_next = (p->cache_next + 1u) % TX_LAYOUT_CACHE;
        (void)ax_atomic_fetch_add_u32(&l->refs, 1u, AX_MO_RELAXED); /* the cache's */
        ax_mutex_unlock(&p->mutex);
        if (old != NULL) {
            ax_text_layout_release(&old->pub);
        }
    }
    return &l->pub;
}

void ax_text_layout_retain(const ax_text_layout *pub)
{
    tx_layout *l = (tx_layout *)(void *)(uintptr_t)pub;
    (void)ax_atomic_fetch_add_u32(&l->refs, 1u, AX_MO_RELAXED);
}

void ax_text_layout_release(const ax_text_layout *pub)
{
    tx_layout *l = (tx_layout *)(void *)(uintptr_t)pub;
    if (l != NULL && ax_atomic_fetch_add_u32(&l->refs, UINT32_MAX, AX_MO_ACQ_REL) == 1u) {
        ax_mem_free(&l->ts->allocator, l, l->bytes);
    }
}

void ax_text_paragraph_measure(ax_text_paragraph *p, float max_width, float *out_width,
                               float *out_height)
{
    const ax_text_layout *l = ax_text_paragraph_layout(p, max_width, NAN);
    *out_width = l != NULL ? l->width : 0.0f;
    *out_height = l != NULL ? l->height : 0.0f;
    if (l != NULL) {
        ax_text_layout_release(l);
    }
}
