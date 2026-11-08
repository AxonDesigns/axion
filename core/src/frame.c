/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The frame stages after applying transactions (ax_frame.h): window roots, minimal positioning,
 * and recording the Render IR from the node store with a per-node cache of encoded items.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_jobs.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_frame.h>
#include <axion/core/ax_layout.h>
#include <axion/core/ax_text_node.h>
#include <axion/text/ax_text.h>

#include "anim/anim_internal.h"

/* A window root and its viewport. */
typedef struct frame_root {
    ax_node node;
    float width, height;
    float scale;          /* pixels per logical unit, for rounding layout */
    bool changed;         /* content changed since it was last recorded */
    bool animating;       /* something under it animated at the last ax_world_animate */
    uint64_t changed_seq; /* newest transaction applied when it changed */
} frame_root;

/* What was last recorded for a slot. */
typedef struct rec_entry {
    uint32_t generation; /* the node it describes; 0: nothing */
    uint32_t layer;      /* slot of the layer whose space the items are in (AX_NODE_NONE: none) */
    ax_vec2 origin;      /* the node's top-left in that space */
    float width, height;
    bool stale;             /* marked PAINT since (LAYOUT shows as a new size or origin) */
    bool incomplete;        /* some glyph runs waited for their glyphs (decision 100) */
    float raster_scale;     /* the raster scale its glyph runs were recorded at */
    bool promoted;          /* the node records a layer (until demoted, decision 94) */
    uint64_t neutral_since; /* animator time since its layer has been neutral; 0: not neutral */
    uint8_t *items;
    uint32_t size, capacity;
} rec_entry;

/* The recording walk's stack. */
typedef struct rec_frame {
    uint32_t slot;
    uint32_t layer; /* the layer the node's items go to */
    ax_vec2 origin; /* the node's top-left in that layer's space */
    uint32_t next;  /* next child to visit; AX_NODE_NONE when done */
    bool entered;
    bool pushed;           /* a layer was pushed for the node */
    bool clipped;          /* a clip layer was pushed for its children (overflow hidden) */
    bool in_present;       /* its frame comes from a presentation relayout (the resize preset) */
    bool children_present; /* its children's frames do */
} rec_frame;

struct frame_state {
    frame_root *roots;
    uint32_t root_count, root_capacity;
    rec_entry *rec; /* by slot */
    uint32_t rec_capacity;
    rec_frame *stack;
    uint32_t stack_capacity;
    ax_node_layout *pbox; /* presentation relayouts (the resize preset), by row */
    uint32_t pbox_capacity;
    ax_frame_counters counters;
    /* Text (decisions 100 and 101). */
    uint64_t epoch;        /* the newest snapshot's resource epoch */
    float record_scale;    /* the window's scale, while a root records */
    uint32_t glyph_budget; /* glyphs rasterized at once this frame, left */
    uint32_t glyph_budget_set; /* ax_world_set_glyph_budget (0 in a fresh state: automatic) */
    bool glyph_budget_custom;
    ax_glyph_key *keys;    /* scratch for one run's glyph keys */
    ax_ir_glyph *glyphs;   /* scratch for one run's records */
    uint32_t scratch_capacity;
};

static world_impl *scene_world(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT_MSG(w != NULL, "frame: invalid world");
    if (w != NULL) {
        AX_ASSERT_MSG(ax__world_on_scene_thread(w), "frame: not the world's scene thread");
    }
    return w;
}

static frame_state *state(world_impl *w)
{
    if (w->frame == NULL) {
        w->frame = (frame_state *)ax_mem_alloc(&w->allocator, sizeof(frame_state),
                                               AX_ALIGNOF(frame_state));
        if (w->frame != NULL) {
            memset(w->frame, 0, sizeof *w->frame);
        }
    }
    return w->frame;
}

void ax__frame_free(world_impl *w)
{
    frame_state *f = w->frame;
    uint32_t i;
    if (f == NULL) {
        return;
    }
    for (i = 0; i < f->rec_capacity; ++i) {
        ax_mem_free(&w->allocator, f->rec[i].items, f->rec[i].capacity);
    }
    ax_mem_free(&w->allocator, f->rec, sizeof(rec_entry) * f->rec_capacity);
    ax_mem_free(&w->allocator, f->roots, sizeof(frame_root) * f->root_capacity);
    ax_mem_free(&w->allocator, f->stack, sizeof(rec_frame) * f->stack_capacity);
    ax_mem_free(&w->allocator, f->pbox, sizeof(ax_node_layout) * f->pbox_capacity);
    ax_mem_free(&w->allocator, f->keys, sizeof(ax_glyph_key) * f->scratch_capacity);
    ax_mem_free(&w->allocator, f->glyphs, sizeof(ax_ir_glyph) * f->scratch_capacity);
    ax_mem_free(&w->allocator, f, sizeof *f);
    w->frame = NULL;
}

/* Grows an array of `elem`-sized elements to at least `need`, zeroing the new part. */
static bool grow(world_impl *w, void **array, uint32_t *capacity, uint32_t need, size_t elem)
{
    uint32_t cap = *capacity < 16u ? 16u : *capacity;
    void *fresh;
    if (need <= *capacity) {
        return true;
    }
    while (cap < need) {
        cap = cap > UINT32_MAX / 2u ? need : cap * 2u;
    }
    fresh = ax_mem_alloc(&w->allocator, elem * cap, AX_MAX_ALIGN);
    if (fresh == NULL) {
        return false;
    }
    memset((unsigned char *)fresh + elem * *capacity, 0, elem * (cap - *capacity));
    if (*array != NULL) {
        memcpy(fresh, *array, elem * *capacity);
        ax_mem_free(&w->allocator, *array, elem * *capacity);
    }
    *array = fresh;
    *capacity = cap;
    return true;
}

static frame_root *find_root(frame_state *f, ax_node node)
{
    uint32_t i;
    for (i = 0; f != NULL && i < f->root_count; ++i) {
        if (f->roots[i].node.h == node.h) {
            return &f->roots[i];
        }
    }
    return NULL;
}

/* The viewport root of a slot, if the slot holds one (by slot: handles are checked by the caller
 * through the store). */
static const frame_root *root_of_slot(const frame_state *f, const ax_node_store *s, uint32_t slot)
{
    uint32_t i;
    for (i = 0; f != NULL && i < f->root_count; ++i) {
        if (ax_handle_slot(f->roots[i].node.h) == slot &&
            ax_handle_generation(f->roots[i].node.h) == s->slots[slot].generation) {
            return &f->roots[i];
        }
    }
    return NULL;
}

/* ---- Window roots ---------------------------------------------------------------------------- */

ax_result ax_world_set_root_viewport(ax_world world, ax_node root, float width, float height,
                                     float scale)
{
    world_impl *w = scene_world(world);
    frame_state *f;
    frame_root *r;
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_set_root_viewport: invalid world");
    }
    if (!ax_node_store_is_live(&w->scene.store, root)) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_set_root_viewport: stale node");
    }
    if (!AX_HANDLE_IS_NULL(ax_node_store_parent(&w->scene.store, root))) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_world_set_root_viewport: the node has a parent");
    }
    if (!(width >= 0.0f) || !(height >= 0.0f) || !isfinite(width) || !isfinite(height) ||
        !(scale > 0.0f) || !isfinite(scale)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_world_set_root_viewport: bad size");
    }
    f = state(w);
    if (f == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_set_root_viewport: out of memory");
    }
    r = find_root(f, root);
    if (r == NULL) {
        if (!grow(w, (void **)&f->roots, &f->root_capacity, f->root_count + 1u,
                  sizeof(frame_root))) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_set_root_viewport: out of memory");
        }
        r = &f->roots[f->root_count++];
        memset(r, 0, sizeof *r);
        r->node = root;
        r->width = -1.0f; /* differs from any size: laid out below */
        r->changed = true;
        r->changed_seq = ax_atomic_load_u64(&w->applied_seq, AX_MO_RELAXED);
    }
    if (r->width != width || r->height != height || r->scale != scale) {
        if (r->width >= 0.0f) {
            ax__anim_viewport_changed(w); /* a window's resize never animates frames */
        }
        r->width = width;
        r->height = height;
        r->scale = scale;
        ax_scene_mark_layout(&w->scene, root);
    }
    return AX_OK;
}

void ax_world_remove_root(ax_world world, ax_node root)
{
    world_impl *w = scene_world(world);
    frame_state *f = w != NULL ? w->frame : NULL;
    frame_root *r = f != NULL ? find_root(f, root) : NULL;
    if (r != NULL) {
        *r = f->roots[--f->root_count];
        if (ax_node_store_is_live(&w->scene.store, root)) {
            ax_scene_mark_layout(&w->scene, root); /* laid out by its own properties again */
        }
    }
}

/* ---- Positioning: the layout engine (decision 90) -----------------------------------------------
 */

static bool viewport_of(void *user, ax_node root, float *width, float *height, float *scale)
{
    world_impl *w = (world_impl *)user;
    const frame_root *r = find_root(w->frame, root);
    if (r == NULL) {
        return false;
    }
    *width = r->width;
    *height = r->height;
    *scale = r->scale;
    return true;
}

static void frame_changed(void *user, uint32_t slot)
{
    world_impl *w = (world_impl *)user;
    ax__mirror_touch_slot(w, slot);
    ax__anim_on_frame_changed(w, slot);
    w->frame->counters.frames_changed++;
}

/* ---- Animations (decision 94) ---------------------------------------------------------------- */

static void mark_root_animating(world_impl *w, uint32_t root_slot)
{
    frame_root *r = (frame_root *)root_of_slot(w->frame, &w->scene.store, root_slot);
    if (r != NULL) {
        r->animating = true;
    }
}

uint32_t ax_world_animate(ax_world world, uint64_t frame_time_ns)
{
    world_impl *w = scene_world(world);
    uint32_t i;
    if (w == NULL || state(w) == NULL) {
        return 0;
    }
    for (i = 0; i < w->frame->root_count; ++i) {
        w->frame->roots[i].animating = false;
    }
    return ax__anim_step(w, frame_time_ns, mark_root_animating);
}

bool ax_world_root_animating(ax_world world, ax_node root)
{
    world_impl *w = scene_world(world);
    const frame_root *r = w != NULL ? find_root(w->frame, root) : NULL;
    return r != NULL && r->animating;
}

void ax_world_get_anim_counters(ax_world world, ax_anim_counters *out, bool reset)
{
    world_impl *w = scene_world(world);
    AX_ASSERT(out != NULL);
    memset(out, 0, sizeof *out);
    if (w != NULL) {
        ax__anim_get_counters(w, out, reset);
    }
}

uint32_t ax_world_position(ax_world world)
{
    world_impl *w = scene_world(world);
    ax_layout_pass_desc desc;
    if (w == NULL || state(w) == NULL) {
        return 0;
    }
    ax__text_prepare(w); /* the style pass and the text leaves' paragraphs (decision 99) */
    memset(&desc, 0, sizeof desc);
    desc.jobs = w->jobs;
    desc.viewport = viewport_of;
    desc.viewport_user = w;
    desc.frame_changed = frame_changed;
    desc.frame_changed_user = w;
    return ax_layout_run(&w->scene, &desc);
}

/* ---- Collecting what changed ----------------------------------------------------------------- */

void ax_world_record_collect(ax_world world)
{
    world_impl *w = scene_world(world);
    frame_state *f;
    ax_node_store *s;
    uint64_t seq;
    uint32_t row;
    if (w == NULL || (f = state(w)) == NULL) {
        return;
    }
    s = &w->scene.store;
    seq = ax_atomic_load_u64(&w->applied_seq, AX_MO_RELAXED);
    if (ax_atomic_load_u32(&w->glyphs_arrived, AX_MO_ACQUIRE) != 0) {
        /* Background glyphs are ready (decision 100): nodes whose runs waited record again. */
        uint32_t slot;
        ax_atomic_store_u32(&w->glyphs_arrived, 0, AX_MO_RELAXED);
        for (slot = 0; slot < f->rec_capacity && slot < s->slot_count; ++slot) {
            if (f->rec[slot].incomplete && s->slots[slot].row < AX_NODE_SLOT_RETIRED &&
                s->slots[slot].generation == f->rec[slot].generation) {
                s->dirty[s->slots[slot].row] |= AX_DIRTY_PAINT;
            }
        }
    }
    for (row = 0; row < s->row_count; ++row) {
        uint32_t flags = s->dirty[row];
        uint32_t slot;
        uint32_t top;
        frame_root *r;
        if (flags == 0) {
            continue;
        }
        slot = s->slot_of_row[row];
        /* PAINT: the items change. LAYOUT changes them only through the frame's size or the
         * node's origin in its layer, which recording compares with what it cached. */
        if ((flags & AX_DIRTY_PAINT) != 0 && slot < f->rec_capacity) {
            f->rec[slot].stale = true;
        }
        /* The window root above it, if any, has new content. */
        top = slot;
        while (s->links[s->slots[top].row].parent != AX_NODE_NONE) {
            top = s->links[s->slots[top].row].parent;
        }
        r = (frame_root *)root_of_slot(f, s, top);
        if (r != NULL) {
            r->changed = true;
            r->changed_seq = seq;
        }
    }
}

bool ax_world_root_changed(ax_world world, ax_node root, uint64_t *out_seq)
{
    world_impl *w = scene_world(world);
    frame_root *r = w != NULL ? find_root(w->frame, root) : NULL;
    if (out_seq != NULL) {
        *out_seq = r != NULL ? r->changed_seq : 0;
    }
    return r != NULL && r->changed;
}

/* ---- Recording ------------------------------------------------------------------------------- */

static bool is_identity(const ax_affine2 *m)
{
    return m->a == 1.0f && m->b == 0.0f && m->c == 0.0f && m->d == 1.0f && m->tx == 0.0f &&
           m->ty == 0.0f;
}

static bool append_item(world_impl *w, rec_entry *e, const void *record, uint32_t size)
{
    if (e->size + size > e->capacity) {
        uint32_t cap = e->capacity < 64u ? 64u : e->capacity * 2u;
        uint8_t *fresh;
        while (cap < e->size + size) {
            cap *= 2u;
        }
        fresh = (uint8_t *)ax_mem_alloc(&w->allocator, cap, 8u);
        if (fresh == NULL) {
            return false;
        }
        if (e->items != NULL) {
            memcpy(fresh, e->items, e->size);
            ax_mem_free(&w->allocator, e->items, e->capacity);
        }
        e->items = fresh;
        e->capacity = cap;
    }
    memcpy(e->items + e->size, record, size);
    e->size += size;
    return true;
}

/* Makes the node's items from its properties, in its layer's space at `origin`. */
/* What a node shows this frame (decision 94): its targets, or the animator's presentation values,
 * the frame being the target plus the frame channel's offset. */
typedef struct pres {
    ax_rect frame;
    ax_node_paint paint;
    ax_node_composite composite;
    bool moving; /* its frame animates: recorded as a layer, so moving it records no items */
} pres;

static void presentation(world_impl *w, uint32_t row, pres *out)
{
    const ax_node_store *s = &w->scene.store;
    const axa_node *an = ax__anim_node(w, row);
    out->frame = s->layout[row].frame;
    out->paint = s->paint[row];
    out->composite = s->composite[row];
    out->moving = false;
    if (an != NULL) {
        out->paint.background = an->background;
        memcpy(out->paint.corner_radius, an->radii, sizeof an->radii);
        out->composite.opacity = an->opacity;
        out->composite.transform = an->transform;
        if (an->active & (1u << AXA_FRAME)) {
            const float *o = an->track[AXA_FRAME].off;
            out->frame.x += o[0];
            out->frame.y += o[1];
            out->frame.w = out->frame.w + o[2] > 0.0f ? out->frame.w + o[2] : 0.0f;
            out->frame.h = out->frame.h + o[3] > 0.0f ? out->frame.h + o[3] : 0.0f;
            out->moving = true;
        }
    }
}

ax_result ax_world_presentation(ax_world world, ax_node node, ax_prop prop, ax_value *out)
{
    world_impl *w = scene_world(world);
    uint32_t row;
    pres pr;
    memset(out, 0, sizeof *out);
    if (w == NULL || (row = ax_node_store_row(&w->scene.store, node)) == AX_NODE_NONE) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_presentation: stale node");
    }
    presentation(w, row, &pr);
    switch (prop) {
    case AX_PROP_BACKGROUND_COLOR:
        out->color = pr.paint.background;
        return AX_OK;
    case AX_PROP_CORNER_RADIUS_TOP_LEFT:
    case AX_PROP_CORNER_RADIUS_TOP_RIGHT:
    case AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT:
    case AX_PROP_CORNER_RADIUS_BOTTOM_LEFT:
        out->f32 = pr.paint.corner_radius[prop - AX_PROP_CORNER_RADIUS_TOP_LEFT];
        return AX_OK;
    case AX_PROP_OPACITY:
        out->f32 = pr.composite.opacity;
        return AX_OK;
    case AX_PROP_TRANSFORM:
        out->transform2d = pr.composite.transform;
        return AX_OK;
    default:
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_world_presentation: property 0x%x does not animate",
                            (unsigned)prop);
    }
}

ax_result ax_world_presentation_frame(ax_world world, ax_node node, ax_rect *out)
{
    world_impl *w = scene_world(world);
    uint32_t row;
    pres pr;
    memset(out, 0, sizeof *out);
    if (w == NULL || (row = ax_node_store_row(&w->scene.store, node)) == AX_NODE_NONE) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_presentation_frame: stale node");
    }
    presentation(w, row, &pr);
    *out = pr.frame;
    return AX_OK;
}

/* Floor division by 4 of a quarter-pixel count (glyph x on the raster grid). */
static int32_t floor_div4(int64_t n)
{
    return (int32_t)(n >= 0 ? n / 4 : -((-n + 3) / 4));
}

static int64_t round_to_i64(double v)
{
    return (int64_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

/* Appends one glyph run (glyphs [g0, g1) of a laid-out paragraph, of one font and size) at
 * `origin` (the content box's top-left in the layer's space), in `color`. A run whose glyphs are
 * not all ready is left out whole (decision 100) and the entry is marked incomplete. */
static bool record_run(world_impl *w, rec_entry *e, ax_text_system *ts, const ax_text_layout *l,
                       uint32_t g0, uint32_t g1, ax_vec2 origin, ax_color color, float scale)
{
    frame_state *f = w->frame;
    const ax_text_glyph *first = &l->glyphs[g0];
    float extra = 1.0f;
    const float pixel_size = first->size * scale;
    const ax_font_instance instance = ax_text_font_instance(ts, first->font, pixel_size, &extra);
    const float rs = scale / extra; /* raster pixels per layer unit for this run */
    const bool subpixel = pixel_size <= AX_TEXT_SUBPIXEL_MAX_SIZE;
    const uint32_t n = g1 - g0;
    ax_ir_glyph_run_item head;
    uint32_t i, missing;
    if (instance == 0 || n == 0) {
        return true;
    }
    if (n > f->scratch_capacity) {
        uint32_t cap = f->scratch_capacity < 256u ? 256u : f->scratch_capacity;
        ax_glyph_key *k;
        ax_ir_glyph *g;
        while (cap < n) {
            cap *= 2u;
        }
        k = (ax_glyph_key *)ax_mem_alloc(&w->allocator, sizeof *k * cap, AX_ALIGNOF(ax_glyph_key));
        g = (ax_ir_glyph *)ax_mem_alloc(&w->allocator, sizeof *g * cap, AX_ALIGNOF(ax_ir_glyph));
        if (k == NULL || g == NULL) {
            ax_mem_free(&w->allocator, k, sizeof *k * cap);
            ax_mem_free(&w->allocator, g, sizeof *g * cap);
            return false;
        }
        ax_mem_free(&w->allocator, f->keys, sizeof(ax_glyph_key) * f->scratch_capacity);
        ax_mem_free(&w->allocator, f->glyphs, sizeof(ax_ir_glyph) * f->scratch_capacity);
        f->keys = k;
        f->glyphs = g;
        f->scratch_capacity = cap;
    }
    for (i = 0; i < n; ++i) {
        const ax_text_glyph *tg = &l->glyphs[g0 + i];
        /* On the raster grid: the baseline on a whole pixel, x to a quarter pixel (subpixel
         * positions) or a whole one above AX_TEXT_SUBPIXEL_MAX_SIZE. In double, so positions far
         * from the origin keep their quarters. */
        const double x = ((double)origin.x + (double)tg->x) * (double)rs;
        const double y = ((double)origin.y + (double)tg->y) * (double)rs;
        ax_ir_glyph *g = &f->glyphs[i];
        g->id = tg->id;
        g->y = (int32_t)round_to_i64(y);
        if (subpixel) {
            const int64_t q = round_to_i64(x * (double)AX_TEXT_SUBPIXEL_BINS);
            g->x = floor_div4(q);
            g->bin = (uint32_t)(q - (int64_t)g->x * 4);
        } else {
            g->x = (int32_t)round_to_i64(x);
            g->bin = 0;
        }
        f->keys[i].instance = instance;
        f->keys[i].glyph = g->id;
        f->keys[i].bin = g->bin;
    }
    missing = ax_text_glyphs_ensure(ts, f->keys, n, f->epoch, w->jobs, &f->glyph_budget);
    if (missing > 0) {
        e->incomplete = true;
        f->counters.glyph_runs_deferred++;
        return true; /* the whole run later, never part of it */
    }
    memset(&head, 0, sizeof head);
    head.header.type = AX_IR_ITEM_GLYPH_RUN;
    head.header.size = (uint32_t)(sizeof head + sizeof(ax_ir_glyph) * n);
    head.color = color;
    head.font_instance = instance;
    head.glyph_count = n;
    head.raster_scale = rs;
    f->counters.items_recorded++;
    f->counters.glyph_runs++;
    return append_item(w, e, &head, (uint32_t)sizeof head) &&
           append_item(w, e, f->glyphs, (uint32_t)(sizeof(ax_ir_glyph) * n));
}

/* The glyph runs of a text leaf: its paragraph laid out at its content box's width, one run per
 * line and font. */
static bool record_text(world_impl *w, rec_entry *e, uint32_t row, const pres *pr, ax_vec2 origin)
{
    const ax_node_store *s = &w->scene.store;
    const ax_node_text *t = (const ax_node_text *)ax_node_store_peek_component(
        s, s->slot_of_row[row], w->scene.text_component);
    const ax_node_layout *box = &s->layout[row];
    ax_text_system *ts = w->text;
    const ax_text_layout *l;
    ax_vec2 content;
    float cw;
    ax_color color;
    uint32_t i;
    bool ok = true;
    if (t == NULL || t->paragraph == NULL || ts == NULL) {
        return true;
    }
    color = t->computed.color;
    if (!(color.a > 0.0f)) {
        return true;
    }
    content.x = origin.x + box->border[AX_EDGE_LEFT] + box->padding[AX_EDGE_LEFT];
    content.y = origin.y + box->border[AX_EDGE_TOP] + box->padding[AX_EDGE_TOP];
    cw = pr->frame.w - box->border[AX_EDGE_LEFT] - box->padding[AX_EDGE_LEFT] -
         box->border[AX_EDGE_RIGHT] - box->padding[AX_EDGE_RIGHT];
    if (cw < 0.0f) {
        cw = 0.0f;
    }
    l = ax_text_paragraph_layout(t->paragraph, cw, cw);
    if (l == NULL) {
        return false;
    }
    for (i = 0; i < l->line_count && ok; ++i) {
        const ax_text_line *line = &l->lines[i];
        uint32_t g = line->first_glyph, end = line->first_glyph + line->glyph_count;
        while (g < end && ok) {
            uint32_t k = g + 1u;
            while (k < end && l->glyphs[k].font == l->glyphs[g].font &&
                   l->glyphs[k].size == l->glyphs[g].size) {
                k++;
            }
            ok = record_run(w, e, ts, l, g, k, content, color, w->frame->record_scale);
            g = k;
        }
    }
    ax_text_layout_release(l);
    return ok;
}

static bool record_items(world_impl *w, rec_entry *e, uint32_t row, const pres *pr, ax_vec2 origin)
{
    const ax_node_paint *p = &pr->paint;
    const ax_rect frame = pr->frame;
    const ax_rect rect = ax_rect_make(origin.x, origin.y, frame.w, frame.h);
    e->size = 0;
    e->incomplete = false;
    e->raster_scale = w->frame->record_scale;
    w->frame->counters.nodes_recorded++;
    if (frame.w <= 0.0f || frame.h <= 0.0f) {
        return true; /* nothing visible */
    }
    if (p->background.a <= 0.0f) {
        return record_text(w, e, row, pr, origin);
    }
    if (p->corner_radius[0] > 0.0f || p->corner_radius[1] > 0.0f || p->corner_radius[2] > 0.0f ||
        p->corner_radius[3] > 0.0f) {
        ax_ir_rounded_rect_item it;
        memset(&it, 0, sizeof it);
        it.header.type = AX_IR_ITEM_ROUNDED_RECT;
        it.header.size = (uint32_t)sizeof it;
        it.rect = rect;
        memcpy(it.radii, p->corner_radius, sizeof it.radii);
        it.color = p->background;
        w->frame->counters.items_recorded++;
        if (!append_item(w, e, &it, (uint32_t)sizeof it)) {
            return false;
        }
    } else {
        ax_ir_rect_item it;
        memset(&it, 0, sizeof it);
        it.header.type = AX_IR_ITEM_RECT;
        it.header.size = (uint32_t)sizeof it;
        it.rect = rect;
        it.color = p->background;
        w->frame->counters.items_recorded++;
        if (!append_item(w, e, &it, (uint32_t)sizeof it)) {
            return false;
        }
    }
    return record_text(w, e, row, pr, origin);
}

/* Emits the node's items: from the cache when still valid, recorded again otherwise. */
static bool emit_items(world_impl *w, ax_ir_builder *b, uint32_t slot, const pres *pr,
                       uint32_t layer, ax_vec2 origin)
{
    rec_entry *e = &w->frame->rec[slot];
    const ax_node_store *s = &w->scene.store;
    const ax_rect frame = pr->frame;
    uint32_t off = 0;
    bool reuse = e->generation == s->slots[slot].generation && !e->stale && e->layer == layer &&
                 e->origin.x == origin.x && e->origin.y == origin.y && e->width == frame.w &&
                 e->height == frame.h && e->raster_scale == w->frame->record_scale;
    if (!reuse) {
        if (!record_items(w, e, s->slots[slot].row, pr, origin)) {
            return false;
        }
        e->generation = s->slots[slot].generation;
        e->layer = layer;
        e->origin = origin;
        e->width = frame.w;
        e->height = frame.h;
        e->stale = false;
    }
    while (off < e->size) {
        const ax_ir_item_header *h = (const ax_ir_item_header *)(const void *)(e->items + off);
        ax_ir_item(b, h->type, h->flags, e->items + off + sizeof *h, h->size - (uint32_t)sizeof *h);
        if (reuse) {
            w->frame->counters.items_reused++;
        }
        off += h->size;
    }
    return true;
}

/* The clip layer of an overflow-hidden node at `origin` in its layer: its frame, rounded by its
 * corner radii, around its children (decision 93). */
static ax_ir_layer_desc clip_desc(const pres *pr, ax_vec2 origin)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const float *r = pr->paint.corner_radius;
    d.transform = ax_affine2_translate(origin.x, origin.y);
    d.clip.kind = (r[0] > 0.0f || r[1] > 0.0f || r[2] > 0.0f || r[3] > 0.0f)
                      ? AX_IR_CLIP_ROUNDED_RECT
                      : AX_IR_CLIP_RECT;
    d.clip.path = AX_IR_NO_INDEX;
    d.clip.rect = ax_rect_make(0.0f, 0.0f, pr->frame.w, pr->frame.h);
    memcpy(d.clip.radii, r, sizeof d.clip.radii);
    return d;
}

/* The layer record of a promoted node at `origin` in its parent layer: translate there, then the
 * node's transform about its frame's center (decision 63). */
static ax_ir_layer_desc layer_desc(const pres *pr, ax_vec2 origin)
{
    const ax_node_composite *c = &pr->composite;
    const ax_rect frame = pr->frame;
    const float cx = frame.w * 0.5f, cy = frame.h * 0.5f;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    ax_affine2 m = ax_affine2_translate(origin.x, origin.y);
    if (!is_identity(&c->transform)) {
        m = ax_affine2_mul(m, ax_affine2_translate(cx, cy));
        m = ax_affine2_mul(m, c->transform);
        m = ax_affine2_mul(m, ax_affine2_translate(-cx, -cy));
    }
    d.transform = m;
    d.opacity = c->opacity;
    return d;
}

ax_result ax_world_record_root(ax_world world, ax_node root, ax_ir_builder *b)
{
    world_impl *w = scene_world(world);
    frame_state *f;
    frame_root *r;
    ax_node_store *s;
    uint32_t depth = 0;
    if (w == NULL || (f = w->frame) == NULL || (r = find_root(f, root)) == NULL ||
        !ax_node_store_is_live(&w->scene.store, root)) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_record_root: not a live window root");
    }
    s = &w->scene.store;
    if (!grow(w, (void **)&f->rec, &f->rec_capacity, s->slot_count, sizeof(rec_entry))) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_record_root: out of memory");
    }
    /* Text (decisions 100 and 101): the snapshot's epoch stamps the glyphs it uses, and its
     * glyph source lets the renderer read them. */
    f->epoch++;
    f->record_scale = r->scale;
    f->glyph_budget = f->glyph_budget_custom ? f->glyph_budget_set
                      : w->jobs != NULL && !ax_jobs_is_inline(w->jobs) ? 1024u
                                                                        : 256u;
    if (w->text != NULL) {
        ax_ir_builder_set_resources(b, &w->glyph_source, f->epoch);
    }
    if (!grow(w, (void **)&f->stack, &f->stack_capacity, 1u, sizeof(rec_frame))) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_record_root: out of memory");
    }
    f->stack[0].slot = ax_handle_slot(root.h);
    f->stack[0].layer = AX_NODE_NONE; /* the snapshot's root layer */
    f->stack[0].origin = ax_vec2_make(0.0f, 0.0f);
    f->stack[0].entered = false;
    f->stack[0].pushed = false;
    f->stack[0].clipped = false;
    f->stack[0].in_present = false;
    f->stack[0].children_present = false;
    depth = 1;
    while (depth > 0) {
        rec_frame *top = &f->stack[depth - 1u];
        uint32_t row = s->slots[top->slot].row;
        if (!top->entered) {
            rec_entry *e = &f->rec[top->slot];
            pres pr;
            const ax_node_composite *c = &pr.composite;
            top->entered = true;
            if (s->style[row].display == AX_DISPLAY_NONE) {
                depth--; /* takes no space and draws nothing, with its subtree */
                continue;
            }
            presentation(w, row, &pr);
            {
                /* The frame its animation offsets: the target, or, under a resizing ancestor,
                 * the presentation relayout's (the resize preset, decision 94). */
                const ax_rect target = s->layout[row].frame;
                ax_rect base = target;
                if (top->in_present) {
                    const ax_node_layout *pb = &f->pbox[row];
                    base = ax_rect_make(pb->pos[AX_EDGE_LEFT], pb->pos[AX_EDGE_TOP], pb->measured_w,
                                        pb->measured_h);
                    pr.frame.x += base.x - target.x;
                    pr.frame.y += base.y - target.y;
                    pr.frame.w = base.w + (pr.frame.w - target.w);
                    pr.frame.h = base.h + (pr.frame.h - target.h);
                    pr.frame.w = pr.frame.w > 0.0f ? pr.frame.w : 0.0f;
                    pr.frame.h = pr.frame.h > 0.0f ? pr.frame.h : 0.0f;
                }
                /* The parent placed this node at its target frame's origin. */
                top->origin.x += pr.frame.x - target.x;
                top->origin.y += pr.frame.y - target.y;
                top->children_present = top->in_present;
                if ((pr.frame.w != base.w || pr.frame.h != base.h) &&
                    s->links[row].first_child != AX_NODE_NONE &&
                    grow(w, (void **)&f->pbox, &f->pbox_capacity, s->row_count,
                         sizeof(ax_node_layout))) {
                    /* Resizing: its children laid out again at the interpolated size. */
                    ax_layout_present(&w->scene, row, pr.frame.w, pr.frame.h, f->pbox);
                    f->counters.present_relayouts++;
                    top->children_present = true;
                }
            }
            {
                /* Demotion with hysteresis: a layer whose transform, opacity and frame have been
                 * neutral for a second goes back to its parent's space (one re-recording). */
                const uint64_t now = ax__anim_time(w);
                const bool neutral = is_identity(&c->transform) && c->opacity == 1.0f && !pr.moving;
                if (e->promoted && neutral && now != 0) {
                    if (e->neutral_since == 0) {
                        e->neutral_since = now;
                    } else if (now - e->neutral_since >= 1000000000ull) {
                        e->promoted = false;
                        e->neutral_since = 0;
                        f->counters.layers_demoted++;
                    }
                } else if (!neutral) {
                    e->neutral_since = 0;
                }
            }
            if (e->generation != s->slots[top->slot].generation) {
                e->promoted = false; /* a new node in a reused slot */
                e->generation = 0;
            }
            if (!e->promoted && (!is_identity(&c->transform) || c->opacity != 1.0f || pr.moving)) {
                e->promoted = true;
                f->counters.layers_promoted++;
            }
            if (e->promoted) {
                ax_ir_layer_desc d = layer_desc(&pr, top->origin);
                ax_ir_push_layer(b, &d);
                f->counters.layers++;
                top->pushed = true;
                top->layer = top->slot;
                top->origin = ax_vec2_make(0.0f, 0.0f);
            }
            if (!emit_items(w, b, top->slot, &pr, top->layer, top->origin)) {
                return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_record_root: out of memory");
            }
            if (s->style[row].overflow == AX_OVERFLOW_HIDDEN &&
                s->links[row].first_child != AX_NODE_NONE) {
                /* The children only: the node's own items keep their edges unclipped. */
                ax_ir_layer_desc d = clip_desc(&pr, top->origin);
                ax_ir_push_layer(b, &d);
                f->counters.layers++;
                f->counters.clips++;
                top->clipped = true;
                top->layer = top->slot;
                top->origin = ax_vec2_make(0.0f, 0.0f);
            }
            top->next = s->links[row].first_child;
            continue;
        }
        if (top->next != AX_NODE_NONE) {
            uint32_t child = top->next;
            uint32_t crow = s->slots[child].row;
            rec_frame *next;
            top->next = s->links[crow].next_sibling;
            if (!grow(w, (void **)&f->stack, &f->stack_capacity, depth + 1u, sizeof(rec_frame))) {
                return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_record_root: out of memory");
            }
            top = &f->stack[depth - 1u]; /* the stack may have moved */
            next = &f->stack[depth++];
            next->slot = child;
            next->layer = top->layer;
            next->origin = ax_vec2_add(
                top->origin, ax_vec2_make(s->layout[crow].frame.x, s->layout[crow].frame.y));
            next->entered = false;
            next->pushed = false;
            next->clipped = false;
            next->in_present = top->children_present;
            next->children_present = false;
            continue;
        }
        if (top->clipped) {
            ax_ir_pop_layer(b);
        }
        if (top->pushed) {
            ax_ir_pop_layer(b);
        }
        depth--;
    }
    r->changed = false;
    f->counters.snapshots++;
    return AX_OK;
}

void ax_world_get_frame_counters(ax_world world, ax_frame_counters *out, bool reset)
{
    world_impl *w = scene_world(world);
    AX_ASSERT(out != NULL);
    memset(out, 0, sizeof *out);
    if (w == NULL || w->frame == NULL) {
        return;
    }
    *out = w->frame->counters;
    if (reset) {
        memset(&w->frame->counters, 0, sizeof w->frame->counters);
    }
}

uint32_t ax_world_retire_glyphs(ax_world world, uint64_t oldest_live_epoch)
{
    world_impl *w = scene_world(world);
    if (w == NULL || w->text == NULL) {
        return 0;
    }
    return ax_text_glyphs_retire(w->text, oldest_live_epoch);
}

void ax_world_set_glyph_budget(ax_world world, uint32_t glyphs)
{
    world_impl *w = scene_world(world);
    frame_state *f = w != NULL ? state(w) : NULL;
    if (f != NULL) {
        f->glyph_budget_custom = glyphs != AX_GLYPH_BUDGET_AUTO;
        f->glyph_budget_set = glyphs;
    }
}
