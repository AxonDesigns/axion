/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The accessibility tree (decision 108; ax_a11y.h). The world's change points queue the slots
 * they touch (ax__a11y_touch); each flush rebuilds those nodes' records, compares each record's
 * hash with the one last sent, and sends the nodes that differ to their root's sink. A record per
 * slot remembers what was sent: the node, its parent, its root and the hash, so a destroyed or
 * moved node's old parent is sent again with its new children.
 */
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_arena.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_a11y.h>
#include <axion/core/ax_text_node.h>
#include <axion/text/ax_text.h>

#define TOUCH_STRUCT AX__A11Y_STRUCT
#define TOUCH_TEXT   AX__A11Y_TEXT

#define NAME_FROM_CONTENT_MAX 1024u

typedef struct a11y_rec {
    uint64_t handle; /* the node the record describes (0: none) */
    uint64_t parent; /* its parent when it was last sent */
    uint64_t hash;   /* of what was last sent */
    uint32_t root;   /* index + 1 of the root it was sent to; 0: not in a tree */
    uint32_t queued; /* the epoch it is queued for */
    uint32_t qindex; /* its entry in the queue */
} a11y_rec;

typedef struct a11y_entry {
    uint32_t slot;
    uint32_t flags;
} a11y_entry;

typedef struct a11y_root {
    ax_node root;
    ax_a11y_sink sink;
    bool active;
    bool wants_full;
    uint64_t sent_focus; /* the focus of its last update (0: none sent) */
} a11y_root;

typedef struct a11y_state {
    a11y_rec *recs;
    uint32_t rec_capacity;
    a11y_entry *queue;
    uint32_t queue_count;
    uint32_t queue_capacity;
    bool overflowed; /* a slot could not be queued: rebuild every active tree */
    uint32_t epoch;
    a11y_root *roots;
    uint32_t root_count;
    uint32_t root_capacity;
    uint64_t focus;   /* the assistive technology's focus (a handle), or 0 */
    uint64_t handled; /* requests posted up to here are handled once this flush ends */
    /* One flush's output. */
    ax_arena arena; /* children, computed names, text runs' arrays */
    ax_a11y_node *out;
    uint32_t *out_root; /* index of each node's root */
    uint32_t out_count;
    uint32_t out_capacity;
    ax_a11y_node *grouped; /* one root's nodes, for its sink */
    uint32_t grouped_capacity;
    uint32_t *stack; /* subtree walks */
    uint32_t stack_capacity;
    ax_a11y_counters counters;
} a11y_state;

/* ---- Small helpers --------------------------------------------------------------------------- */

static bool grow(const ax_allocator *a, void **p, uint32_t *cap, uint32_t need, size_t elem,
                 size_t align)
{
    uint32_t c = *cap < 64u ? 64u : *cap;
    void *fresh;
    if (need <= *cap) {
        return true;
    }
    while (c < need) {
        c *= 2u;
    }
    fresh = ax_mem_alloc(a, elem * c, align);
    if (fresh == NULL) {
        return false;
    }
    if (*p != NULL) {
        memcpy(fresh, *p, elem * *cap);
        ax_mem_free(a, *p, elem * *cap);
    }
    memset((unsigned char *)fresh + elem * *cap, 0, elem * (c - *cap));
    *p = fresh;
    *cap = c;
    return true;
}

static uint32_t live_row(const ax_node_store *s, uint32_t slot)
{
    if (slot >= s->slot_count || s->slots[slot].row >= AX_NODE_SLOT_RETIRED) {
        return AX_NODE_NONE;
    }
    return s->slots[slot].row;
}

static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t fnv_u32(uint64_t h, uint32_t v)
{
    return fnv(h, &v, sizeof v);
}

static uint64_t fnv_str(uint64_t h, ax_str s)
{
    h = fnv_u32(h, (uint32_t)s.len);
    return s.len > 0 ? fnv(h, s.ptr, s.len) : h;
}

static ax_str shared_view(const ax_shared_str *s)
{
    return ax_shared_str_view(s);
}

const char *ax_role_name(uint32_t role)
{
    static const char *const NAMES[] = {
        "none",      "generic",     "window",   "group",     "label",      "heading",
        "paragraph", "button",      "link",     "image",     "checkbox",   "switch",
        "radio",     "slider",      "progress", "text_input", "list",      "list_item",
        "article",   "dialog",      "toolbar",  "tab_list",  "tab",        "tab_panel",
        "menu",      "menu_item",   "scroll_view", "separator", "text_run"};
    AX_STATIC_ASSERT(sizeof NAMES / sizeof NAMES[0] == AX_ROLE_LAST + 1, "a name per role");
    return role <= AX_ROLE_LAST ? NAMES[role] : "unknown";
}

/* ---- State ----------------------------------------------------------------------------------- */

static a11y_state *state(world_impl *w)
{
    if (w->a11y == NULL) {
        a11y_state *st =
            (a11y_state *)ax_mem_alloc(&w->allocator, sizeof *st, AX_ALIGNOF(a11y_state));
        if (st == NULL) {
            return NULL;
        }
        memset(st, 0, sizeof *st);
        st->epoch = 1;
        ax_arena_init(&st->arena, &w->allocator, 64u * 1024u);
        w->a11y = st;
    }
    return (a11y_state *)w->a11y;
}

void ax__a11y_free(world_impl *w)
{
    a11y_state *st = (a11y_state *)w->a11y;
    const ax_allocator *a = &w->allocator;
    if (st != NULL) {
        ax_mem_free(a, st->recs, sizeof(a11y_rec) * st->rec_capacity);
        ax_mem_free(a, st->queue, sizeof(a11y_entry) * st->queue_capacity);
        ax_mem_free(a, st->roots, sizeof(a11y_root) * st->root_capacity);
        ax_mem_free(a, st->out, sizeof(ax_a11y_node) * st->out_capacity);
        ax_mem_free(a, st->out_root, sizeof(uint32_t) * st->out_capacity);
        ax_mem_free(a, st->grouped, sizeof(ax_a11y_node) * st->grouped_capacity);
        ax_mem_free(a, st->stack, sizeof(uint32_t) * st->stack_capacity);
        ax_arena_deinit(&st->arena);
        ax_mem_free(a, st, sizeof *st);
        w->a11y = NULL;
    }
    ax_mutex_deinit(&w->a11y_lock);
}

void ax__a11y_init(world_impl *w)
{
    ax_mutex_init(&w->a11y_lock);
    ax_atomic_init_u32(&w->a11y_pending, 0);
    ax_atomic_init_u32(&w->a11y_dropped, 0);
    ax_atomic_init_u64(&w->a11y_posted, 0);
    ax_atomic_init_u64(&w->a11y_handled, 0);
    ax_atomic_init_u64(&w->a11y_flushed_seq, 0);
    ax_atomic_init_u32(&w->a11y_active, 0);
}

/* ---- Change points --------------------------------------------------------------------------- */

static void queue_slot(world_impl *w, a11y_state *st, uint32_t slot, uint32_t flags)
{
    a11y_rec *r;
    if (slot >= st->rec_capacity && !grow(&w->allocator, (void **)&st->recs, &st->rec_capacity,
                                          slot + 1u, sizeof(a11y_rec), AX_ALIGNOF(a11y_rec))) {
        st->overflowed = true;
        return;
    }
    r = &st->recs[slot];
    if (r->queued == st->epoch) {
        st->queue[r->qindex].flags |= flags;
        return;
    }
    if (st->queue_count == st->queue_capacity &&
        !grow(&w->allocator, (void **)&st->queue, &st->queue_capacity, st->queue_count + 1u,
              sizeof(a11y_entry), AX_ALIGNOF(a11y_entry))) {
        st->overflowed = true;
        return;
    }
    r->queued = st->epoch;
    r->qindex = st->queue_count;
    st->queue[st->queue_count].slot = slot;
    st->queue[st->queue_count].flags = flags;
    st->queue_count++;
    st->counters.touched++;
}

void ax__a11y_touch_slow(world_impl *w, uint32_t slot, uint32_t flags)
{
    a11y_state *st = (a11y_state *)w->a11y;
    if (st != NULL) {
        queue_slot(w, st, slot, flags);
    }
}

void ax__a11y_touch_prop(world_impl *w, uint32_t slot, ax_prop prop)
{
    const ax_prop_desc *d;
    uint32_t flags = 0;
    if (!w->a11y_enabled) {
        return;
    }
    d = ax_prop_builtin(prop);
    if (d != NULL && d->storage == AX_PROP_IN_TEXT) {
        flags |= TOUCH_TEXT;
    }
    if (prop == AX_PROP_DISPLAY || prop == AX_PROP_HIDDEN || prop == AX_PROP_TEXT) {
        flags |= TOUCH_STRUCT; /* in the tree or not; a leaf or not */
    }
    ax__a11y_touch_slow(w, slot, flags);
}

/* ---- The tree's rules ------------------------------------------------------------------------ */

static bool is_text_leaf(const ax_scene *scene, uint32_t slot)
{
    const ax_node_text *t = (const ax_node_text *)ax_node_store_peek_component(
        &scene->store, slot, scene->text_component);
    return t != NULL && (t->own & AX_TEXT_PROP_BIT(AX_PROP_TEXT)) != 0;
}

static const ax_node_semantics *semantics_of(const ax_scene *scene, uint32_t slot)
{
    return (const ax_node_semantics *)ax_node_store_peek_component(&scene->store, slot,
                                                                   scene->semantics_component);
}

static bool included(const ax_scene *scene, uint32_t slot, uint32_t row)
{
    const ax_node_semantics *m;
    if (scene->store.style[row].display == AX_DISPLAY_NONE) {
        return false;
    }
    m = semantics_of(scene, slot);
    return m == NULL || !m->hidden;
}

static uint32_t resolved_role(const ax_scene *scene, uint32_t slot, uint32_t row)
{
    const ax_node_semantics *m = semantics_of(scene, slot);
    if (m != NULL && m->role != AX_ROLE_NONE) {
        return m->role;
    }
    if (scene->store.links[row].parent == AX_NODE_NONE) {
        return AX_ROLE_WINDOW;
    }
    return is_text_leaf(scene, slot) ? AX_ROLE_LABEL : AX_ROLE_GENERIC;
}

/* ARIA's roles named from their content. */
static bool named_by_content(uint32_t role)
{
    switch (role) {
    case AX_ROLE_BUTTON:
    case AX_ROLE_CHECKBOX:
    case AX_ROLE_HEADING:
    case AX_ROLE_LINK:
    case AX_ROLE_MENU_ITEM:
    case AX_ROLE_RADIO:
    case AX_ROLE_SWITCH:
    case AX_ROLE_TAB:
        return true;
    default:
        return false;
    }
}

static int find_root(const a11y_state *st, uint64_t handle)
{
    uint32_t i;
    for (i = 0; i < st->root_count; ++i) {
        if (handle != 0 && st->roots[i].root.h == handle) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * The active root whose tree holds slot, or -1: every node from it up to the root is in the tree
 * (no HIDDEN or DISPLAY none, no text leaf above it). With `ancestors`, queues the ancestors named
 * by their content (their name may have changed).
 */
static int reach(world_impl *w, a11y_state *st, uint32_t slot, bool ancestors)
{
    const ax_scene *scene = &w->scene;
    const ax_node_store *s = &scene->store;
    uint32_t cur = slot;
    bool first = true;
    for (;;) {
        const uint32_t row = live_row(s, cur);
        uint32_t parent;
        if (row == AX_NODE_NONE) {
            return -1;
        }
        parent = s->links[row].parent;
        if (parent != AX_NODE_NONE && !included(scene, cur, row)) {
            return -1;
        }
        if (!first) {
            if (is_text_leaf(scene, cur)) {
                return -1; /* a text leaf's children are not in the tree */
            }
            if (ancestors && named_by_content(resolved_role(scene, cur, row))) {
                queue_slot(w, st, cur, 0);
            }
        }
        if (parent == AX_NODE_NONE) {
            const int i = find_root(st, ax_node_store_handle(s, cur).h);
            return i >= 0 && st->roots[i].active ? i : -1;
        }
        cur = parent;
        first = false;
    }
}

/* ---- Building records ------------------------------------------------------------------------ */

static ax_a11y_node *push_node(world_impl *w, a11y_state *st, uint32_t root_index)
{
    ax_a11y_node *n;
    if (st->out_count == st->out_capacity) {
        uint32_t cap = st->out_capacity;
        if (!grow(&w->allocator, (void **)&st->out, &cap, st->out_count + 1u, sizeof(ax_a11y_node),
                  AX_ALIGNOF(ax_a11y_node))) {
            return NULL;
        }
        cap = st->out_capacity;
        if (!grow(&w->allocator, (void **)&st->out_root, &cap, st->out_count + 1u, sizeof(uint32_t),
                  AX_ALIGNOF(uint32_t))) {
            return NULL;
        }
        st->out_capacity = cap;
    }
    n = &st->out[st->out_count];
    memset(n, 0, sizeof *n);
    st->out_root[st->out_count] = root_index;
    st->out_count++;
    return n;
}

/* The text of the text leaves below slot, joined by spaces (a name from content). */
static ax_str content_name(world_impl *w, a11y_state *st, uint32_t slot)
{
    const ax_scene *scene = &w->scene;
    const ax_node_store *s = &scene->store;
    char *buf = (char *)ax_arena_alloc(&st->arena, NAME_FROM_CONTENT_MAX, 1);
    ax_str out = {NULL, 0};
    uint32_t len = 0, cur;
    if (buf == NULL) {
        return out;
    }
    cur = s->links[live_row(s, slot)].first_child;
    while (cur != AX_NODE_NONE && cur != slot) {
        const uint32_t row = live_row(s, cur);
        bool descend = included(scene, cur, row);
        if (descend && is_text_leaf(scene, cur)) {
            const ax_node_text *t = ax_scene_text(scene, ax_node_store_handle(s, cur));
            ax_str v = shared_view(t->content);
            uint32_t n = (uint32_t)v.len;
            if (len > 0 && len < NAME_FROM_CONTENT_MAX) {
                buf[len++] = ' ';
            }
            if (n > NAME_FROM_CONTENT_MAX - len) {
                n = NAME_FROM_CONTENT_MAX - len;
            }
            if (n > 0) {
                memcpy(buf + len, v.ptr, n);
                len += n;
            }
            descend = false;
        }
        if (descend && s->links[row].first_child != AX_NODE_NONE) {
            cur = s->links[row].first_child;
            continue;
        }
        while (cur != slot && s->links[live_row(s, cur)].next_sibling == AX_NODE_NONE) {
            cur = s->links[live_row(s, cur)].parent;
        }
        if (cur == slot) {
            break;
        }
        cur = s->links[live_row(s, cur)].next_sibling;
    }
    out.ptr = buf;
    out.len = len;
    return out;
}

static uint64_t run_id(uint64_t leaf, uint32_t line)
{
    const uint64_t slot = ax_handle_slot(leaf);
    const uint64_t gen = ax_handle_generation(leaf) & 0xFFFu;
    return AX_A11Y_RUN_BIT | (gen << 51) | ((slot & 0xFFFFFFFFull) << 19) | (line & 0x7FFFFu);
}

/* The TEXT_RUN nodes of a text leaf's lines, appended after it; their ids go to `ids`. Returns
 * the hash of what they hold. */
static uint64_t build_runs(world_impl *w, a11y_state *st, uint32_t slot, uint32_t row,
                           uint32_t root_index, uint64_t **ids, uint32_t *id_count, uint64_t h)
{
    const ax_scene *scene = &w->scene;
    const ax_node_store *s = &scene->store;
    const ax_node_text *t = ax_scene_text(scene, ax_node_store_handle(s, slot));
    const ax_node_layout *box = &s->layout[row];
    const ax_text_layout *l;
    float cw, cx, cy;
    uint32_t i;
    ax_str text;
    if (t == NULL || t->paragraph == NULL || !ax_text_paragraph_is_shaped(t->paragraph)) {
        return h;
    }
    cx = box->border[AX_EDGE_LEFT] + box->padding[AX_EDGE_LEFT];
    cy = box->border[AX_EDGE_TOP] + box->padding[AX_EDGE_TOP];
    cw = box->frame.w - cx - box->border[AX_EDGE_RIGHT] - box->padding[AX_EDGE_RIGHT];
    if (cw < 0.0f) {
        cw = 0.0f;
    }
    l = ax_text_paragraph_layout(t->paragraph, cw, cw);
    if (l == NULL) {
        return h;
    }
    text = ax_text_paragraph_text(t->paragraph);
    {
        uint64_t *list = (uint64_t *)ax_arena_alloc(
            &st->arena, sizeof(uint64_t) * (*id_count + l->line_count), AX_ALIGNOF(uint64_t));
        if (list == NULL) {
            ax_text_layout_release(l);
            return h;
        }
        if (*id_count > 0) {
            memcpy(list, *ids, sizeof(uint64_t) * *id_count);
        }
        *ids = list;
    }
    for (i = 0; i < l->line_count; ++i) {
        const ax_text_line *ln = &l->lines[i];
        const uint32_t nchars = ax_text_line_chars(t->paragraph, l, i, NULL, 0);
        ax_text_char *chars = (ax_text_char *)ax_arena_alloc(
            &st->arena, sizeof(ax_text_char) * (nchars > 0 ? nchars : 1u),
            AX_ALIGNOF(ax_text_char));
        uint8_t *lengths = (uint8_t *)ax_arena_alloc(&st->arena, nchars > 0 ? nchars : 1u, 1);
        float *pos = (float *)ax_arena_alloc(&st->arena, sizeof(float) * (nchars > 0 ? nchars : 1u),
                                             AX_ALIGNOF(float));
        float *wid = (float *)ax_arena_alloc(&st->arena, sizeof(float) * (nchars > 0 ? nchars : 1u),
                                             AX_ALIGNOF(float));
        uint32_t *words = (uint32_t *)ax_arena_alloc(
            &st->arena, sizeof(uint32_t) * (nchars > 0 ? nchars : 1u), AX_ALIGNOF(uint32_t));
        uint8_t *wl = (uint8_t *)ax_arena_alloc(&st->arena, nchars > 0 ? nchars * 2u : 1u, 1);
        uint32_t nwords, k, wcount = 0;
        ax_a11y_node *run;
        const uint64_t id = run_id(ax_node_store_handle(s, slot).h, i);
        if (chars == NULL || lengths == NULL || pos == NULL || wid == NULL || words == NULL ||
            wl == NULL) {
            break;
        }
        (void)ax_text_line_chars(t->paragraph, l, i, chars, nchars);
        nwords = ax_text_line_words(t->paragraph, chars, nchars, words, nchars);
        if (nwords > nchars) {
            nwords = nchars;
        }
        for (k = 0; k < nchars; ++k) {
            lengths[k] = (uint8_t)(chars[k].length > 255u ? 255u : chars[k].length);
            pos[k] = chars[k].x - ln->x;
            wid[k] = chars[k].width;
        }
        /* AccessKit counts a word's characters in a byte: longer words are split. */
        for (k = 0; k < nwords; ++k) {
            uint32_t left = words[k];
            while (left > 0) {
                const uint32_t part = left > 255u ? 255u : left;
                wl[wcount++] = (uint8_t)part;
                left -= part;
            }
        }
        run = push_node(w, st, root_index);
        if (run == NULL) {
            break;
        }
        run->id = id;
        run->role = AX_ROLE_TEXT_RUN;
        run->bounds.x = cx + ln->x;
        run->bounds.y = cy + ln->top;
        run->bounds.w = ln->width;
        run->bounds.h = ln->height;
        run->transform = ax_affine2_identity();
        run->text.ptr = text.ptr + ln->start;
        run->text.len = ln->end - ln->start;
        run->text_start = ln->start;
        run->char_count = nchars;
        run->char_lengths = lengths;
        run->char_positions = pos;
        run->char_widths = wid;
        run->word_count = wcount;
        run->word_lengths = wl;
        (*ids)[(*id_count)++] = id;
        h = fnv(h, &run->bounds, sizeof run->bounds);
        h = fnv_str(h, run->text);
        h = fnv(h, pos, sizeof(float) * nchars);
        h = fnv(h, wid, sizeof(float) * nchars);
        h = fnv(h, wl, wcount);
        st->counters.runs_sent++; /* undone below if the leaf is not sent */
    }
    ax_text_layout_release(l);
    return h;
}

/*
 * Builds slot's record (and its text runs) at the end of the output, and keeps it when `force` or
 * when its hash differs from what was sent. Returns whether it was kept.
 */
static bool build(world_impl *w, a11y_state *st, uint32_t slot, uint32_t row, uint32_t root_index,
                  bool force)
{
    const ax_scene *scene = &w->scene;
    const ax_node_store *s = &scene->store;
    const ax_node_semantics *m = semantics_of(scene, slot);
    const ax_node_layout *box = &s->layout[row];
    const ax_arena_mark mark = ax_arena_save(&st->arena);
    const uint32_t at = st->out_count;
    const uint64_t runs_before = st->counters.runs_sent;
    const uint32_t parent = s->links[row].parent;
    a11y_rec *rec = &st->recs[slot];
    ax_a11y_node *n = push_node(w, st, root_index);
    uint64_t h = 1469598103934665603ull;
    uint64_t *ids = NULL;
    uint32_t count = 0, cur, idx;
    bool leaf;
    if (n == NULL) {
        st->overflowed = true;
        return false;
    }
    st->counters.built++;
    leaf = is_text_leaf(scene, slot);
    n->id = ax_node_store_handle(s, slot).h;
    n->role = resolved_role(scene, slot, row);
    if (m != NULL) {
        n->flags = (m->disabled ? AX_A11Y_DISABLED : 0u) | (m->selected ? AX_A11Y_SELECTED : 0u) |
                   ((m->actions & AX_ACTION_FOCUS) != 0 ? AX_A11Y_FOCUSABLE : 0u) |
                   ((m->own & AX_SEMANTICS_BIT(AX_PROP_VALUE)) != 0 ? AX_A11Y_HAS_VALUE : 0u) |
                   ((m->own & AX_SEMANTICS_BIT(AX_PROP_VALUE_MIN)) != 0 ? AX_A11Y_HAS_MIN : 0u) |
                   ((m->own & AX_SEMANTICS_BIT(AX_PROP_VALUE_MAX)) != 0 ? AX_A11Y_HAS_MAX : 0u) |
                   ((m->own & AX_SEMANTICS_BIT(AX_PROP_VALUE_STEP)) != 0 ? AX_A11Y_HAS_STEP : 0u);
        n->checked = m->checked;
        n->expanded = m->expanded;
        n->actions = m->actions;
        n->level = m->level;
        n->name = shared_view(m->label);
        n->description = shared_view(m->description);
        n->value_text = shared_view(m->value_text);
        n->test_id = shared_view(m->test_id);
        n->value = m->value;
        n->value_min = m->value_min;
        n->value_max = m->value_max;
        n->value_step = m->value_step;
    }
    if (n->name.len == 0) {
        if (leaf) {
            n->name = shared_view(ax_scene_text(scene, ax_node_store_handle(s, slot))->content);
        } else if (named_by_content(n->role)) {
            n->name = content_name(w, st, slot);
        }
    }
    /* Geometry: target values (decision 108). */
    n->bounds.x = 0.0f;
    n->bounds.y = 0.0f;
    n->bounds.w = box->frame.w;
    n->bounds.h = box->frame.h;
    {
        const ax_affine2 t = s->composite[row].transform;
        const float cx = box->frame.w * 0.5f, cy = box->frame.h * 0.5f;
        ax_affine2 about = ax_affine2_mul(ax_affine2_translate(cx, cy),
                                          ax_affine2_mul(t, ax_affine2_translate(-cx, -cy)));
        n->transform = ax_affine2_mul(ax_affine2_translate(box->frame.x, box->frame.y), about);
    }
    /* Children: the ones in the tree, or a text leaf's runs. */
    if (!leaf) {
        for (cur = s->links[row].first_child; cur != AX_NODE_NONE;
             cur = s->links[live_row(s, cur)].next_sibling) {
            count += included(scene, cur, live_row(s, cur)) ? 1u : 0u;
        }
        if (count > 0) {
            ids = (uint64_t *)ax_arena_alloc(&st->arena, sizeof(uint64_t) * count,
                                             AX_ALIGNOF(uint64_t));
            if (ids == NULL) {
                st->out_count = at;
                ax_arena_restore(&st->arena, mark);
                st->overflowed = true;
                return false;
            }
            count = 0;
            for (cur = s->links[row].first_child; cur != AX_NODE_NONE;
                 cur = s->links[live_row(s, cur)].next_sibling) {
                if (included(scene, cur, live_row(s, cur))) {
                    ids[count++] = ax_node_store_handle(s, cur).h;
                }
            }
        }
    }
    h = fnv_u32(h, n->role);
    h = fnv_u32(h, n->flags);
    h = fnv_u32(h, n->checked);
    h = fnv_u32(h, n->expanded);
    h = fnv_u32(h, n->actions);
    h = fnv_u32(h, n->level);
    h = fnv_str(h, n->name);
    h = fnv_str(h, n->description);
    h = fnv_str(h, n->value_text);
    h = fnv_str(h, n->test_id);
    h = fnv(h, &n->value, sizeof(float) * 4u);
    h = fnv(h, &n->bounds, sizeof n->bounds);
    h = fnv(h, &n->transform, sizeof n->transform);
    h = fnv_u32(h, count);
    if (count > 0) {
        h = fnv(h, ids, sizeof(uint64_t) * count);
    }
    if (leaf) {
        h = build_runs(w, st, slot, row, root_index, &ids, &count, h);
        n = &st->out[at]; /* the output may have moved */
    }
    n->children = ids;
    n->child_count = count;
    if (!force && rec->root == root_index + 1u && rec->hash == h) {
        st->out_count = at;
        ax_arena_restore(&st->arena, mark);
        st->counters.runs_sent = runs_before;
        return false;
    }
    rec->handle = n->id;
    rec->parent = parent == AX_NODE_NONE ? 0 : ax_node_store_handle(s, parent).h;
    rec->root = root_index + 1u;
    rec->hash = h;
    for (idx = at; idx < st->out_count; ++idx) {
        st->counters.sent++;
    }
    return true;
}

/* Sends slot and every node below it in the tree (it has just joined it). */
static void build_subtree(world_impl *w, a11y_state *st, uint32_t top, uint32_t root_index)
{
    const ax_scene *scene = &w->scene;
    const ax_node_store *s = &scene->store;
    uint32_t depth = 0;
    if (!grow(&w->allocator, (void **)&st->stack, &st->stack_capacity, 1u, sizeof(uint32_t),
              AX_ALIGNOF(uint32_t))) {
        st->overflowed = true;
        return;
    }
    st->stack[depth++] = top;
    while (depth > 0) {
        const uint32_t slot = st->stack[--depth];
        const uint32_t row = live_row(s, slot);
        uint32_t cur, n = 0, i;
        if (row == AX_NODE_NONE) {
            continue;
        }
        if (slot >= st->rec_capacity && !grow(&w->allocator, (void **)&st->recs, &st->rec_capacity,
                                              slot + 1u, sizeof(a11y_rec), AX_ALIGNOF(a11y_rec))) {
            st->overflowed = true;
            return;
        }
        (void)build(w, st, slot, row, root_index, true);
        if (is_text_leaf(scene, slot)) {
            continue;
        }
        /* Children pushed in reverse, so they are sent in order. */
        for (cur = s->links[row].first_child; cur != AX_NODE_NONE;
             cur = s->links[live_row(s, cur)].next_sibling) {
            n++;
        }
        if (!grow(&w->allocator, (void **)&st->stack, &st->stack_capacity, depth + n,
                  sizeof(uint32_t), AX_ALIGNOF(uint32_t))) {
            st->overflowed = true;
            return;
        }
        i = depth + n;
        for (cur = s->links[row].first_child; cur != AX_NODE_NONE;
             cur = s->links[live_row(s, cur)].next_sibling) {
            st->stack[--i] = cur;
        }
        for (i = depth; i < depth + n; ++i) {
            const uint32_t c = st->stack[i];
            if (!included(scene, c, live_row(s, c))) {
                st->stack[i] = AX_NODE_NONE; /* skipped: not in the tree */
            }
        }
        depth += n;
    }
}

static void queue_handle(world_impl *w, a11y_state *st, uint64_t handle)
{
    ax_node n;
    n.h = handle;
    if (handle != 0 && ax_node_store_row(&w->scene.store, n) != AX_NODE_NONE) {
        queue_slot(w, st, ax_handle_slot(handle), 0);
    }
}

static void process(world_impl *w, a11y_state *st, a11y_entry e)
{
    /* queue_slot may grow the records: they are read by index after every call that queues. */
#define REC (st->recs[e.slot])
    const ax_node_store *s = &w->scene.store;
    const uint32_t row = live_row(s, e.slot);
    const uint64_t handle = row != AX_NODE_NONE ? ax_node_store_handle(s, e.slot).h : 0;
    uint64_t parent, old_parent;
    int ri;
    if (REC.handle != handle) {
        /* The node it described is gone: its parent loses a child. */
        old_parent = REC.root != 0 ? REC.parent : 0;
        REC.handle = handle;
        REC.root = 0;
        REC.parent = 0;
        REC.hash = 0;
        queue_handle(w, st, old_parent);
        if (handle == 0) {
            return;
        }
    }
    ri = reach(w, st, e.slot, (e.flags & (TOUCH_STRUCT | TOUCH_TEXT)) != 0);
    if (ri < 0) {
        if (REC.root != 0) { /* it left the tree */
            REC.root = 0;
            queue_handle(w, st, REC.parent);
        }
        return;
    }
    parent =
        s->links[row].parent == AX_NODE_NONE ? 0 : ax_node_store_handle(s, s->links[row].parent).h;
    if (REC.root != (uint32_t)ri + 1u) {
        /* It joined this tree: it and its subtree are new to it. */
        old_parent = REC.root != 0 ? REC.parent : 0;
        queue_handle(w, st, old_parent);
        build_subtree(w, st, e.slot, (uint32_t)ri);
        queue_handle(w, st, parent);
        return;
    }
    old_parent = REC.parent;
    if (old_parent != parent || (e.flags & TOUCH_STRUCT) != 0) {
        queue_handle(w, st, old_parent);
        queue_handle(w, st, parent);
    }
    (void)build(w, st, e.slot, row, (uint32_t)ri, false);
#undef REC
}

/* ---- Activation, actions --------------------------------------------------------------------- */

static void update_enabled(world_impl *w, a11y_state *st)
{
    uint32_t i;
    bool any = false;
    for (i = 0; i < st->root_count; ++i) {
        any = any || st->roots[i].active;
    }
    if (any && !w->a11y_enabled) {
        st->queue_count = 0; /* full builds cover what was queued */
        st->epoch++;
    }
    w->a11y_enabled = any;
    ax_atomic_store_u32(&w->a11y_active, any ? 1u : 0u, AX_MO_RELEASE);
}

ax_result ax_world_set_a11y_sink(ax_world world, ax_node root, const ax_a11y_sink *sink)
{
    world_impl *w = ax__world_from_handle(world);
    a11y_state *st;
    int i;
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_set_a11y_sink: invalid world");
    }
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_set_a11y_sink: not the scene thread");
    if (AX_HANDLE_IS_NULL(root)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_world_set_a11y_sink: null root");
    }
    st = state(w);
    if (st == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_set_a11y_sink: out of memory");
    }
    i = find_root(st, root.h);
    if (sink == NULL || sink->update == NULL) {
        /* Entries keep their index (records name roots by it): a free entry has a null root. */
        if (i >= 0) {
            memset(&st->roots[i], 0, sizeof st->roots[i]);
            update_enabled(w, st);
        }
        return AX_OK;
    }
    if (i < 0) {
        i = 0;
        while (i < (int)st->root_count && st->roots[i].root.h != 0) {
            i++; /* the first free entry, or a new one */
        }
        if (i == (int)st->root_count) {
            if (st->root_count == AX_A11Y_MAX_ROOTS ||
                !grow(&w->allocator, (void **)&st->roots, &st->root_capacity, st->root_count + 1u,
                      sizeof(a11y_root), AX_ALIGNOF(a11y_root))) {
                return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_set_a11y_sink: out of memory");
            }
            st->root_count++;
        }
        memset(&st->roots[i], 0, sizeof st->roots[i]);
        st->roots[i].root = root;
    }
    st->roots[i].sink = *sink;
    return AX_OK;
}

void ax_world_a11y_activate(ax_world world, ax_node root, bool active)
{
    world_impl *w = ax__world_from_handle(world);
    uint32_t i;
    if (w == NULL || AX_HANDLE_IS_NULL(root)) {
        return;
    }
    ax_mutex_lock(&w->a11y_lock);
    for (i = 0; i < w->a11y_activation_count; ++i) {
        if (w->a11y_activations[i].root.h == root.h) {
            break;
        }
    }
    if (i < AX_A11Y_MAX_ACTIVATIONS) {
        w->a11y_activations[i].root = root;
        w->a11y_activations[i].active = active;
        w->a11y_activations[i].age = 0;
        if (i == w->a11y_activation_count) {
            w->a11y_activation_count++;
        }
    }
    (void)ax_atomic_fetch_add_u64(&w->a11y_posted, 1u, AX_MO_RELAXED);
    ax_atomic_store_u32(&w->a11y_pending, 1u, AX_MO_RELEASE);
    ax_mutex_unlock(&w->a11y_lock);
    ax__world_wake(w, root);
}

bool ax_world_post_a11y_action(ax_world world, const ax_a11y_action *action)
{
    world_impl *w = ax__world_from_handle(world);
    bool queued = false;
    if (w == NULL || action == NULL) {
        return false;
    }
    ax_mutex_lock(&w->a11y_lock);
    if (w->a11y_action_count < AX_A11Y_ACTION_QUEUE) {
        w->a11y_actions[w->a11y_action_count++] = *action;
        (void)ax_atomic_fetch_add_u64(&w->a11y_posted, 1u, AX_MO_RELAXED);
        queued = true;
        ax_atomic_store_u32(&w->a11y_pending, 1u, AX_MO_RELEASE);
    }
    ax_mutex_unlock(&w->a11y_lock);
    if (!queued) {
        ax_atomic_fetch_add_u32(&w->a11y_dropped, 1u, AX_MO_RELAXED);
        return false;
    }
    ax__world_wake(w, action->root);
    return true;
}

static void run_action(world_impl *w, a11y_state *st, const ax_a11y_action *a)
{
    const ax_node_store *s = &w->scene.store;
    ax_node node;
    uint32_t row;
    const ax_node_semantics *m;
    ax_world_event ev;
    uint64_t target = a->target;
    if ((target & AX_A11Y_RUN_BIT) != 0) {
        /* A text run's: its leaf's slot is in the id; the leaf must still be the run's. */
        const uint32_t slot = (uint32_t)((target >> 19) & 0xFFFFFFFFull);
        node = ax_node_store_handle(s, live_row(s, slot) == AX_NODE_NONE ? AX_NODE_NONE : slot);
        target = node.h;
    }
    node.h = target;
    row = ax_node_store_row(s, node);
    if (row == AX_NODE_NONE || reach(w, st, ax_handle_slot(node.h), false) < 0) {
        st->counters.actions_dropped++;
        return;
    }
    m = semantics_of(&w->scene, ax_handle_slot(node.h));
    if (m == NULL || (m->actions & a->action) == 0 || (a->action & (a->action - 1u)) != 0) {
        st->counters.actions_dropped++;
        return;
    }
    if (a->action == AX_ACTION_FOCUS) {
        st->focus = node.h;
    }
    memset(&ev, 0, sizeof ev);
    ev.struct_size = sizeof ev;
    ev.type = AX_WORLD_EVENT_A11Y_ACTION;
    ev.root = a->root.h;
    ev.time_ns = ax__world_now(w);
    ev.data.a11y_action.node = node.h;
    ev.data.a11y_action.action = a->action;
    ev.data.a11y_action.value = a->value;
    {
        ax_world world;
        world.h = ax_handle_make(w->slot, w->generation);
        (void)ax_world_push_event(world, &ev);
    }
    st->counters.actions++;
}

/* An activation may come before the scene thread has its root's sink (the surface is picked up
 * at the next scene step): it waits for it this many flushes. */
#define ACTIVATION_PATIENCE 64u

static void take_pending(world_impl *w, a11y_state *st)
{
    struct {
        ax_node root;
        bool active;
        uint32_t age;
    } acts[AX_A11Y_MAX_ACTIVATIONS];
    ax_a11y_action actions[AX_A11Y_ACTION_QUEUE];
    uint32_t nacts, nactions, i, waiting = 0;
    uint64_t posted;
    ax_mutex_lock(&w->a11y_lock);
    posted = ax_atomic_load_u64(&w->a11y_posted, AX_MO_RELAXED);
    nacts = w->a11y_activation_count;
    for (i = 0; i < nacts; ++i) {
        acts[i].root = w->a11y_activations[i].root;
        acts[i].active = w->a11y_activations[i].active;
        acts[i].age = w->a11y_activations[i].age;
    }
    nactions = w->a11y_action_count;
    memcpy(actions, w->a11y_actions, sizeof(ax_a11y_action) * nactions);
    w->a11y_activation_count = 0;
    w->a11y_action_count = 0;
    ax_atomic_store_u32(&w->a11y_pending, 0u, AX_MO_RELAXED);
    ax_mutex_unlock(&w->a11y_lock);
    for (i = 0; i < nacts; ++i) {
        const int r = find_root(st, acts[i].root.h);
        if (r < 0) {
            /* No sink for it yet: it waits (unless a newer activation of the root came). */
            if (acts[i].age + 1u < ACTIVATION_PATIENCE) {
                acts[waiting] = acts[i];
                acts[waiting].age++;
                waiting++;
            }
            continue;
        }
        if (acts[i].active && !st->roots[r].active) {
            st->roots[r].wants_full = true;
        }
        st->roots[r].active = acts[i].active;
        if (!acts[i].active) {
            st->roots[r].sent_focus = 0;
        }
    }
    if (waiting > 0) {
        ax_mutex_lock(&w->a11y_lock);
        for (i = 0; i < waiting && w->a11y_activation_count < AX_A11Y_MAX_ACTIVATIONS; ++i) {
            uint32_t k;
            for (k = 0; k < w->a11y_activation_count; ++k) {
                if (w->a11y_activations[k].root.h == acts[i].root.h) {
                    break; /* a newer one replaced it */
                }
            }
            if (k == w->a11y_activation_count) {
                w->a11y_activations[k].root = acts[i].root;
                w->a11y_activations[k].active = acts[i].active;
                w->a11y_activations[k].age = acts[i].age;
                w->a11y_activation_count++;
            }
        }
        ax_atomic_store_u32(&w->a11y_pending, 1u, AX_MO_RELEASE);
        ax_mutex_unlock(&w->a11y_lock);
    }
    if (waiting == 0) {
        st->handled = posted; /* every request taken is handled by the end of this flush */
    }
    update_enabled(w, st);
    for (i = 0; i < nactions; ++i) {
        run_action(w, st, &actions[i]);
    }
}

/* ---- The flush ------------------------------------------------------------------------------- */

static uint64_t focus_of(world_impl *w, a11y_state *st, uint32_t root_index)
{
    const a11y_root *r = &st->roots[root_index];
    if (st->focus != 0 && reach(w, st, ax_handle_slot(st->focus), false) == (int)root_index) {
        ax_node f;
        f.h = st->focus;
        if (ax_node_store_row(&w->scene.store, f) != AX_NODE_NONE) {
            return st->focus;
        }
    }
    return r->root.h;
}

static void send(world_impl *w, a11y_state *st, uint32_t root_index, bool full)
{
    a11y_root *r = &st->roots[root_index];
    ax_a11y_update u;
    uint32_t i, n = 0;
    const uint64_t focus = focus_of(w, st, root_index);
    for (i = 0; i < st->out_count; ++i) {
        n += st->out_root[i] == root_index ? 1u : 0u;
    }
    if (n == 0 && !full && focus == r->sent_focus) {
        return;
    }
    if (!grow(&w->allocator, (void **)&st->grouped, &st->grouped_capacity, n > 0 ? n : 1u,
              sizeof(ax_a11y_node), AX_ALIGNOF(ax_a11y_node))) {
        st->overflowed = true;
        return;
    }
    n = 0;
    for (i = 0; i < st->out_count; ++i) {
        if (st->out_root[i] == root_index) {
            st->grouped[n++] = st->out[i];
        }
    }
    memset(&u, 0, sizeof u);
    u.root = r->root;
    u.focus = focus;
    u.full = full;
    u.nodes = st->grouped;
    u.count = n;
    r->sent_focus = focus;
    st->counters.updates++;
    r->sink.update(r->sink.user, &u);
}

void ax_world_a11y_flush(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    a11y_state *st;
    uint32_t i;
    bool full_any = false;
    bool full_root[AX_A11Y_MAX_ROOTS];
    if (w == NULL) {
        return;
    }
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_a11y_flush: not the scene thread");
    st = (a11y_state *)w->a11y;
    if (st == NULL) {
        return;
    }
    if (ax_atomic_load_u32(&w->a11y_pending, AX_MO_ACQUIRE) != 0) {
        take_pending(w, st);
    }
    {
        /* Counted by posting threads; taken here without an exchange (base has none). */
        const uint32_t dropped = ax_atomic_load_u32(&w->a11y_dropped, AX_MO_RELAXED);
        if (dropped != 0) {
            (void)ax_atomic_fetch_add_u32(&w->a11y_dropped, 0u - dropped, AX_MO_RELAXED);
            st->counters.actions_dropped += dropped;
        }
    }
    if (!w->a11y_enabled) {
        ax_atomic_store_u64(&w->a11y_handled, st->handled, AX_MO_RELEASE);
        return; /* nothing reads the tree: nothing to do (decision 108) */
    }
    ax_arena_reset(&st->arena);
    st->out_count = 0;
    if (st->overflowed) {
        /* Something could not be queued: every active tree again. */
        st->overflowed = false;
        for (i = 0; i < st->root_count; ++i) {
            st->roots[i].wants_full = st->roots[i].active;
        }
    }
    if (!grow(&w->allocator, (void **)&st->recs, &st->rec_capacity,
              w->scene.store.slot_count > 0 ? w->scene.store.slot_count : 1u, sizeof(a11y_rec),
              AX_ALIGNOF(a11y_rec))) {
        st->overflowed = true;
        return;
    }
    memset(full_root, 0, sizeof full_root);
    for (i = 0; i < st->root_count; ++i) {
        a11y_root *r = &st->roots[i];
        const uint32_t row = ax_node_store_row(&w->scene.store, r->root);
        if (!r->active || !r->wants_full || row == AX_NODE_NONE) {
            continue;
        }
        r->wants_full = false;
        full_root[i] = true;
        full_any = true;
        st->counters.full_builds++;
        build_subtree(w, st, ax_handle_slot(r->root.h), i);
    }
    (void)full_any;
    for (i = 0; i < st->queue_count; ++i) {
        process(w, st, st->queue[i]); /* may queue more: parents, named ancestors */
    }
    st->queue_count = 0;
    st->epoch++;
    for (i = 0; i < st->root_count; ++i) {
        if (st->roots[i].active) {
            send(w, st, i, full_root[i]);
        }
    }
    ax_atomic_store_u64(&w->a11y_flushed_seq, ax_world_last_applied_seq(world), AX_MO_RELEASE);
    ax_atomic_store_u64(&w->a11y_handled, st->handled, AX_MO_RELEASE);
}

bool ax_world_a11y_enabled(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    return w != NULL && w->a11y_enabled;
}

bool ax_world_a11y_caught_up(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    if (w == NULL) {
        return true;
    }
    if (ax_atomic_load_u64(&w->a11y_handled, AX_MO_ACQUIRE) <
        ax_atomic_load_u64(&w->a11y_posted, AX_MO_RELAXED)) {
        return false;
    }
    return ax_atomic_load_u32(&w->a11y_active, AX_MO_ACQUIRE) == 0 ||
           ax_atomic_load_u64(&w->a11y_flushed_seq, AX_MO_ACQUIRE) >=
               ax_world_last_applied_seq(world);
}

void ax_world_get_a11y_counters(ax_world world, ax_a11y_counters *out, bool reset)
{
    world_impl *w = ax__world_from_handle(world);
    a11y_state *st = w != NULL ? (a11y_state *)w->a11y : NULL;
    memset(out, 0, sizeof *out);
    if (st == NULL) {
        return;
    }
    *out = st->counters;
    if (reset) {
        memset(&st->counters, 0, sizeof st->counters);
    }
}
