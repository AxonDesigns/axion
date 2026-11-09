/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The accessibility tree (decision 108): activation, minimal diffs, the tree a consumer rebuilds
 * from them, text runs, names from content, target geometry, actions, and no cost while inactive.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_a11y.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/render/ax_render_ir.h"

/* ---- A consumer: applies updates as AccessKit would ------------------------------------------ */

#define MAX_NODES 256
#define MAX_KIDS  32

typedef struct c_node {
    uint64_t id;
    uint32_t role, flags, actions, checked;
    char name[128];
    ax_rect bounds;
    ax_affine2 transform;
    uint64_t kids[MAX_KIDS];
    uint32_t kid_count;
    uint32_t chars, words;
    uint32_t char_bytes; /* sum of character lengths */
    uint32_t word_chars; /* sum of word lengths */
    char text[128];
} c_node;

typedef struct consumer {
    c_node nodes[MAX_NODES];
    uint32_t count;
    uint64_t root, focus;
    uint32_t updates;
    uint32_t last_count; /* nodes in the last update */
    bool last_full;
    uint64_t last_ids[MAX_NODES];
} consumer;

static c_node *find(consumer *c, uint64_t id)
{
    uint32_t i;
    for (i = 0; i < c->count; ++i) {
        if (c->nodes[i].id == id) {
            return &c->nodes[i];
        }
    }
    return NULL;
}

static void copy_str(char *dst, size_t cap, ax_str s)
{
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (n > 0) {
        memcpy(dst, s.ptr, n);
    }
    dst[n] = '\0';
}

/* Keeps only what is reachable from the root (AccessKit drops the rest). */
static void prune(consumer *c)
{
    uint64_t keep[MAX_NODES];
    uint32_t nk = 0, i, k;
    c_node fresh[MAX_NODES];
    uint32_t nf = 0;
    if (find(c, c->root) == NULL) {
        c->count = 0;
        return;
    }
    keep[nk++] = c->root;
    for (i = 0; i < nk; ++i) {
        c_node *n = find(c, keep[i]);
        if (n == NULL) {
            continue;
        }
        for (k = 0; k < n->kid_count && nk < MAX_NODES; ++k) {
            keep[nk++] = n->kids[k];
        }
    }
    for (i = 0; i < c->count; ++i) {
        for (k = 0; k < nk; ++k) {
            if (keep[k] == c->nodes[i].id) {
                fresh[nf++] = c->nodes[i];
                break;
            }
        }
    }
    memcpy(c->nodes, fresh, sizeof(c_node) * nf);
    c->count = nf;
}

static void consume(consumer *c, const ax_a11y_update *u)
{
    uint32_t i, k;
    if (u->full) {
        c->count = 0;
    }
    c->root = u->root.h;
    c->focus = u->focus;
    c->updates++;
    c->last_count = u->count;
    c->last_full = u->full;
    for (i = 0; i < u->count; ++i) {
        const ax_a11y_node *s = &u->nodes[i];
        c_node *n = find(c, s->id);
        if (i < MAX_NODES) {
            c->last_ids[i] = s->id;
        }
        if (n == NULL) {
            if (c->count == MAX_NODES) {
                continue;
            }
            n = &c->nodes[c->count++];
        }
        memset(n, 0, sizeof *n);
        n->id = s->id;
        n->role = s->role;
        n->flags = s->flags;
        n->actions = s->actions;
        n->checked = s->checked;
        copy_str(n->name, sizeof n->name, s->name);
        copy_str(n->text, sizeof n->text, s->text);
        n->bounds = s->bounds;
        n->transform = s->transform;
        n->kid_count = s->child_count < MAX_KIDS ? s->child_count : MAX_KIDS;
        for (k = 0; k < n->kid_count; ++k) {
            n->kids[k] = s->children[k];
        }
        n->chars = s->char_count;
        n->words = s->word_count;
        for (k = 0; k < s->char_count; ++k) {
            n->char_bytes += s->char_lengths[k];
        }
        for (k = 0; k < s->word_count; ++k) {
            n->word_chars += s->word_lengths[k];
        }
    }
    prune(c);
}

typedef struct sinks {
    consumer diffs; /* every update */
    consumer full;  /* only full ones, from empty */
} sinks;

static void on_update(void *user, const ax_a11y_update *u)
{
    sinks *s = (sinks *)user;
    consume(&s->diffs, u);
    if (u->full) {
        memset(&s->full, 0, sizeof s->full);
        consume(&s->full, u);
    }
}

/* The tree as indented text, for comparisons. */
static void dump_node(consumer *c, uint64_t id, int depth, char *out, size_t cap, size_t *len)
{
    c_node *n = find(c, id);
    uint32_t k;
    int w;
    if (n == NULL || *len >= cap) {
        return;
    }
    w = snprintf(out + *len, cap - *len,
                 "%*s%u \"%s\" f%x a%x [%g %g %g %g | %g %g %g %g %g %g] c%u w%u %s\n", depth * 2,
                 "", n->role, n->name, n->flags, n->actions, (double)n->bounds.x,
                 (double)n->bounds.y, (double)n->bounds.w, (double)n->bounds.h,
                 (double)n->transform.a, (double)n->transform.b, (double)n->transform.c,
                 (double)n->transform.d, (double)n->transform.tx, (double)n->transform.ty, n->chars,
                 n->words, n->text);
    if (w > 0) {
        *len += (size_t)w;
    }
    for (k = 0; k < n->kid_count; ++k) {
        dump_node(c, n->kids[k], depth + 1, out, cap, len);
    }
}

static void dump(consumer *c, char *out, size_t cap)
{
    size_t len = 0;
    out[0] = '\0';
    dump_node(c, c->root, 0, out, cap, &len);
}

/* ---- A world and its frames ------------------------------------------------------------------ */

typedef struct fixture {
    ax_world w;
    uint64_t now;
    ax_node root;
    sinks s;
    uint32_t wakes;
} fixture;

static uint64_t read_clock(void *user)
{
    return ((fixture *)user)->now;
}

static void on_wake(void *user, ax_node root)
{
    (void)root;
    ((fixture *)user)->wakes++;
}

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static void place(ax_txn *t, ax_node n, float x, float y, float w, float h)
{
    ax_txn_set_u32(t, n, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_dimension(t, n, AX_PROP_LEFT, pt(x));
    ax_txn_set_dimension(t, n, AX_PROP_TOP, pt(y));
    ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(w));
    ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, pt(h));
}

static void label(ax_txn *t, ax_node n, const char *s)
{
    ax_txn_set_str(t, n, AX_PROP_LABEL, s, strlen(s));
}

/* One frame in the runtime's order, the tree's flush after recording. */
static void frame(fixture *f)
{
    ax_ir_builder b;
    const ax_render_ir *ir = NULL;
    f->now += 16666667u;
    (void)ax_world_apply_pending(f->w, UINT64_MAX);
    (void)ax_world_set_root_viewport(f->w, f->root, 400.0f, 300.0f, 1.0f);
    (void)ax_world_position(f->w);
    (void)ax_world_animate(f->w, f->now);
    ax_world_record_collect(f->w);
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 400.0f, 300.0f, 0, ax_color_make(0, 0, 0, 1));
    if (ax_world_root_changed(f->w, f->root, NULL)) {
        (void)ax_world_record_root(f->w, f->root, &b);
    }
    (void)ax_ir_builder_end(&b, &ir);
    ax_ir_builder_deinit(&b);
    ax_world_a11y_flush(f->w);
    (void)ax_world_publish_mirror(f->w);
    ax_scene_clear_dirty(ax_world_scene(f->w));
}

static void commit(ax_txn *t)
{
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
}

static void fixture_init(fixture *f)
{
    ax_world_desc d;
    ax_txn *t;
    memset(f, 0, sizeof *f);
    f->now = 1000000000u;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.clock = read_clock;
    d.clock_user = f;
    AX_REQUIRE(ax_world_create(&d, &f->w) == AX_OK);
    ax_world_set_wake_hook(f->w, on_wake, f);
    t = ax_txn_begin(f->w);
    f->root = ax_txn_create(t, AX_NODE_VIEW);
    label(t, f->root, "Window");
    commit(t);
    frame(f);
}

static void attach(fixture *f)
{
    ax_a11y_sink sink;
    sink.update = on_update;
    sink.user = &f->s;
    AX_REQUIRE(ax_world_set_a11y_sink(f->w, f->root, &sink) == AX_OK);
}

static void fixture_deinit(fixture *f)
{
    AX_CHECK_RESULT(ax_world_destroy(f->w), AX_OK);
}

static ax_a11y_counters counters(fixture *f)
{
    ax_a11y_counters c;
    ax_world_get_a11y_counters(f->w, &c, true);
    return c;
}

/* Deactivates and activates again: the consumer of full updates gets the tree from scratch, which
 * the diffs must have reproduced. */
static bool diffs_match_a_full_build(fixture *f)
{
    static char a[16384], b[16384];
    dump(&f->s.diffs, a, sizeof a);
    ax_world_a11y_activate(f->w, f->root, false);
    frame(f);
    ax_world_a11y_activate(f->w, f->root, true);
    frame(f);
    dump(&f->s.full, b, sizeof b);
    if (strcmp(a, b) != 0) {
        printf("diffs:\n%s\nfull build:\n%s\n", a, b);
        return false;
    }
    return true;
}

/* root: list (10, 10, 200x200) with items a, b (b has a child), and a hidden node. */
typedef struct tree {
    ax_node list, a, b, b1, hidden;
} tree;

static void build_tree(fixture *f, tree *n)
{
    ax_txn *t = ax_txn_begin(f->w);
    n->list = ax_txn_create(t, AX_NODE_VIEW);
    n->a = ax_txn_create(t, AX_NODE_VIEW);
    n->b = ax_txn_create(t, AX_NODE_VIEW);
    n->b1 = ax_txn_create(t, AX_NODE_VIEW);
    n->hidden = ax_txn_create(t, AX_NODE_VIEW);
    place(t, n->list, 10, 10, 200, 200);
    ax_txn_set_u32(t, n->list, AX_PROP_ROLE, AX_ROLE_LIST);
    place(t, n->a, 0, 0, 200, 40);
    ax_txn_set_u32(t, n->a, AX_PROP_ROLE, AX_ROLE_LIST_ITEM);
    label(t, n->a, "First");
    place(t, n->b, 0, 50, 200, 40);
    ax_txn_set_u32(t, n->b, AX_PROP_ROLE, AX_ROLE_LIST_ITEM);
    label(t, n->b, "Second");
    place(t, n->b1, 4, 4, 20, 20);
    ax_txn_set_u32(t, n->b1, AX_PROP_ROLE, AX_ROLE_IMAGE);
    label(t, n->b1, "Avatar");
    ax_txn_set_bool(t, n->hidden, AX_PROP_HIDDEN, true);
    ax_txn_append_child(t, f->root, n->list);
    ax_txn_append_child(t, n->list, n->a);
    ax_txn_append_child(t, n->list, n->b);
    ax_txn_append_child(t, n->b, n->b1);
    ax_txn_append_child(t, f->root, n->hidden);
    commit(t);
}

/* ---- Tests ----------------------------------------------------------------------------------- */

AX_TEST(an_inactive_tree_costs_nothing_and_activation_builds_it_once)
{
    static fixture f;
    tree n;
    ax_a11y_counters c;
    uint32_t i, wakes;
    static char out[4096];
    fixture_init(&f);
    build_tree(&f, &n);
    frame(&f);
    attach(&f);
    /* A sink alone is not an active tree: changes queue nothing, flushes build nothing. */
    for (i = 0; i < 5; ++i) {
        ax_txn *t = ax_txn_begin(f.w);
        ax_txn_set_dimension(t, n.a, AX_PROP_WIDTH, pt(100.0f + (float)i));
        label(t, n.b, i % 2 ? "x" : "y");
        commit(t);
        frame(&f);
    }
    c = counters(&f);
    AX_CHECK_EQ_U(c.touched, 0);
    AX_CHECK_EQ_U(c.built, 0);
    AX_CHECK_EQ_U(f.s.diffs.updates, 0);
    AX_CHECK(!ax_world_a11y_enabled(f.w));

    /* Activation (any thread) asks for a frame; the next flush sends the whole tree. */
    wakes = f.wakes;
    ax_world_a11y_activate(f.w, f.root, true);
    AX_CHECK_EQ_U(f.wakes, wakes + 1u);
    frame(&f);
    AX_CHECK(ax_world_a11y_enabled(f.w));
    AX_CHECK_EQ_U(f.s.diffs.updates, 1);
    AX_CHECK(f.s.diffs.last_full);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 5); /* root, list, a, b, b1: not the hidden node */
    c = counters(&f);
    AX_CHECK_EQ_U(c.full_builds, 1);
    AX_CHECK_EQ_U(c.sent, 5);
    AX_CHECK_EQ_U(f.s.diffs.focus, f.root.h); /* nothing focused: the root */
    dump(&f.s.diffs, out, sizeof out);
    AX_CHECK(strcmp(out, "2 \"Window\" f0 a0 [0 0 400 300 | 1 0 0 1 0 0] c0 w0 \n"
                         "  16 \"\" f0 a0 [0 0 200 200 | 1 0 0 1 10 10] c0 w0 \n"
                         "    17 \"First\" f0 a0 [0 0 104 40 | 1 0 0 1 0 0] c0 w0 \n"
                         "    17 \"y\" f0 a0 [0 0 200 40 | 1 0 0 1 0 50] c0 w0 \n"
                         "      9 \"Avatar\" f0 a0 [0 0 20 20 | 1 0 0 1 4 4] c0 w0 \n") == 0);
    if (strstr(out, "First") == NULL) {
        printf("%s", out);
    }
    /* Nothing changed: the next frame sends nothing. */
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.updates, 1);
    fixture_deinit(&f);
}

AX_TEST(a_change_to_one_node_sends_that_node_only)
{
    static fixture f;
    tree n;
    ax_txn *t;
    ax_a11y_counters c;
    fixture_init(&f);
    build_tree(&f, &n);
    attach(&f);
    ax_world_a11y_activate(f.w, f.root, true);
    frame(&f);
    (void)counters(&f);

    /* A label: that node. */
    t = ax_txn_begin(f.w);
    label(t, n.a, "Renamed");
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(f.s.diffs.last_ids[0], n.a.h);
    AX_CHECK(strcmp(find(&f.s.diffs, n.a.h)->name, "Renamed") == 0);

    /* Paint only: rebuilt, compared, nothing sent. */
    t = ax_txn_begin(f.w);
    ax_txn_set_color(t, n.a, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb8(255, 0, 0, 255));
    commit(t);
    (void)counters(&f);
    frame(&f);
    c = counters(&f);
    AX_CHECK_EQ_U(c.built, 1);
    AX_CHECK_EQ_U(c.sent, 0);
    AX_CHECK_EQ_U(f.s.diffs.updates, 2);

    /* A frame: that node (its bounds), not its parent. */
    t = ax_txn_begin(f.w);
    ax_txn_set_dimension(t, n.b1, AX_PROP_WIDTH, pt(30.0f));
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(f.s.diffs.last_ids[0], n.b1.h);

    /* A reorder (absolute boxes keep their frames): the parent, with its children in order. */
    t = ax_txn_begin(f.w);
    ax_txn_insert_before(t, n.list, n.b, n.a);
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(f.s.diffs.last_ids[0], n.list.h);
    AX_CHECK_EQ_U(find(&f.s.diffs, n.list.h)->kids[0], n.b.h);

    /* A state and an action set. */
    t = ax_txn_begin(f.w);
    ax_txn_set_bool(t, n.b, AX_PROP_SELECTED, true);
    ax_txn_set_u32(t, n.b, AX_PROP_ACTIONS, AX_ACTION_FOCUS | AX_ACTION_DEFAULT);
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(find(&f.s.diffs, n.b.h)->flags, AX_A11Y_SELECTED | AX_A11Y_FOCUSABLE);
    AX_CHECK(diffs_match_a_full_build(&f));
    fixture_deinit(&f);
}

AX_TEST(removed_hidden_and_new_subtrees_update_their_parent)
{
    static fixture f;
    tree n;
    ax_txn *t;
    ax_node fresh, fresh_kid;
    fixture_init(&f);
    build_tree(&f, &n);
    attach(&f);
    ax_world_a11y_activate(f.w, f.root, true);
    frame(&f);

    /* Removing b (and b1): the list only; the consumer drops the subtree. */
    t = ax_txn_begin(f.w);
    ax_txn_remove(t, n.b);
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(f.s.diffs.last_ids[0], n.list.h);
    AX_CHECK(find(&f.s.diffs, n.b1.h) == NULL);
    AX_CHECK(diffs_match_a_full_build(&f));

    /* Hiding a: the list only. Showing it again: a and the list. */
    t = ax_txn_begin(f.w);
    ax_txn_set_bool(t, n.a, AX_PROP_HIDDEN, true);
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(f.s.diffs.last_ids[0], n.list.h);
    AX_CHECK(find(&f.s.diffs, n.a.h) == NULL);
    t = ax_txn_begin(f.w);
    ax_txn_set_bool(t, n.a, AX_PROP_HIDDEN, false);
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 2);
    AX_CHECK(find(&f.s.diffs, n.a.h) != NULL);

    /* DISPLAY none takes the hidden node's place: a new subtree under it is not sent. */
    t = ax_txn_begin(f.w);
    fresh = ax_txn_create(t, AX_NODE_VIEW);
    fresh_kid = ax_txn_create(t, AX_NODE_VIEW);
    label(t, fresh, "New");
    label(t, fresh_kid, "Kid");
    ax_txn_append_child(t, fresh, fresh_kid);
    ax_txn_append_child(t, n.list, fresh);
    ax_txn_set_u32(t, n.a, AX_PROP_DISPLAY, AX_DISPLAY_NONE);
    commit(t);
    frame(&f);
    AX_CHECK(find(&f.s.diffs, fresh_kid.h) != NULL); /* the new subtree, whole */
    AX_CHECK(find(&f.s.diffs, n.a.h) == NULL);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 3); /* fresh, its kid, the list */
    AX_CHECK(diffs_match_a_full_build(&f));

    /* A reparented node moves with its subtree: both parents. */
    t = ax_txn_begin(f.w);
    ax_txn_append_child(t, f.root, fresh);
    commit(t);
    frame(&f);
    AX_CHECK(find(&f.s.diffs, fresh_kid.h) != NULL);
    AX_CHECK(diffs_match_a_full_build(&f));

    /* Destroying the whole list. */
    t = ax_txn_begin(f.w);
    ax_txn_remove(t, n.list);
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_EQ_U(f.s.diffs.last_ids[0], f.root.h);
    AX_CHECK(diffs_match_a_full_build(&f));
    fixture_deinit(&f);
}

AX_TEST(text_leaves_have_lines_with_characters_and_words)
{
    static fixture f;
    ax_txn *t;
    ax_node para, button, caption;
    c_node *p;
    uint32_t i, runs = 0, chars = 0, bytes = 0;
    static const char TEXT[] = "Lines break at word boundaries and wrap again when narrower.";
    fixture_init(&f);
    t = ax_txn_begin(f.w);
    para = ax_txn_create(t, AX_NODE_VIEW);
    button = ax_txn_create(t, AX_NODE_VIEW);
    caption = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_dimension(t, para, AX_PROP_WIDTH, pt(120.0f));
    ax_txn_set_str(t, para, AX_PROP_TEXT, TEXT, strlen(TEXT));
    ax_txn_set_u32(t, button, AX_PROP_ROLE, AX_ROLE_BUTTON);
    ax_txn_set_str(t, caption, AX_PROP_TEXT, "Tapped 0 times", 14);
    ax_txn_append_child(t, f.root, para);
    ax_txn_append_child(t, f.root, button);
    ax_txn_append_child(t, button, caption);
    commit(t);
    attach(&f);
    ax_world_a11y_activate(f.w, f.root, true);
    frame(&f);

    p = find(&f.s.diffs, para.h);
    AX_REQUIRE(p != NULL);
    AX_CHECK_EQ_U(p->role, AX_ROLE_LABEL);
    AX_CHECK(strcmp(p->name, TEXT) == 0);
    AX_CHECK(p->kid_count > 1); /* it wraps at 120 */
    for (i = 0; i < p->kid_count; ++i) {
        const c_node *run = find(&f.s.diffs, p->kids[i]);
        AX_REQUIRE(run != NULL);
        AX_CHECK((run->id & AX_A11Y_RUN_BIT) != 0);
        AX_CHECK_EQ_U(run->role, AX_ROLE_TEXT_RUN);
        AX_CHECK_EQ_U(run->char_bytes, (uint32_t)strlen(run->text));
        AX_CHECK_EQ_U(run->word_chars, run->chars);
        AX_CHECK(run->words >= 1u);
        AX_CHECK(run->bounds.w > 0.0f && run->bounds.w <= 120.0f);
        chars += run->chars;
        bytes += run->char_bytes;
        runs++;
    }
    AX_CHECK_EQ_U(bytes, (uint32_t)strlen(TEXT));
    AX_CHECK(chars > 0 && runs == p->kid_count);

    /* A button is named by its content. */
    AX_CHECK(strcmp(find(&f.s.diffs, button.h)->name, "Tapped 0 times") == 0);
    t = ax_txn_begin(f.w);
    ax_txn_set_str(t, caption, AX_PROP_TEXT, "Tapped 1 times", 14);
    commit(t);
    frame(&f);
    AX_CHECK(strcmp(find(&f.s.diffs, button.h)->name, "Tapped 1 times") == 0);

    /* Narrower: the lines change (more of them), the leaf with them. */
    t = ax_txn_begin(f.w);
    ax_txn_set_dimension(t, para, AX_PROP_WIDTH, pt(60.0f));
    commit(t);
    frame(&f);
    AX_CHECK(find(&f.s.diffs, para.h)->kid_count > runs);
    AX_CHECK(diffs_match_a_full_build(&f));
    fixture_deinit(&f);
}

AX_TEST(geometry_is_the_target_frame_and_transform)
{
    static fixture f;
    ax_txn *t;
    ax_node spinner;
    c_node *s;
    ax_transform2d rot = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f}; /* 90 degrees */
    ax_transition tr;
    ax_a11y_counters c;
    uint32_t i;
    fixture_init(&f);
    t = ax_txn_begin(f.w);
    spinner = ax_txn_create(t, AX_NODE_VIEW);
    place(t, spinner, 100, 50, 40, 20);
    ax_txn_set_transform(t, spinner, AX_PROP_TRANSFORM, rot);
    ax_txn_append_child(t, f.root, spinner);
    commit(t);
    attach(&f);
    ax_world_a11y_activate(f.w, f.root, true);
    frame(&f);
    s = find(&f.s.diffs, spinner.h);
    AX_REQUIRE(s != NULL);
    /* translate(100, 50) * [rotate 90 about (20, 10)]: the box's center stays at (120, 60). */
    AX_CHECK(s->bounds.w == 40.0f && s->bounds.h == 20.0f);
    {
        const float cx = s->transform.a * 20.0f + s->transform.c * 10.0f + s->transform.tx;
        const float cy = s->transform.b * 20.0f + s->transform.d * 10.0f + s->transform.ty;
        AX_CHECK_NEAR_F(cx, 120.0f, 1e-4f);
        AX_CHECK_NEAR_F(cy, 60.0f, 1e-4f);
        AX_CHECK_NEAR_F(s->transform.b, 1.0f, 1e-6f);
    }

    /* An animated move: one update with the target, then nothing while it runs. */
    memset(&tr, 0, sizeof tr);
    tr.struct_size = sizeof tr;
    tr.kind = AX_TRANSITION_TIMING;
    tr.duration = 0.5f;
    tr.easing = AX_EASING_CUBIC_BEZIER;
    tr.curve[2] = 1.0f;
    tr.curve[3] = 1.0f;
    t = ax_txn_begin(f.w);
    ax_txn_set_animated(t, true);
    ax_txn_set_transition(t, spinner, AX_PROP_LAYOUT, &tr);
    ax_txn_set_dimension(t, spinner, AX_PROP_LEFT, pt(200.0f));
    commit(t);
    frame(&f);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 1);
    AX_CHECK_NEAR_F(find(&f.s.diffs, spinner.h)->transform.tx, 200.0f + 20.0f + 10.0f, 1e-4f);
    (void)counters(&f);
    for (i = 0; i < 10; ++i) {
        frame(&f); /* the animation runs: presentation values only */
    }
    c = counters(&f);
    AX_CHECK_EQ_U(c.sent, 0);
    AX_CHECK_EQ_U(c.updates, 0);
    fixture_deinit(&f);
}

typedef struct poster {
    ax_world w;
    ax_node root, target;
} poster;

static int post_from_thread(void *arg)
{
    poster *p = (poster *)arg;
    ax_a11y_action a;
    memset(&a, 0, sizeof a);
    a.root = p->root;
    a.target = p->target.h;
    a.action = AX_ACTION_FOCUS;
    (void)ax_world_post_a11y_action(p->w, &a);
    a.action = AX_ACTION_DEFAULT;
    (void)ax_world_post_a11y_action(p->w, &a);
    a.action = AX_ACTION_SET_VALUE; /* not one of its actions: dropped */
    a.value = 3.0f;
    (void)ax_world_post_a11y_action(p->w, &a);
    return 0;
}

AX_TEST(actions_from_another_thread_reach_the_app_and_move_the_focus)
{
    static fixture f;
    tree n;
    ax_txn *t;
    poster p;
    ax_thread th;
    ax_world_event ev;
    ax_a11y_counters c;
    uint32_t wakes, i;
    ax_a11y_action a;
    fixture_init(&f);
    build_tree(&f, &n);
    t = ax_txn_begin(f.w);
    ax_txn_set_u32(t, n.a, AX_PROP_ROLE, AX_ROLE_BUTTON);
    ax_txn_set_u32(t, n.a, AX_PROP_ACTIONS, AX_ACTION_FOCUS | AX_ACTION_DEFAULT);
    commit(t);
    attach(&f);
    ax_world_a11y_activate(f.w, f.root, true);
    frame(&f);

    p.w = f.w;
    p.root = f.root;
    p.target = n.a;
    wakes = f.wakes;
    if (ax_threads_supported()) {
        AX_REQUIRE(ax_thread_create(&th, post_from_thread, &p) == AX_OK);
        (void)ax_thread_join(&th);
    } else {
        (void)post_from_thread(&p); /* the single-threaded web build */
    }
    AX_CHECK(f.wakes >= wakes + 3u); /* each asked for a frame */
    frame(&f);
    c = counters(&f);
    AX_CHECK_EQ_U(c.actions, 2);
    AX_CHECK_EQ_U(c.actions_dropped, 1);
    AX_CHECK_EQ_U(f.s.diffs.focus, n.a.h); /* the focus moved, in an update of its own */
    AX_CHECK_EQ_U(f.s.diffs.last_count, 0);

    memset(&ev, 0, sizeof ev);
    ev.struct_size = sizeof ev;
    AX_REQUIRE(ax_world_poll_event(f.w, &ev) == AX_OK);
    AX_CHECK_EQ_U(ev.type, AX_WORLD_EVENT_A11Y_ACTION);
    AX_CHECK_EQ_U(ev.data.a11y_action.node, n.a.h);
    AX_CHECK_EQ_U(ev.data.a11y_action.action, AX_ACTION_FOCUS);
    AX_CHECK_EQ_U(ev.root, f.root.h);
    AX_REQUIRE(ax_world_poll_event(f.w, &ev) == AX_OK);
    AX_CHECK_EQ_U(ev.data.a11y_action.action, AX_ACTION_DEFAULT);
    AX_REQUIRE(ax_world_poll_event(f.w, &ev) == AX_OK);
    AX_CHECK_EQ_U(ev.type, AX_WORLD_EVENT_NONE);

    /* The queue holds AX_A11Y_ACTION_QUEUE actions between flushes. */
    memset(&a, 0, sizeof a);
    a.root = f.root;
    a.target = n.a.h;
    a.action = AX_ACTION_DEFAULT;
    for (i = 0; i < AX_A11Y_ACTION_QUEUE; ++i) {
        AX_CHECK(ax_world_post_a11y_action(f.w, &a));
    }
    AX_CHECK(!ax_world_post_a11y_action(f.w, &a));
    frame(&f);
    c = counters(&f);
    AX_CHECK_EQ_U(c.actions, AX_A11Y_ACTION_QUEUE);
    AX_CHECK_EQ_U(c.actions_dropped, 1);
    fixture_deinit(&f);
}

AX_TEST(a_semantics_only_change_records_no_items)
{
    static fixture f;
    tree n;
    ax_txn *t;
    ax_frame_counters fc;
    fixture_init(&f);
    build_tree(&f, &n);
    attach(&f);
    ax_world_a11y_activate(f.w, f.root, true);
    frame(&f);
    ax_world_get_frame_counters(f.w, &fc, true);
    t = ax_txn_begin(f.w);
    label(t, n.a, "Only semantics");
    ax_txn_set_u32(t, n.b, AX_PROP_CHECKED, AX_TOGGLED_TRUE);
    commit(t);
    (void)ax_world_apply_pending(f.w, UINT64_MAX);
    ax_world_record_collect(f.w);
    AX_CHECK(!ax_world_root_changed(f.w, f.root, NULL)); /* no new snapshot */
    frame(&f);
    ax_world_get_frame_counters(f.w, &fc, true);
    AX_CHECK_EQ_U(fc.items_recorded, 0);
    AX_CHECK_EQ_U(fc.nodes_recorded, 0);
    AX_CHECK_EQ_U(fc.snapshots, 0);
    AX_CHECK_EQ_U(f.s.diffs.last_count, 2);
    fixture_deinit(&f);
}
