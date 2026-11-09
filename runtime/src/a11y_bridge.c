/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The accessibility bridge of a window (decision 108): the root's tree, as core publishes it
 * (ax_a11y_update, on the scene thread), into the platform's adapter (ax_a11y_adapter, AccessKit
 * on Linux), whose activations and actions come back from the adapter's threads as
 * ax_world_a11y_activate and ax_world_post_a11y_action: queued for the scene thread, which a frame
 * request wakes. Nothing here touches the node store.
 */
#include <string.h>

#include "rt_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_a11y.h>
#include <axion/platform/ax_a11y_adapter.h>

/* The adapter's flags are core's. */
AX_STATIC_ASSERT(AX_A11Y_TREE_DISABLED == AX_A11Y_DISABLED &&
                     AX_A11Y_TREE_SELECTED == AX_A11Y_SELECTED &&
                     AX_A11Y_TREE_FOCUSABLE == AX_A11Y_FOCUSABLE &&
                     AX_A11Y_TREE_HAS_VALUE == AX_A11Y_HAS_VALUE &&
                     AX_A11Y_TREE_HAS_MIN == AX_A11Y_HAS_MIN &&
                     AX_A11Y_TREE_HAS_MAX == AX_A11Y_HAS_MAX &&
                     AX_A11Y_TREE_HAS_STEP == AX_A11Y_HAS_STEP,
                 "the adapter's node flags are core's");

struct rt_a11y_bridge {
    ax_world world;
    ax_node root;
    ax_a11y_adapter *adapter;
    ax_a11y_tree_node *nodes; /* scene thread: the update being converted */
    uint32_t capacity;
};

/* Adapter threads. */
static void on_activated(void *user, bool active)
{
    rt_a11y_bridge *b = (rt_a11y_bridge *)user;
    ax_world_a11y_activate(b->world, b->root, active);
}

static void on_action(void *user, uint64_t target, uint32_t action, float value)
{
    rt_a11y_bridge *b = (rt_a11y_bridge *)user;
    ax_a11y_action a;
    memset(&a, 0, sizeof a);
    a.root = b->root;
    a.target = target;
    a.action = action;
    a.value = value;
    (void)ax_world_post_a11y_action(b->world, &a);
}

rt_a11y_bridge *ax__rt_a11y_bridge_create(ax_world world, ax_window window, ax_node root)
{
    rt_a11y_bridge *b;
    ax_a11y_adapter_desc d;
    if (!ax_a11y_adapter_available()) {
        return NULL;
    }
    b = (rt_a11y_bridge *)ax_mem_alloc(ax_default_allocator(), sizeof *b,
                                       AX_ALIGNOF(rt_a11y_bridge));
    if (b == NULL) {
        return NULL;
    }
    memset(b, 0, sizeof *b);
    b->world = world;
    b->root = root;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.root_id = root.h;
    d.activated = on_activated;
    d.action = on_action;
    d.user = b;
    if (ax_a11y_adapter_create(window, &d, &b->adapter) != AX_OK) {
        ax_mem_free(ax_default_allocator(), b, sizeof *b);
        return NULL; /* no assistive technology bridge: the app runs the same */
    }
    return b;
}

void ax__rt_a11y_bridge_destroy(rt_a11y_bridge *b)
{
    if (b == NULL) {
        return;
    }
    ax_a11y_adapter_destroy(b->adapter); /* no callback after this */
    ax_mem_free(ax_default_allocator(), b->nodes, sizeof(ax_a11y_tree_node) * b->capacity);
    ax_mem_free(ax_default_allocator(), b, sizeof *b);
}

void ax__rt_a11y_bridge_window_focused(rt_a11y_bridge *b, bool focused)
{
    if (b != NULL) {
        ax_a11y_adapter_set_window_focused(b->adapter, focused);
    }
}

/* The surface's sink: core's update in the adapter's types. Scene thread. */
void ax__rt_a11y_bridge_sink(void *user, const ax_a11y_update *u)
{
    rt_a11y_bridge *b = (rt_a11y_bridge *)user;
    ax_a11y_tree_update t;
    uint32_t i;
    if (u->count > b->capacity) {
        uint32_t cap = b->capacity < 256u ? 256u : b->capacity;
        ax_a11y_tree_node *fresh;
        while (cap < u->count) {
            cap *= 2u;
        }
        fresh = (ax_a11y_tree_node *)ax_mem_alloc(
            ax_default_allocator(), sizeof(ax_a11y_tree_node) * cap, AX_ALIGNOF(ax_a11y_tree_node));
        if (fresh == NULL) {
            return; /* the adapter misses this update; the next activation is whole again */
        }
        ax_mem_free(ax_default_allocator(), b->nodes, sizeof(ax_a11y_tree_node) * b->capacity);
        b->nodes = fresh;
        b->capacity = cap;
    }
    for (i = 0; i < u->count; ++i) {
        const ax_a11y_node *s = &u->nodes[i];
        ax_a11y_tree_node *d = &b->nodes[i];
        memset(d, 0, sizeof *d);
        d->id = s->id;
        d->role = s->role;
        d->flags = s->flags;
        d->checked = s->checked;
        d->expanded = s->expanded;
        d->actions = s->actions;
        d->level = s->level;
        d->name = s->name;
        d->description = s->description;
        d->value_text = s->value_text;
        d->value = s->value;
        d->value_min = s->value_min;
        d->value_max = s->value_max;
        d->value_step = s->value_step;
        d->bounds[0] = s->bounds.x;
        d->bounds[1] = s->bounds.y;
        d->bounds[2] = s->bounds.w;
        d->bounds[3] = s->bounds.h;
        d->transform[0] = s->transform.a;
        d->transform[1] = s->transform.b;
        d->transform[2] = s->transform.c;
        d->transform[3] = s->transform.d;
        d->transform[4] = s->transform.tx;
        d->transform[5] = s->transform.ty;
        d->children = s->children;
        d->child_count = s->child_count;
        d->text = s->text;
        d->char_count = s->char_count;
        d->char_lengths = s->char_lengths;
        d->char_positions = s->char_positions;
        d->char_widths = s->char_widths;
        d->word_count = s->word_count;
        d->word_lengths = s->word_lengths;
    }
    memset(&t, 0, sizeof t);
    t.root = u->root.h;
    t.focus = u->focus;
    t.full = u->full;
    t.nodes = b->nodes;
    t.count = u->count;
    ax_a11y_adapter_update(b->adapter, &t);
}
