/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The node store: dense hot columns indexed by row, a slot table for stable handles, hierarchy
 * links by slot, and side components.
 */
#include <math.h>
#include <string.h>

#include <axion/base/ax_assert.h>
#include <axion/base/ax_memory.h>
#include <axion/base/ax_thread.h>
#include <axion/core/ax_node_store.h>

#define CHECK_THREAD(s)                                                                        \
    do {                                                                                       \
        AX_ASSERT_MSG(!ax_node_store_in_measure(),                                             \
                      "a layout measure function touched the node store; it may only use its " \
                      "node's data and the constraints (decision 90)");                        \
        AX_ASSERT_MSG((s)->owner_thread == ax_thread_current_id(),                             \
                      "ax_node_store used from a thread that does not own it (the scene "      \
                      "thread; see ax_node_store_bind_thread)");                               \
    } while (0)

static AX_THREAD_LOCAL uint32_t tl_measure_depth;

void ax_node_store_enter_measure(void)
{
    tl_measure_depth++;
}

void ax_node_store_leave_measure(void)
{
    AX_ASSERT(tl_measure_depth > 0);
    tl_measure_depth--;
}

bool ax_node_store_in_measure(void)
{
    return tl_measure_depth != 0;
}

#define MIN_ROWS          64u
#define MIN_SLOTS         64u
#define SPARSE_PAGE_SHIFT 8u
#define SPARSE_PAGE_SIZE  (1u << SPARSE_PAGE_SHIFT)

/* ---- Columns --------------------------------------------------------------------------------- */

/* A hot column: one of the store's typed array pointers, reached by offset. The pointer fields are
 * read and written with memcpy, so code handling every column generically never accesses a
 * `uint16_t *` or an `ax_node_links *` through a `void *` lvalue (strict aliasing). */
typedef struct column {
    size_t offset; /* of the pointer field in ax_node_store */
    size_t elem_size;
    size_t align;
} column;

#define COLUMN(field, type) {offsetof(ax_node_store, field), sizeof(type), AX_ALIGNOF(type)}

static const column COLUMNS[] = {
    COLUMN(slot_of_row, uint32_t),
    COLUMN(kind, uint16_t),
    COLUMN(dirty, uint32_t),
    COLUMN(component_mask, uint32_t),
    COLUMN(links, ax_node_links),
    COLUMN(style, ax_node_style),
    COLUMN(layout, ax_node_layout),
    COLUMN(paint, ax_node_paint),
    COLUMN(composite, ax_node_composite),
};

enum { COLUMN_COUNT = (int)(sizeof COLUMNS / sizeof COLUMNS[0]) };

AX_STATIC_ASSERT(sizeof(uint16_t *) == sizeof(void *) && sizeof(ax_node_links *) == sizeof(void *),
                 "column pointers share the representation of void *");

static void *column_base(const ax_node_store *s, const column *c)
{
    void *p;
    memcpy(&p, (const unsigned char *)s + c->offset, sizeof p);
    return p;
}

static void column_set_base(ax_node_store *s, const column *c, void *p)
{
    memcpy((unsigned char *)s + c->offset, &p, sizeof p);
}

static void *column_at(const ax_node_store *s, const column *c, uint32_t row)
{
    return (unsigned char *)column_base(s, c) + (size_t)row * c->elem_size;
}

/* Grows every column to hold `needed` rows. All or nothing. */
static bool reserve_rows(ax_node_store *s, uint32_t needed)
{
    const column *cols = COLUMNS;
    void *fresh[COLUMN_COUNT];
    uint32_t cap;
    int i;

    if (needed <= s->row_capacity) {
        return true;
    }
    cap = s->row_capacity < MIN_ROWS ? MIN_ROWS : s->row_capacity;
    while (cap < needed) {
        cap = cap > 0x7FFFFFFFu ? AX_NODE_STORE_MAX_SLOTS : cap * 2u;
    }
    for (i = 0; i < COLUMN_COUNT; ++i) {
        fresh[i] = ax_mem_alloc(&s->allocator, cols[i].elem_size * cap, cols[i].align);
        if (fresh[i] == NULL) {
            while (i-- > 0) {
                ax_mem_free(&s->allocator, fresh[i], cols[i].elem_size * cap);
            }
            return false;
        }
    }
    for (i = 0; i < COLUMN_COUNT; ++i) {
        void *old = column_base(s, &cols[i]);
        if (old != NULL) {
            memcpy(fresh[i], old, cols[i].elem_size * s->row_count);
            ax_mem_free(&s->allocator, old, cols[i].elem_size * s->row_capacity);
        }
        column_set_base(s, &cols[i], fresh[i]);
    }
    s->row_capacity = cap;
    return true;
}

/* ---- Slots ----------------------------------------------------------------------------------- */

static bool reserve_slots(ax_node_store *s, uint32_t needed)
{
    uint32_t cap;
    ax_node_slot *fresh;
    if (needed <= s->slot_capacity) {
        return true;
    }
    if (needed > AX_NODE_STORE_MAX_SLOTS) {
        return false;
    }
    cap = s->slot_capacity < MIN_SLOTS ? MIN_SLOTS : s->slot_capacity;
    while (cap < needed) {
        cap = cap > AX_NODE_STORE_MAX_SLOTS / 2u ? AX_NODE_STORE_MAX_SLOTS : cap * 2u;
    }
    if (s->slots == NULL) {
        fresh = (ax_node_slot *)ax_mem_alloc(&s->allocator, sizeof(ax_node_slot) * cap,
                                             AX_ALIGNOF(ax_node_slot));
    } else {
        fresh = (ax_node_slot *)ax_mem_resize(&s->allocator, s->slots,
                                              sizeof(ax_node_slot) * s->slot_capacity,
                                              sizeof(ax_node_slot) * cap, AX_ALIGNOF(ax_node_slot));
    }
    if (fresh == NULL) {
        return false;
    }
    s->slots = fresh;
    s->slot_capacity = cap;
    return true;
}

/* Makes slots [slot_count, upto) known: free, generation 1. */
static void open_slots(ax_node_store *s, uint32_t upto)
{
    while (s->slot_count < upto) {
        ax_node_slot *sl = &s->slots[s->slot_count++];
        sl->generation = 1;
        sl->row = AX_NODE_NONE;
        sl->next_free = AX_NODE_NONE;
    }
}

static ax_node_links *links_of_slot(ax_node_store *s, uint32_t slot)
{
    return &s->links[s->slots[slot].row];
}

/* ---- Init and teardown ----------------------------------------------------------------------- */

void ax_node_store_init(ax_node_store *s, const ax_node_store_desc *desc)
{
    AX_ASSERT(s != NULL);
    memset(s, 0, sizeof *s);
    s->allocator =
        *(desc != NULL && desc->allocator != NULL ? desc->allocator : ax_default_allocator());
    s->owner_thread = ax_thread_current_id();
    if (desc != NULL) {
        s->on_slot_freed = desc->on_slot_freed;
        s->user = desc->user;
    }
    s->free_head = AX_NODE_NONE;
}

static void free_component_storage(ax_node_store *s, ax_component_storage *cs)
{
    uint32_t i;
    if (cs->desc.on_remove != NULL) {
        for (i = 0; i < cs->count; ++i) {
            cs->desc.on_remove(cs->data + (size_t)i * cs->stride, cs->desc.user);
        }
    }
    ax_mem_free(&s->allocator, cs->data, cs->stride * cs->capacity);
    ax_mem_free(&s->allocator, cs->owner_slot, sizeof(uint32_t) * cs->capacity);
    for (i = 0; i < cs->sparse_pages; ++i) {
        ax_mem_free(&s->allocator, cs->sparse[i], sizeof(uint32_t) * SPARSE_PAGE_SIZE);
    }
    ax_mem_free(&s->allocator, cs->sparse, sizeof(uint32_t *) * cs->sparse_pages);
    memset(cs, 0, sizeof *cs);
}

void ax_node_store_deinit(ax_node_store *s)
{
    uint32_t i;
    CHECK_THREAD(s);
    for (i = 0; i < s->component_type_count; ++i) {
        free_component_storage(s, &s->components[i]);
    }
    for (i = 0; i < (uint32_t)COLUMN_COUNT; ++i) {
        ax_mem_free(&s->allocator, column_base(s, &COLUMNS[i]),
                    COLUMNS[i].elem_size * s->row_capacity);
        column_set_base(s, &COLUMNS[i], NULL);
    }
    ax_mem_free(&s->allocator, s->slots, sizeof(ax_node_slot) * s->slot_capacity);
    memset(s, 0, sizeof *s);
}

void ax_node_store_bind_thread(ax_node_store *s)
{
    s->owner_thread = ax_thread_current_id();
}

bool ax_node_store_on_owner_thread(const ax_node_store *s)
{
    return s->owner_thread == ax_thread_current_id();
}

/* ---- Creating and destroying ----------------------------------------------------------------- */

/* Appends a row with default values for a slot. Rows must have been reserved. */
static void init_row(ax_node_store *s, uint32_t slot, ax_node_kind kind)
{
    uint32_t row = s->row_count++;
    ax_node_links *l = &s->links[row];
    ax_node_style *st = &s->style[row];
    ax_node_paint *p = &s->paint[row];
    ax_node_composite *c = &s->composite[row];

    s->slot_of_row[row] = slot;
    s->kind[row] = (uint16_t)kind;
    s->dirty[row] = 0;
    s->component_mask[row] = 0;
    l->parent = l->first_child = l->last_child = l->prev_sibling = l->next_sibling = AX_NODE_NONE;
    memset(st, 0, sizeof *st); /* every dimension AX_UNIT_AUTO, every enum its first value */
    for (int e = 0; e < 4; ++e) {
        st->margin[e].unit = AX_UNIT_POINTS; /* 0 points: AUTO is an auto margin */
    }
    st->flex_direction = AX_FLEX_DIRECTION_COLUMN; /* Yoga's defaults (decision 89) */
    st->align_items = AX_ALIGN_STRETCH;
    st->align_content = AX_ALIGN_FLEX_START;
    memset(&s->layout[row], 0, sizeof s->layout[row]);
    s->layout[row].flex_basis = NAN;
    memset(p, 0, sizeof *p); /* transparent, square corners */
    c->transform = ax_affine2_identity();
    c->opacity = 1.0f;
    s->slots[slot].row = row;
}

ax_result ax_node_store_create(ax_node_store *s, ax_node_kind kind, ax_node *out)
{
    uint32_t slot;
    CHECK_THREAD(s);
    AX_ASSERT_MSG(s->on_slot_freed == NULL,
                  "ax_node_store_create needs internal slots; this store uses create_at");
    AX_ASSERT(out != NULL);
    out->h = 0;
    if (!reserve_rows(s, s->row_count + 1u)) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    if (s->free_head != AX_NODE_NONE) {
        slot = s->free_head;
        s->free_head = s->slots[slot].next_free;
        s->slots[slot].next_free = AX_NODE_NONE;
    } else {
        if (s->slot_count >= AX_NODE_STORE_MAX_SLOTS || !reserve_slots(s, s->slot_count + 1u)) {
            return AX_ERR_OUT_OF_MEMORY;
        }
        slot = s->slot_count;
        open_slots(s, slot + 1u);
    }
    init_row(s, slot, kind);
    out->h = ax_handle_make(slot, s->slots[slot].generation);
    return AX_OK;
}

ax_result ax_node_store_create_at(ax_node_store *s, ax_node node, ax_node_kind kind)
{
    uint32_t slot = ax_handle_slot(node.h);
    uint32_t gen = ax_handle_generation(node.h);
    CHECK_THREAD(s);
    AX_ASSERT_MSG(
        s->on_slot_freed != NULL,
        "ax_node_store_create_at needs external slots (ax_node_store_desc.on_slot_freed)");
    if (gen == 0 || slot >= AX_NODE_STORE_MAX_SLOTS) {
        return AX_ERR_INVALID_HANDLE;
    }
    if (slot < s->slot_count) {
        const ax_node_slot *sl = &s->slots[slot];
        if (sl->row != AX_NODE_NONE || gen < sl->generation) {
            return AX_ERR_INVALID_HANDLE; /* live, retired, or a generation already used */
        }
    }
    /* A slot never seen accepts any generation from 1; a free slot any generation from the one
     * it records. Generations the app skipped (aborted or rejected creates) are never reused. */
    if (!reserve_rows(s, s->row_count + 1u) || !reserve_slots(s, slot + 1u)) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    open_slots(s, slot + 1u);
    s->slots[slot].generation = gen;
    init_row(s, slot, kind);
    return AX_OK;
}

static void unlink_slot(ax_node_store *s, uint32_t slot)
{
    ax_node_links *l = links_of_slot(s, slot);
    ax_node_links *p;
    if (l->parent == AX_NODE_NONE) {
        return;
    }
    p = links_of_slot(s, l->parent);
    if (l->prev_sibling != AX_NODE_NONE) {
        links_of_slot(s, l->prev_sibling)->next_sibling = l->next_sibling;
    } else {
        p->first_child = l->next_sibling;
    }
    if (l->next_sibling != AX_NODE_NONE) {
        links_of_slot(s, l->next_sibling)->prev_sibling = l->prev_sibling;
    } else {
        p->last_child = l->prev_sibling;
    }
    l->parent = l->prev_sibling = l->next_sibling = AX_NODE_NONE;
}

/* Links child (which has no parent) before `before` (a child of parent), or last when NONE. */
static void link_slot(ax_node_store *s, uint32_t parent, uint32_t child, uint32_t before)
{
    ax_node_links *p = links_of_slot(s, parent);
    ax_node_links *c = links_of_slot(s, child);
    AX_ASSERT(c->parent == AX_NODE_NONE);
    c->parent = parent;
    c->next_sibling = before;
    if (before == AX_NODE_NONE) {
        c->prev_sibling = p->last_child;
        if (p->last_child != AX_NODE_NONE) {
            links_of_slot(s, p->last_child)->next_sibling = child;
        } else {
            p->first_child = child;
        }
        p->last_child = child;
    } else {
        ax_node_links *b = links_of_slot(s, before);
        c->prev_sibling = b->prev_sibling;
        if (b->prev_sibling != AX_NODE_NONE) {
            links_of_slot(s, b->prev_sibling)->next_sibling = child;
        } else {
            p->first_child = child;
        }
        b->prev_sibling = child;
    }
}

static void remove_component_at(ax_node_store *s, ax_component_storage *cs, uint32_t slot);

/* Frees a node that has no children and no parent left: its components, its row (swap-remove)
 * and its slot. */
static void free_node(ax_node_store *s, uint32_t slot)
{
    uint32_t row = s->slots[slot].row;
    uint32_t last = s->row_count - 1u;
    uint32_t mask = s->component_mask[row];
    uint32_t gen;
    uint32_t t;
    int i;

    for (t = 0; mask != 0; ++t, mask >>= 1) {
        if (mask & 1u) {
            remove_component_at(s, &s->components[t], slot);
        }
    }
    if (row != last) {
        for (i = 0; i < COLUMN_COUNT; ++i) {
            memcpy(column_at(s, &COLUMNS[i], row), column_at(s, &COLUMNS[i], last),
                   COLUMNS[i].elem_size);
        }
        s->slots[s->slot_of_row[row]].row = row;
    }
    s->row_count--;

    gen = s->slots[slot].generation;
    if (gen == UINT32_MAX) {
        s->slots[slot].row = AX_NODE_SLOT_RETIRED; /* never reused, so never matched again */
        return;
    }
    s->slots[slot].generation = gen + 1u;
    s->slots[slot].row = AX_NODE_NONE;
    if (s->on_slot_freed != NULL) {
        ax_node next;
        next.h = ax_handle_make(slot, gen + 1u);
        s->on_slot_freed(s->user, next);
    } else {
        s->slots[slot].next_free = s->free_head;
        s->free_head = slot;
    }
}

ax_result ax_node_store_destroy(ax_node_store *s, ax_node node)
{
    uint32_t root;
    uint32_t cur;
    CHECK_THREAD(s);
    if (ax_node_store_row(s, node) == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    root = ax_handle_slot(node.h);
    unlink_slot(s, root);
    /* Post-order without a stack: descend to the first leaf, free it (it is its parent's first
     * child), then continue with its next sibling, or with the parent once it has no children. */
    cur = root;
    for (;;) {
        ax_node_links *l = links_of_slot(s, cur);
        uint32_t parent;
        uint32_t next;
        if (l->first_child != AX_NODE_NONE) {
            cur = l->first_child;
            continue;
        }
        if (cur == root) {
            free_node(s, cur);
            break;
        }
        parent = l->parent;
        next = l->next_sibling;
        {
            ax_node_links *p = links_of_slot(s, parent);
            p->first_child = next;
            if (next == AX_NODE_NONE) {
                p->last_child = AX_NODE_NONE;
            } else {
                links_of_slot(s, next)->prev_sibling = AX_NODE_NONE;
            }
        }
        free_node(s, cur);
        cur = next != AX_NODE_NONE ? next : parent;
    }
    return AX_OK;
}

bool ax_node_store_is_live(const ax_node_store *s, ax_node node)
{
    CHECK_THREAD(s);
    return ax_node_store_row(s, node) != AX_NODE_NONE;
}

/* ---- Navigation ------------------------------------------------------------------------------ */

static ax_node link_handle(const ax_node_store *s, ax_node node, size_t field)
{
    ax_node none = AX_HANDLE_NULL_INIT;
    uint32_t row = ax_node_store_row(s, node);
    uint32_t slot;
    CHECK_THREAD(s);
    if (row == AX_NODE_NONE) {
        return none;
    }
    memcpy(&slot, (const unsigned char *)&s->links[row] + field, sizeof slot);
    return ax_node_store_handle(s, slot);
}

ax_node ax_node_store_parent(const ax_node_store *s, ax_node node)
{
    return link_handle(s, node, offsetof(ax_node_links, parent));
}

ax_node ax_node_store_first_child(const ax_node_store *s, ax_node node)
{
    return link_handle(s, node, offsetof(ax_node_links, first_child));
}

ax_node ax_node_store_last_child(const ax_node_store *s, ax_node node)
{
    return link_handle(s, node, offsetof(ax_node_links, last_child));
}

ax_node ax_node_store_next_sibling(const ax_node_store *s, ax_node node)
{
    return link_handle(s, node, offsetof(ax_node_links, next_sibling));
}

ax_node ax_node_store_prev_sibling(const ax_node_store *s, ax_node node)
{
    return link_handle(s, node, offsetof(ax_node_links, prev_sibling));
}

/* ---- Hierarchy ------------------------------------------------------------------------------- */

static bool is_ancestor_or_self_slot(ax_node_store *s, uint32_t ancestor, uint32_t slot)
{
    while (slot != AX_NODE_NONE) {
        if (slot == ancestor) {
            return true;
        }
        slot = links_of_slot(s, slot)->parent;
    }
    return false;
}

bool ax_node_store_is_ancestor_or_self(const ax_node_store *s, ax_node ancestor, ax_node node)
{
    CHECK_THREAD(s);
    if (ax_node_store_row(s, ancestor) == AX_NODE_NONE ||
        ax_node_store_row(s, node) == AX_NODE_NONE) {
        return false;
    }
    return is_ancestor_or_self_slot((ax_node_store *)s, ax_handle_slot(ancestor.h),
                                    ax_handle_slot(node.h));
}

ax_result ax_node_store_insert_before(ax_node_store *s, ax_node parent, ax_node child,
                                      ax_node before)
{
    uint32_t p;
    uint32_t c;
    uint32_t b = AX_NODE_NONE;
    ax_node_links *cl;
    CHECK_THREAD(s);
    if (ax_node_store_row(s, parent) == AX_NODE_NONE ||
        ax_node_store_row(s, child) == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    p = ax_handle_slot(parent.h);
    c = ax_handle_slot(child.h);
    if (!AX_HANDLE_IS_NULL(before)) {
        if (ax_node_store_row(s, before) == AX_NODE_NONE) {
            return AX_ERR_INVALID_HANDLE;
        }
        b = ax_handle_slot(before.h);
        if (b == c || links_of_slot(s, b)->parent != p) {
            return AX_ERR_INVALID_ARGUMENT;
        }
    }
    if (is_ancestor_or_self_slot(s, c, p)) {
        return AX_ERR_INVALID_ARGUMENT; /* parent is child or inside its subtree */
    }
    cl = links_of_slot(s, c);
    if (cl->parent == p && cl->next_sibling == b) {
        return AX_OK; /* already there */
    }
    unlink_slot(s, c);
    link_slot(s, p, c, b);
    return AX_OK;
}

ax_result ax_node_store_append_child(ax_node_store *s, ax_node parent, ax_node child)
{
    ax_node none = AX_HANDLE_NULL_INIT;
    return ax_node_store_insert_before(s, parent, child, none);
}

ax_result ax_node_store_detach(ax_node_store *s, ax_node node)
{
    CHECK_THREAD(s);
    if (ax_node_store_row(s, node) == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    unlink_slot(s, ax_handle_slot(node.h));
    return AX_OK;
}

/* ---- Compaction ------------------------------------------------------------------------------ */

ax_result ax_node_store_compact(ax_node_store *s)
{
    const column *cols = COLUMNS;
    uint32_t *order; /* new row -> slot */
    unsigned char *scratch;
    size_t max_elem = 0;
    uint32_t n = s->row_count;
    uint32_t emitted = 0;
    uint32_t row;
    int i;

    CHECK_THREAD(s);
    if (n < 2u) {
        return AX_OK;
    }
    for (i = 0; i < COLUMN_COUNT; ++i) {
        max_elem = cols[i].elem_size > max_elem ? cols[i].elem_size : max_elem;
    }
    order = (uint32_t *)ax_mem_alloc(&s->allocator, sizeof(uint32_t) * n, AX_ALIGNOF(uint32_t));
    scratch = (unsigned char *)ax_mem_alloc(&s->allocator, max_elem * n, 16);
    if (order == NULL || scratch == NULL) {
        ax_mem_free(&s->allocator, order, sizeof(uint32_t) * n);
        ax_mem_free(&s->allocator, scratch, max_elem * n);
        return AX_ERR_OUT_OF_MEMORY;
    }

    /* Pre-order of every tree, roots in current row order. */
    for (row = 0; row < n; ++row) {
        uint32_t root;
        uint32_t cur;
        if (s->links[row].parent != AX_NODE_NONE) {
            continue;
        }
        root = s->slot_of_row[row];
        cur = root;
        for (;;) {
            const ax_node_links *l = links_of_slot(s, cur);
            order[emitted++] = cur;
            if (l->first_child != AX_NODE_NONE) {
                cur = l->first_child;
                continue;
            }
            while (cur != root && links_of_slot(s, cur)->next_sibling == AX_NODE_NONE) {
                cur = links_of_slot(s, cur)->parent;
            }
            if (cur == root) {
                break;
            }
            cur = links_of_slot(s, cur)->next_sibling;
        }
    }
    AX_ASSERT_MSG(emitted == n, "node store: rows not reachable from a root");

    /* Permute every column through the scratch buffer. The slot table still maps each slot to
     * its old row until the end. */
    for (i = 0; i < COLUMN_COUNT; ++i) {
        size_t sz = cols[i].elem_size;
        for (row = 0; row < n; ++row) {
            memcpy(scratch + (size_t)row * sz, column_at(s, &cols[i], s->slots[order[row]].row),
                   sz);
        }
        memcpy(column_base(s, &cols[i]), scratch, sz * n);
    }
    for (row = 0; row < n; ++row) {
        s->slots[s->slot_of_row[row]].row = row;
    }

    ax_mem_free(&s->allocator, order, sizeof(uint32_t) * n);
    ax_mem_free(&s->allocator, scratch, max_elem * n);
    return AX_OK;
}

/* ---- Side components ------------------------------------------------------------------------- */

ax_result ax_node_store_register_component(ax_node_store *s, const ax_component_desc *desc,
                                           ax_component_type *out_type)
{
    ax_component_storage *cs;
    CHECK_THREAD(s);
    AX_ASSERT(desc != NULL && out_type != NULL);
    if (desc->size == 0 || !ax_is_pow2(desc->align)) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    if (s->component_type_count >= AX_NODE_MAX_COMPONENT_TYPES) {
        return AX_ERR_UNSUPPORTED;
    }
    cs = &s->components[s->component_type_count];
    memset(cs, 0, sizeof *cs);
    cs->desc = *desc;
    cs->stride = ax_align_up(desc->size, desc->align);
    *out_type = s->component_type_count++;
    return AX_OK;
}

/* Element index + 1 for a slot, 0 when none. */
static uint32_t sparse_get(const ax_component_storage *cs, uint32_t slot)
{
    uint32_t page = slot >> SPARSE_PAGE_SHIFT;
    if (page >= cs->sparse_pages || cs->sparse[page] == NULL) {
        return 0;
    }
    return cs->sparse[page][slot & (SPARSE_PAGE_SIZE - 1u)];
}

/* The sparse entry for a slot, allocating its page. NULL when out of memory. */
static uint32_t *sparse_entry(ax_node_store *s, ax_component_storage *cs, uint32_t slot)
{
    uint32_t page = slot >> SPARSE_PAGE_SHIFT;
    if (page >= cs->sparse_pages) {
        uint32_t pages = cs->sparse_pages < 4u ? 4u : cs->sparse_pages;
        uint32_t **fresh;
        while (pages <= page) {
            pages *= 2u;
        }
        fresh = (uint32_t **)ax_mem_alloc(&s->allocator, sizeof(uint32_t *) * pages,
                                          AX_ALIGNOF(uint32_t *));
        if (fresh == NULL) {
            return NULL;
        }
        memset(fresh, 0, sizeof(uint32_t *) * pages);
        if (cs->sparse != NULL) {
            memcpy(fresh, cs->sparse, sizeof(uint32_t *) * cs->sparse_pages);
            ax_mem_free(&s->allocator, cs->sparse, sizeof(uint32_t *) * cs->sparse_pages);
        }
        cs->sparse = fresh;
        cs->sparse_pages = pages;
    }
    if (cs->sparse[page] == NULL) {
        cs->sparse[page] = (uint32_t *)ax_mem_alloc(
            &s->allocator, sizeof(uint32_t) * SPARSE_PAGE_SIZE, AX_ALIGNOF(uint32_t));
        if (cs->sparse[page] == NULL) {
            return NULL;
        }
        memset(cs->sparse[page], 0, sizeof(uint32_t) * SPARSE_PAGE_SIZE);
    }
    return &cs->sparse[page][slot & (SPARSE_PAGE_SIZE - 1u)];
}

static bool reserve_elements(ax_node_store *s, ax_component_storage *cs, uint32_t needed)
{
    uint32_t cap;
    unsigned char *data;
    uint32_t *owners;
    if (needed <= cs->capacity) {
        return true;
    }
    cap = cs->capacity < 16u ? 16u : cs->capacity;
    while (cap < needed) {
        cap *= 2u;
    }
    data = (unsigned char *)ax_mem_alloc(&s->allocator, cs->stride * cap, cs->desc.align);
    owners = (uint32_t *)ax_mem_alloc(&s->allocator, sizeof(uint32_t) * cap, AX_ALIGNOF(uint32_t));
    if (data == NULL || owners == NULL) {
        ax_mem_free(&s->allocator, data, cs->stride * cap);
        ax_mem_free(&s->allocator, owners, sizeof(uint32_t) * cap);
        return false;
    }
    if (cs->count > 0) {
        memcpy(data, cs->data, cs->stride * cs->count);
        memcpy(owners, cs->owner_slot, sizeof(uint32_t) * cs->count);
    }
    ax_mem_free(&s->allocator, cs->data, cs->stride * cs->capacity);
    ax_mem_free(&s->allocator, cs->owner_slot, sizeof(uint32_t) * cs->capacity);
    cs->data = data;
    cs->owner_slot = owners;
    cs->capacity = cap;
    return true;
}

ax_result ax_node_store_add_component(ax_node_store *s, ax_node node, ax_component_type type,
                                      void **out_element)
{
    ax_component_storage *cs;
    uint32_t row = ax_node_store_row(s, node);
    uint32_t slot = ax_handle_slot(node.h);
    uint32_t existing;
    uint32_t *entry;
    unsigned char *elem;
    CHECK_THREAD(s);
    if (out_element != NULL) {
        *out_element = NULL;
    }
    if (row == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    if (type >= s->component_type_count) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    cs = &s->components[type];
    existing = sparse_get(cs, slot);
    if (existing != 0) {
        if (out_element != NULL) {
            *out_element = cs->data + (size_t)(existing - 1u) * cs->stride;
        }
        return AX_OK;
    }
    if (!reserve_elements(s, cs, cs->count + 1u)) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    entry = sparse_entry(s, cs, slot);
    if (entry == NULL) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    elem = cs->data + (size_t)cs->count * cs->stride;
    memset(elem, 0, cs->stride);
    cs->owner_slot[cs->count] = slot;
    cs->count++;
    *entry = cs->count;
    s->component_mask[row] |= 1u << type;
    if (out_element != NULL) {
        *out_element = elem;
    }
    return AX_OK;
}

void *ax_node_store_get_component(const ax_node_store *s, ax_node node, ax_component_type type)
{
    const ax_component_storage *cs;
    uint32_t index;
    CHECK_THREAD(s);
    if (ax_node_store_row(s, node) == AX_NODE_NONE || type >= s->component_type_count) {
        return NULL;
    }
    cs = &s->components[type];
    index = sparse_get(cs, ax_handle_slot(node.h));
    return index == 0 ? NULL : cs->data + (size_t)(index - 1u) * cs->stride;
}

void *ax_node_store_peek_component(const ax_node_store *s, uint32_t slot, ax_component_type type)
{
    const ax_component_storage *cs;
    uint32_t index;
    if (slot >= s->slot_count || s->slots[slot].row >= AX_NODE_SLOT_RETIRED ||
        type >= s->component_type_count) {
        return NULL;
    }
    cs = &s->components[type];
    index = sparse_get(cs, slot);
    return index == 0 ? NULL : cs->data + (size_t)(index - 1u) * cs->stride;
}

/* Removes the slot's element (which must exist): on_remove, then swap-remove. Does not touch the
 * node's component mask. */
static void remove_component_at(ax_node_store *s, ax_component_storage *cs, uint32_t slot)
{
    uint32_t page = slot >> SPARSE_PAGE_SHIFT;
    uint32_t *entry = &cs->sparse[page][slot & (SPARSE_PAGE_SIZE - 1u)];
    uint32_t index = *entry - 1u;
    uint32_t last = cs->count - 1u;
    (void)s;
    if (cs->desc.on_remove != NULL) {
        cs->desc.on_remove(cs->data + (size_t)index * cs->stride, cs->desc.user);
    }
    if (index != last) {
        uint32_t moved = cs->owner_slot[last];
        memcpy(cs->data + (size_t)index * cs->stride, cs->data + (size_t)last * cs->stride,
               cs->stride);
        cs->owner_slot[index] = moved;
        cs->sparse[moved >> SPARSE_PAGE_SHIFT][moved & (SPARSE_PAGE_SIZE - 1u)] = index + 1u;
    }
    *entry = 0;
    cs->count--;
}

ax_result ax_node_store_remove_component(ax_node_store *s, ax_node node, ax_component_type type)
{
    uint32_t row = ax_node_store_row(s, node);
    CHECK_THREAD(s);
    if (row == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    if (type >= s->component_type_count || (s->component_mask[row] & (1u << type)) == 0) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    remove_component_at(s, &s->components[type], ax_handle_slot(node.h));
    s->component_mask[row] &= ~(1u << type);
    return AX_OK;
}
