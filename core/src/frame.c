/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The frame stages after applying transactions (ax_frame.h): window roots, minimal positioning,
 * and recording the Render IR from the node store with a per-node cache of encoded items.
 */
#include <math.h>
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_frame.h>

/* A window root and its viewport. */
typedef struct frame_root {
    ax_node node;
    float width, height;
    bool changed;         /* content changed since it was last recorded */
    uint64_t changed_seq; /* newest transaction applied when it changed */
} frame_root;

/* What was last recorded for a slot. */
typedef struct rec_entry {
    uint32_t generation; /* the node it describes; 0: nothing */
    uint32_t layer;      /* slot of the layer whose space the items are in (AX_NODE_NONE: none) */
    ax_vec2 origin;      /* the node's top-left in that space */
    float width, height;
    bool stale;    /* marked PAINT since (LAYOUT shows as a new size or origin) */
    bool promoted; /* the node records a layer (sticky) */
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
    bool pushed; /* a layer was pushed for the node */
} rec_frame;

struct frame_state {
    frame_root *roots;
    uint32_t root_count, root_capacity;
    rec_entry *rec; /* by slot */
    uint32_t rec_capacity;
    rec_frame *stack;
    uint32_t stack_capacity;
    ax_frame_counters counters;
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

ax_result ax_world_set_root_viewport(ax_world world, ax_node root, float width, float height)
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
    if (!(width >= 0.0f) || !(height >= 0.0f) || !isfinite(width) || !isfinite(height)) {
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
    if (r->width != width || r->height != height) {
        r->width = width;
        r->height = height;
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

/* ---- Positioning ----------------------------------------------------------------------------- */

static float points_or_zero(ax_dimension d)
{
    return d.unit == AX_UNIT_POINTS ? d.value : 0.0f;
}

static void place(world_impl *w, uint32_t row)
{
    ax_node_store *s = &w->scene.store;
    uint32_t slot = s->slot_of_row[row];
    const frame_root *vr = root_of_slot(w->frame, s, slot);
    ax_rect frame;
    if (vr != NULL) {
        frame = ax_rect_make(0.0f, 0.0f, vr->width, vr->height);
    } else {
        const ax_node_style *st = &s->style[row];
        frame = ax_rect_make(points_or_zero(st->left), points_or_zero(st->top),
                             points_or_zero(st->width), points_or_zero(st->height));
    }
    if (memcmp(&s->layout[row].frame, &frame, sizeof frame) != 0) {
        s->layout[row].frame = frame;
        ax__mirror_touch_slot(w, slot);
        if (w->frame != NULL) {
            w->frame->counters.frames_changed++;
        }
    }
}

/* Positions from `root_slot` down, descending through dirty nodes (or every node). */
static void position_tree(world_impl *w, uint32_t root_slot, bool everything)
{
    ax_node_store *s = &w->scene.store;
    uint32_t cur = root_slot;
    for (;;) {
        uint32_t row = s->slots[cur].row;
        const ax_node_links *l = &s->links[row];
        place(w, row);
        if (l->first_child != AX_NODE_NONE &&
            (everything || (s->dirty[row] & (AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_CHILD)) != 0)) {
            cur = l->first_child;
            continue;
        }
        while (cur != root_slot && s->links[s->slots[cur].row].next_sibling == AX_NODE_NONE) {
            cur = s->links[s->slots[cur].row].parent;
        }
        if (cur == root_slot) {
            return;
        }
        cur = s->links[s->slots[cur].row].next_sibling;
    }
}

uint32_t ax_world_position(ax_world world)
{
    world_impl *w = scene_world(world);
    ax_node_store *s;
    const ax_dirty_set *d;
    uint64_t before;
    uint32_t i;
    if (w == NULL || state(w) == NULL) {
        return 0;
    }
    s = &w->scene.store;
    d = &w->scene.dirty;
    before = w->frame->counters.frames_changed;
    if (d->roots_overflowed) {
        for (i = 0; i < s->row_count; ++i) {
            if (s->links[i].parent == AX_NODE_NONE) {
                position_tree(w, s->slot_of_row[i], true);
            }
        }
    } else {
        for (i = 0; i < d->layout_root_count; ++i) {
            ax_node r = d->layout_roots[i];
            if (ax_node_store_is_live(s, r)) { /* destroyed since it was listed: skip */
                position_tree(w, ax_handle_slot(r.h), false);
            }
        }
    }
    return (uint32_t)(w->frame->counters.frames_changed - before);
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
static bool record_items(world_impl *w, rec_entry *e, uint32_t row, ax_vec2 origin)
{
    const ax_node_store *s = &w->scene.store;
    const ax_node_paint *p = &s->paint[row];
    const ax_rect frame = s->layout[row].frame;
    const ax_rect rect = ax_rect_make(origin.x, origin.y, frame.w, frame.h);
    e->size = 0;
    w->frame->counters.nodes_recorded++;
    if (p->background.a <= 0.0f || frame.w <= 0.0f || frame.h <= 0.0f) {
        return true; /* nothing visible */
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
        return append_item(w, e, &it, (uint32_t)sizeof it);
    } else {
        ax_ir_rect_item it;
        memset(&it, 0, sizeof it);
        it.header.type = AX_IR_ITEM_RECT;
        it.header.size = (uint32_t)sizeof it;
        it.rect = rect;
        it.color = p->background;
        w->frame->counters.items_recorded++;
        return append_item(w, e, &it, (uint32_t)sizeof it);
    }
}

/* Emits the node's items: from the cache when still valid, recorded again otherwise. */
static bool emit_items(world_impl *w, ax_ir_builder *b, uint32_t slot, uint32_t row, uint32_t layer,
                       ax_vec2 origin)
{
    rec_entry *e = &w->frame->rec[slot];
    const ax_node_store *s = &w->scene.store;
    const ax_rect frame = s->layout[row].frame;
    uint32_t off = 0;
    bool reuse = e->generation == s->slots[slot].generation && !e->stale && e->layer == layer &&
                 e->origin.x == origin.x && e->origin.y == origin.y && e->width == frame.w &&
                 e->height == frame.h;
    if (!reuse) {
        if (!record_items(w, e, row, origin)) {
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

/* The layer record of a promoted node at `origin` in its parent layer: translate there, then the
 * node's transform about its frame's center (decision 63). */
static ax_ir_layer_desc layer_desc(const ax_node_store *s, uint32_t row, ax_vec2 origin)
{
    const ax_node_composite *c = &s->composite[row];
    const ax_rect frame = s->layout[row].frame;
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
    if (!grow(w, (void **)&f->stack, &f->stack_capacity, 1u, sizeof(rec_frame))) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_record_root: out of memory");
    }
    f->stack[0].slot = ax_handle_slot(root.h);
    f->stack[0].layer = AX_NODE_NONE; /* the snapshot's root layer */
    f->stack[0].origin = ax_vec2_make(0.0f, 0.0f);
    f->stack[0].entered = false;
    f->stack[0].pushed = false;
    depth = 1;
    while (depth > 0) {
        rec_frame *top = &f->stack[depth - 1u];
        uint32_t row = s->slots[top->slot].row;
        if (!top->entered) {
            rec_entry *e = &f->rec[top->slot];
            const ax_node_composite *c = &s->composite[row];
            top->entered = true;
            if (e->generation != s->slots[top->slot].generation) {
                e->promoted = false; /* a new node in a reused slot */
                e->generation = 0;
            }
            if (!e->promoted && (!is_identity(&c->transform) || c->opacity != 1.0f)) {
                e->promoted = true;
                f->counters.layers_promoted++;
            }
            if (e->promoted) {
                ax_ir_layer_desc d = layer_desc(s, row, top->origin);
                ax_ir_push_layer(b, &d);
                f->counters.layers++;
                top->pushed = true;
                top->layer = top->slot;
                top->origin = ax_vec2_make(0.0f, 0.0f);
            }
            if (!emit_items(w, b, top->slot, row, top->layer, top->origin)) {
                return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_record_root: out of memory");
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
            continue;
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
