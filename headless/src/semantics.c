/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The headless host's semantic tree (decision 108): a sink per surface that applies the
 * accessibility tree's updates as AccessKit would (a node replaced whole, what the root no longer
 * reaches dropped), and the testing API's queries and dump over that copy. The sink runs on the
 * scene thread; the queries on the app thread, both under the host's lock.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "hl_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_a11y.h>

typedef struct hl_node {
    uint64_t id;
    uint32_t role, flags, checked, expanded, actions, level;
    float value, value_min, value_max;
    ax_rect bounds;
    ax_affine2 transform;
    char *strings; /* name, description, value text, test id, run text: each NUL-terminated */
    size_t strings_size;
    uint32_t len[5];
    uint64_t *kids;
    uint32_t kid_count;
} hl_node;

enum { S_NAME, S_DESCRIPTION, S_VALUE_TEXT, S_TEST_ID, S_TEXT };

struct hl_tree {
    const ax_allocator *allocator;
    ax_mutex *lock;
    hl_node *nodes;
    uint32_t count, capacity;
    uint32_t *index; /* open addressing: node index + 1, 0 empty */
    uint32_t index_capacity;
    uint64_t root, focus;
    bool has_tree;
    uint64_t *stack;
    uint32_t stack_capacity;
};

/* ---- Storage --------------------------------------------------------------------------------- */

static uint32_t hash_id(uint64_t id)
{
    id ^= id >> 33;
    id *= 0xff51afd7ed558ccdull;
    id ^= id >> 33;
    return (uint32_t)id;
}

static hl_node *find(hl_tree *t, uint64_t id)
{
    uint32_t i;
    if (t->index_capacity == 0) {
        return NULL;
    }
    for (i = hash_id(id) & (t->index_capacity - 1u);; i = (i + 1u) & (t->index_capacity - 1u)) {
        const uint32_t k = t->index[i];
        if (k == 0) {
            return NULL;
        }
        if (t->nodes[k - 1u].id == id) {
            return &t->nodes[k - 1u];
        }
    }
}

static bool rehash(hl_tree *t, uint32_t capacity)
{
    uint32_t *fresh =
        (uint32_t *)ax_mem_alloc(t->allocator, sizeof(uint32_t) * capacity, AX_ALIGNOF(uint32_t));
    uint32_t n;
    if (fresh == NULL) {
        return false;
    }
    memset(fresh, 0, sizeof(uint32_t) * capacity);
    for (n = 0; n < t->count; ++n) {
        uint32_t i = hash_id(t->nodes[n].id) & (capacity - 1u);
        while (fresh[i] != 0) {
            i = (i + 1u) & (capacity - 1u);
        }
        fresh[i] = n + 1u;
    }
    ax_mem_free(t->allocator, t->index, sizeof(uint32_t) * t->index_capacity);
    t->index = fresh;
    t->index_capacity = capacity;
    return true;
}

static void free_node(hl_tree *t, hl_node *n)
{
    ax_mem_free(t->allocator, n->strings, n->strings_size);
    ax_mem_free(t->allocator, n->kids, sizeof(uint64_t) * n->kid_count);
    n->strings = NULL;
    n->kids = NULL;
}

static void clear(hl_tree *t)
{
    uint32_t i;
    for (i = 0; i < t->count; ++i) {
        free_node(t, &t->nodes[i]);
    }
    t->count = 0;
    if (t->index != NULL) {
        memset(t->index, 0, sizeof(uint32_t) * t->index_capacity);
    }
}

hl_tree *ax__hl_tree_create(const ax_allocator *allocator, ax_mutex *lock)
{
    hl_tree *t = (hl_tree *)ax_mem_alloc(allocator, sizeof *t, AX_ALIGNOF(hl_tree));
    if (t != NULL) {
        memset(t, 0, sizeof *t);
        t->allocator = allocator;
        t->lock = lock;
    }
    return t;
}

void ax__hl_tree_destroy(hl_tree *t)
{
    if (t == NULL) {
        return;
    }
    clear(t);
    ax_mem_free(t->allocator, t->nodes, sizeof(hl_node) * t->capacity);
    ax_mem_free(t->allocator, t->index, sizeof(uint32_t) * t->index_capacity);
    ax_mem_free(t->allocator, t->stack, sizeof(uint64_t) * t->stack_capacity);
    ax_mem_free(t->allocator, t, sizeof *t);
}

static bool push(hl_tree *t, uint32_t *depth, uint64_t id)
{
    if (*depth == t->stack_capacity) {
        const uint32_t cap = t->stack_capacity < 64u ? 64u : t->stack_capacity * 2u;
        uint64_t *fresh =
            (uint64_t *)ax_mem_alloc(t->allocator, sizeof(uint64_t) * cap, AX_ALIGNOF(uint64_t));
        if (fresh == NULL) {
            return false;
        }
        if (t->stack != NULL) {
            memcpy(fresh, t->stack, sizeof(uint64_t) * *depth);
            ax_mem_free(t->allocator, t->stack, sizeof(uint64_t) * t->stack_capacity);
        }
        t->stack = fresh;
        t->stack_capacity = cap;
    }
    t->stack[(*depth)++] = id;
    return true;
}

/* Drops what the root no longer reaches (AccessKit's rule), once it is most of the copy. */
static void prune(hl_tree *t)
{
    uint32_t depth = 0, reached = 0, i, kept = 0;
    uint8_t *mark;
    hl_node *r = find(t, t->root);
    if (r == NULL) {
        clear(t);
        return;
    }
    mark = (uint8_t *)ax_mem_alloc(t->allocator, t->count, 1);
    if (mark == NULL) {
        return;
    }
    memset(mark, 0, t->count);
    if (!push(t, &depth, t->root)) {
        ax_mem_free(t->allocator, mark, t->count);
        return;
    }
    while (depth > 0) {
        hl_node *n = find(t, t->stack[--depth]);
        uint32_t k;
        if (n == NULL || mark[n - t->nodes]) {
            continue;
        }
        mark[n - t->nodes] = 1;
        reached++;
        for (k = 0; k < n->kid_count; ++k) {
            if (!push(t, &depth, n->kids[k])) {
                ax_mem_free(t->allocator, mark, t->count);
                return;
            }
        }
    }
    if (reached == t->count) {
        ax_mem_free(t->allocator, mark, t->count);
        return;
    }
    for (i = 0; i < t->count; ++i) {
        if (mark[i]) {
            t->nodes[kept++] = t->nodes[i];
        } else {
            free_node(t, &t->nodes[i]);
        }
    }
    ax_mem_free(t->allocator, mark, t->count);
    t->count = kept;
    (void)rehash(t, t->index_capacity);
}

static void put(hl_tree *t, const ax_a11y_node *s)
{
    hl_node *n = find(t, s->id);
    const ax_str strs[5] = {s->name, s->description, s->value_text, s->test_id, s->text};
    size_t size = 0;
    uint32_t k;
    char *at;
    if (n == NULL) {
        if (t->count == t->capacity) {
            const uint32_t cap = t->capacity < 64u ? 64u : t->capacity * 2u;
            hl_node *fresh =
                (hl_node *)ax_mem_alloc(t->allocator, sizeof(hl_node) * cap, AX_ALIGNOF(hl_node));
            if (fresh == NULL) {
                return;
            }
            if (t->nodes != NULL) {
                memcpy(fresh, t->nodes, sizeof(hl_node) * t->count);
                ax_mem_free(t->allocator, t->nodes, sizeof(hl_node) * t->capacity);
            }
            t->nodes = fresh;
            t->capacity = cap;
        }
        if ((t->count + 1u) * 2u > t->index_capacity &&
            !rehash(t, t->index_capacity < 128u ? 128u : t->index_capacity * 2u)) {
            return;
        }
        n = &t->nodes[t->count++];
        memset(n, 0, sizeof *n);
        n->id = s->id;
        {
            uint32_t i = hash_id(s->id) & (t->index_capacity - 1u);
            while (t->index[i] != 0) {
                i = (i + 1u) & (t->index_capacity - 1u);
            }
            t->index[i] = (uint32_t)(n - t->nodes) + 1u;
        }
    } else {
        free_node(t, n);
    }
    n->role = s->role;
    n->flags = s->flags;
    n->checked = s->checked;
    n->expanded = s->expanded;
    n->actions = s->actions;
    n->level = s->level;
    n->value = s->value;
    n->value_min = s->value_min;
    n->value_max = s->value_max;
    n->bounds = s->bounds;
    n->transform = s->transform;
    for (k = 0; k < 5; ++k) {
        size += strs[k].len + 1u;
    }
    n->strings = (char *)ax_mem_alloc(t->allocator, size, 1);
    n->strings_size = n->strings != NULL ? size : 0;
    at = n->strings;
    for (k = 0; k < 5 && at != NULL; ++k) {
        n->len[k] = (uint32_t)strs[k].len;
        if (strs[k].len > 0) {
            memcpy(at, strs[k].ptr, strs[k].len);
        }
        at[strs[k].len] = '\0';
        at += strs[k].len + 1u;
    }
    n->kid_count = 0;
    if (s->child_count > 0) {
        n->kids = (uint64_t *)ax_mem_alloc(t->allocator, sizeof(uint64_t) * s->child_count,
                                           AX_ALIGNOF(uint64_t));
        if (n->kids != NULL) {
            memcpy(n->kids, s->children, sizeof(uint64_t) * s->child_count);
            n->kid_count = s->child_count;
        }
    }
}

static const char *str_of(const hl_node *n, int which)
{
    const char *p = n->strings;
    int k;
    if (p == NULL) {
        return "";
    }
    for (k = 0; k < which; ++k) {
        p += n->len[k] + 1u;
    }
    return p;
}

void ax__hl_tree_update(void *user, const ax_a11y_update *u)
{
    hl_tree *t = (hl_tree *)user;
    uint32_t i;
    ax_mutex_lock(t->lock);
    if (u->full) {
        clear(t);
    }
    t->root = u->root.h;
    t->focus = u->focus;
    t->has_tree = true;
    for (i = 0; i < u->count; ++i) {
        put(t, &u->nodes[i]);
    }
    if (u->count > 0) {
        prune(t);
    }
    ax_mutex_unlock(t->lock);
}

/* ---- Matching -------------------------------------------------------------------------------- */

static void trim(const char **s, size_t *n)
{
    while (*n > 0 && isspace((unsigned char)(*s)[0])) {
        (*s)++;
        (*n)--;
    }
    while (*n > 0 && isspace((unsigned char)(*s)[*n - 1u])) {
        (*n)--;
    }
}

static bool chars_equal(char a, char b, bool ci)
{
    if (ci) {
        a = (char)tolower((unsigned char)a);
        b = (char)tolower((unsigned char)b);
    }
    return a == b;
}

static bool matches(const char *have, size_t have_len, const char *want, uint32_t match)
{
    size_t want_len = strlen(want), i, k;
    const bool ci = (match & AX_HEADLESS_MATCH_IGNORE_CASE) != 0;
    trim(&have, &have_len);
    trim(&want, &want_len);
    if ((match & AX_HEADLESS_MATCH_SUBSTRING) == 0) {
        if (have_len != want_len) {
            return false;
        }
        for (i = 0; i < have_len; ++i) {
            if (!chars_equal(have[i], want[i], ci)) {
                return false;
            }
        }
        return true;
    }
    if (want_len == 0) {
        return true;
    }
    for (i = 0; i + want_len <= have_len; ++i) {
        for (k = 0; k < want_len && chars_equal(have[i + k], want[k], ci); ++k) {
        }
        if (k == want_len) {
            return true;
        }
    }
    return false;
}

static bool is_text_leaf(hl_tree *t, const hl_node *n)
{
    const hl_node *first;
    if (n->kid_count == 0) {
        return false; /* an empty text leaf has no lines either: nothing to match */
    }
    first = find(t, n->kids[0]);
    return first != NULL && first->role == AX_ROLE_TEXT_RUN;
}

/* A text leaf's text: its lines' text, joined. */
static size_t leaf_text(hl_tree *t, const hl_node *n, char *buf, size_t cap)
{
    size_t len = 0;
    uint32_t k;
    for (k = 0; k < n->kid_count; ++k) {
        const hl_node *run = find(t, n->kids[k]);
        size_t m;
        if (run == NULL) {
            continue;
        }
        m = run->len[S_TEXT];
        if (len + m > cap) {
            m = cap - len;
        }
        memcpy(buf + len, str_of(run, S_TEXT), m);
        len += m;
    }
    return len;
}

typedef enum query_kind { BY_ROLE, BY_TEXT, BY_TEST_ID } query_kind;

static uint32_t query(ax_headless *h, ax_node root, query_kind kind, uint32_t role,
                      const char *want, uint32_t match, ax_node *out, uint32_t capacity)
{
    hl_tree *t = ax__hl_tree_of(h, root);
    uint32_t depth = 0, found = 0;
    static char text[4096];
    if (t == NULL) {
        return 0;
    }
    ax_mutex_lock(t->lock);
    if (!t->has_tree || !push(t, &depth, t->root)) {
        ax_mutex_unlock(t->lock);
        return 0;
    }
    while (depth > 0) {
        const hl_node *n = find(t, t->stack[--depth]);
        bool hit = false;
        uint32_t k;
        if (n == NULL || n->role == AX_ROLE_TEXT_RUN) {
            continue;
        }
        switch (kind) {
        case BY_ROLE:
            hit = n->role == role &&
                  (want == NULL || matches(str_of(n, S_NAME), n->len[S_NAME], want, match));
            break;
        case BY_TEXT:
            if (is_text_leaf(t, n)) {
                const size_t len = leaf_text(t, n, text, sizeof text);
                hit = matches(text, len, want, match);
            }
            break;
        case BY_TEST_ID:
            hit = n->len[S_TEST_ID] > 0 && strcmp(str_of(n, S_TEST_ID), want) == 0;
            break;
        }
        if (hit) {
            if (found < capacity) {
                out[found].h = n->id;
            }
            found++;
        }
        /* Children in reverse, so they come out in tree order. */
        for (k = n->kid_count; k > 0; --k) {
            if (!push(t, &depth, n->kids[k - 1u])) {
                break;
            }
        }
    }
    ax_mutex_unlock(t->lock);
    return found;
}

uint32_t ax_headless_get_by_role(ax_headless *h, ax_node root, uint32_t role, const char *name,
                                 uint32_t match, ax_node *out, uint32_t capacity)
{
    return query(h, root, BY_ROLE, role, name, match, out, capacity);
}

uint32_t ax_headless_get_by_text(ax_headless *h, ax_node root, const char *text, uint32_t match,
                                 ax_node *out, uint32_t capacity)
{
    return text == NULL ? 0 : query(h, root, BY_TEXT, 0, text, match, out, capacity);
}

uint32_t ax_headless_get_by_test_id(ax_headless *h, ax_node root, const char *test_id, ax_node *out,
                                    uint32_t capacity)
{
    return test_id == NULL ? 0 : query(h, root, BY_TEST_ID, 0, test_id, 0, out, capacity);
}

/* ---- Elements -------------------------------------------------------------------------------- */

/* The node's transform to the window (its ancestors' transforms times its own), searching from
 * the root. False when the root does not reach it. */
static bool find_transform(hl_tree *t, uint64_t at, uint64_t id, ax_affine2 parent, uint32_t guard,
                           ax_affine2 *out)
{
    const hl_node *n = find(t, at);
    ax_affine2 m;
    uint32_t k;
    if (n == NULL || guard > 4096u) {
        return false;
    }
    m = ax_affine2_mul(parent, n->transform);
    if (n->id == id) {
        *out = m;
        return true;
    }
    for (k = 0; k < n->kid_count; ++k) {
        if (find_transform(t, n->kids[k], id, m, guard + 1u, out)) {
            return true;
        }
    }
    return false;
}

static bool window_transform(hl_tree *t, uint64_t id, ax_affine2 *out)
{
    return find_transform(t, t->root, id, ax_affine2_identity(), 0, out);
}

static ax_rectf box_of(ax_affine2 m, ax_rect b)
{
    ax_rectf r;
    const float xs[4] = {b.x, b.x + b.w, b.x, b.x + b.w};
    const float ys[4] = {b.y, b.y, b.y + b.h, b.y + b.h};
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int i;
    for (i = 0; i < 4; ++i) {
        const float x = m.a * xs[i] + m.c * ys[i] + m.tx;
        const float y = m.b * xs[i] + m.d * ys[i] + m.ty;
        if (i == 0 || x < x0) {
            x0 = x;
        }
        if (i == 0 || x > x1) {
            x1 = x;
        }
        if (i == 0 || y < y0) {
            y0 = y;
        }
        if (i == 0 || y > y1) {
            y1 = y;
        }
    }
    r.x = x0;
    r.y = y0;
    r.w = x1 - x0;
    r.h = y1 - y0;
    return r;
}

ax_result ax_headless_get_element(ax_headless *h, ax_node root, ax_node element,
                                  ax_headless_element *out)
{
    hl_tree *t = ax__hl_tree_of(h, root);
    const hl_node *n;
    ax_affine2 m;
    if (out == NULL || out->struct_size < sizeof(ax_headless_element)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_headless_get_element: struct_size");
    }
    if (t == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    ax_mutex_lock(t->lock);
    n = find(t, element.h);
    if (n == NULL || !window_transform(t, element.h, &m)) {
        ax_mutex_unlock(t->lock);
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless_get_element: not in the tree");
    }
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->role = n->role;
    out->flags = n->flags;
    out->actions = n->actions;
    out->checked = n->checked;
    out->expanded = n->expanded;
    out->level = n->level;
    out->value = n->value;
    out->value_min = n->value_min;
    out->value_max = n->value_max;
    out->bounds = box_of(m, n->bounds);
    out->focused = t->focus == n->id;
    ax_mutex_unlock(t->lock);
    return AX_OK;
}

ax_result ax_headless_get_name(ax_headless *h, ax_node root, ax_node element, char *buffer,
                               size_t capacity, size_t *out_length)
{
    hl_tree *t = ax__hl_tree_of(h, root);
    const hl_node *n;
    if (t == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    ax_mutex_lock(t->lock);
    n = find(t, element.h);
    if (n == NULL) {
        ax_mutex_unlock(t->lock);
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless_get_name: not in the tree");
    }
    if (out_length != NULL) {
        *out_length = n->len[S_NAME];
    }
    if (capacity > 0) {
        const size_t m = n->len[S_NAME] < capacity - 1u ? n->len[S_NAME] : capacity - 1u;
        memcpy(buffer, str_of(n, S_NAME), m);
        buffer[m] = '\0';
    }
    ax_mutex_unlock(t->lock);
    return AX_OK;
}

ax_result ax_headless_a11y_action(ax_headless *h, ax_node root, ax_node element, uint32_t action,
                                  float value)
{
    ax_a11y_action a;
    if (ax__hl_tree_of(h, root) == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    memset(&a, 0, sizeof a);
    a.root = root;
    a.target = element.h;
    a.action = action;
    a.value = value;
    if (!ax_world_post_a11y_action(ax_headless_world(h), &a)) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_headless_a11y_action: the queue is full");
    }
    return AX_OK;
}

/* ---- The dump -------------------------------------------------------------------------------- */

typedef struct dumper {
    ax_ir_write_fn write;
    void *ctx;
    char line[2048];
    size_t len;
} dumper;

static void out_raw(dumper *d, const char *s, size_t n)
{
    if (d->len + n >= sizeof d->line) {
        n = sizeof d->line - 1u - d->len;
    }
    memcpy(d->line + d->len, s, n);
    d->len += n;
}

static void out_str(dumper *d, const char *s)
{
    out_raw(d, s, strlen(s));
}

static void out_fmt(dumper *d, const char *fmt, double a, double b, double c, double e)
{
    char buf[128];
    const int n = snprintf(buf, sizeof buf, fmt, a, b, c, e);
    if (n > 0) {
        out_raw(d, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1u);
    }
}

/* A quoted string, with quotes, backslashes and line breaks escaped. */
static void out_quoted(dumper *d, const char *s, size_t n)
{
    size_t i;
    out_raw(d, "\"", 1);
    for (i = 0; i < n; ++i) {
        if (s[i] == '"' || s[i] == '\\') {
            out_raw(d, "\\", 1);
            out_raw(d, s + i, 1);
        } else if (s[i] == '\n') {
            out_raw(d, "\\n", 2);
        } else {
            out_raw(d, s + i, 1);
        }
    }
    out_raw(d, "\"", 1);
}

static void flush_line(dumper *d)
{
    out_raw(d, "\n", 1);
    d->write(d->ctx, d->line, d->len);
    d->len = 0;
}

static void dump_line(hl_tree *t, dumper *d, const hl_node *n, uint32_t depth, ax_affine2 m,
                      uint32_t flags)
{
    static const char *const ACTIONS[] = {"focus", "default", "set_value", "increment",
                                          "decrement"};
    uint32_t i;
    bool first = true;
    for (i = 0; i < depth; ++i) {
        out_raw(d, "  ", 2);
    }
    out_str(d, ax_role_name(n->role));
    if (n->len[S_NAME] > 0) {
        out_raw(d, " ", 1);
        out_quoted(d, str_of(n, S_NAME), n->len[S_NAME]);
    }
    if (n->level > 0) {
        out_fmt(d, " level=%g", n->level, 0, 0, 0);
    }
    if (n->flags & AX_A11Y_HAS_VALUE) {
        out_fmt(d, " value=%g", (double)n->value, 0, 0, 0);
        if ((n->flags & (AX_A11Y_HAS_MIN | AX_A11Y_HAS_MAX)) ==
            (AX_A11Y_HAS_MIN | AX_A11Y_HAS_MAX)) {
            out_fmt(d, " range=%g..%g", (double)n->value_min, (double)n->value_max, 0, 0);
        }
    }
    if (n->len[S_VALUE_TEXT] > 0) {
        out_str(d, " value-text=");
        out_quoted(d, str_of(n, S_VALUE_TEXT), n->len[S_VALUE_TEXT]);
    }
    if (n->checked != AX_TOGGLED_NONE) {
        out_str(d, n->checked == AX_TOGGLED_TRUE    ? " checked"
                   : n->checked == AX_TOGGLED_MIXED ? " checked=mixed"
                                                    : " unchecked");
    }
    if (n->expanded != AX_EXPANDED_NONE) {
        out_str(d, n->expanded == AX_EXPANDED_EXPANDED ? " expanded" : " collapsed");
    }
    if (n->flags & AX_A11Y_SELECTED) {
        out_str(d, " selected");
    }
    if (n->flags & AX_A11Y_DISABLED) {
        out_str(d, " disabled");
    }
    if (t->focus == n->id && n->id != t->root) {
        out_str(d, " focused");
    }
    for (i = 0; i < 5; ++i) {
        if (n->actions & (1u << i)) {
            out_str(d, first ? " actions=" : ",");
            out_str(d, ACTIONS[i]);
            first = false;
        }
    }
    if (n->len[S_TEST_ID] > 0) {
        out_str(d, " test-id=");
        out_quoted(d, str_of(n, S_TEST_ID), n->len[S_TEST_ID]);
    }
    if (n->len[S_DESCRIPTION] > 0) {
        out_str(d, " description=");
        out_quoted(d, str_of(n, S_DESCRIPTION), n->len[S_DESCRIPTION]);
    }
    if (n->role == AX_ROLE_TEXT_RUN) {
        out_str(d, " text=");
        out_quoted(d, str_of(n, S_TEXT), n->len[S_TEXT]);
    }
    if (flags & AX_HEADLESS_DUMP_BOUNDS) {
        const ax_rectf b = box_of(m, n->bounds);
        out_fmt(d, " @%g,%g %gx%g", (double)b.x, (double)b.y, (double)b.w, (double)b.h);
    }
    flush_line(d);
}

static void dump_node(hl_tree *t, dumper *d, uint64_t id, uint32_t depth, ax_affine2 parent,
                      uint32_t flags, uint32_t guard)
{
    const hl_node *n = find(t, id);
    ax_affine2 m;
    bool shown;
    uint32_t k;
    if (n == NULL || guard > 4096u) {
        return;
    }
    m = ax_affine2_mul(parent, n->transform);
    shown = (flags & AX_HEADLESS_DUMP_ALL) != 0 ||
            (n->role != AX_ROLE_GENERIC && n->role != AX_ROLE_TEXT_RUN);
    if (shown) {
        dump_line(t, d, n, depth, m, flags);
    }
    if (n->role != AX_ROLE_TEXT_RUN && (flags & AX_HEADLESS_DUMP_ALL) == 0 && is_text_leaf(t, n)) {
        return; /* a text leaf's lines: only with DUMP_ALL */
    }
    for (k = 0; k < n->kid_count; ++k) {
        dump_node(t, d, n->kids[k], shown ? depth + 1u : depth, m, flags, guard + 1u);
    }
}

ax_result ax_headless_dump_semantics(ax_headless *h, ax_node root, uint32_t flags,
                                     ax_ir_write_fn write, void *ctx)
{
    hl_tree *t = ax__hl_tree_of(h, root);
    dumper d;
    if (t == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_headless: no surface for this root");
    }
    memset(&d, 0, sizeof d);
    d.write = write;
    d.ctx = ctx;
    ax_mutex_lock(t->lock);
    if (!t->has_tree || find(t, t->root) == NULL) {
        ax_mutex_unlock(t->lock);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_headless_dump_semantics: no tree yet");
    }
    dump_node(t, &d, t->root, 0, ax_affine2_identity(), flags, 0);
    ax_mutex_unlock(t->lock);
    return AX_OK;
}
