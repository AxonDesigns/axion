/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The style pass (decisions 63, 64 and 99): computed values of inherited text properties.
 *
 * A node's computed value is its own when it set the property, else its nearest ancestor's with
 * the text component, else the default. Only nodes with the text component store computed values;
 * nodes in between inherit transparently. The pass visits the component's elements whose node is
 * marked STYLE (setting an inherited property marks the subtree, moving a node to another parent
 * marks its subtree, a new node is marked) and resolves each one's ancestors first when they are
 * also marked and not yet resolved in this pass, so every chain resolves top-down exactly once.
 */
#include <string.h>

#include "world_internal.h"

#include <axion/core/ax_scene.h>
#include <axion/core/ax_text_node.h>

static bool same_str(const ax_shared_str *a, const ax_shared_str *b)
{
    ax_str x = ax_shared_str_view(a), y = ax_shared_str_view(b);
    return x.len == y.len && (x.len == 0 || memcmp(x.ptr, y.ptr, x.len) == 0);
}

static void set_str(const ax_shared_str **slot, const ax_shared_str *value)
{
    if (*slot != value) {
        ax_shared_str_retain(value);
        ax_shared_str_release(*slot);
        *slot = value;
    }
}

/* The defaults, from the registry's descriptors. */
static void defaults(ax_text_computed *c)
{
    memset(c, 0, sizeof *c);
    c->size = ax_prop_builtin(AX_PROP_FONT_SIZE)->default_value.f32;
    c->weight = ax_prop_builtin(AX_PROP_FONT_WEIGHT)->default_value.f32;
    c->width = ax_prop_builtin(AX_PROP_FONT_WIDTH)->default_value.f32;
    c->line_height = ax_prop_builtin(AX_PROP_LINE_HEIGHT)->default_value.f32;
    c->color = ax_prop_builtin(AX_PROP_COLOR)->default_value.color;
}

/* The nearest ancestor's text component (its slot in *out_slot), or NULL. */
static ax_node_text *ancestor_text(ax_scene *scene, uint32_t slot, uint32_t *out_slot)
{
    ax_node_store *s = &scene->store;
    uint32_t cur = s->links[s->slots[slot].row].parent;
    while (cur != AX_NODE_NONE) {
        ax_node_text *t = (ax_node_text *)ax_node_store_peek_component(s, cur,
                                                                       scene->text_component);
        if (t != NULL) {
            *out_slot = cur;
            return t;
        }
        cur = s->links[s->slots[cur].row].parent;
    }
    return NULL;
}

static uint32_t resolve(ax_scene *scene, uint32_t slot, ax_node_text *t)
{
    ax_node_store *s = &scene->store;
    const uint32_t pass = scene->style_pass;
    ax_text_computed parent;
    ax_text_computed next;
    const ax_text_computed *pc;
    uint32_t parent_slot = AX_NODE_NONE;
    ax_node_text *pt = ancestor_text(scene, slot, &parent_slot);
    uint32_t resolved = 1;
    bool layout, paint;

    t->style_pass = pass; /* before recursing: a chain is resolved once */
    if (pt != NULL) {
        if (pt->style_pass != pass && (s->dirty[s->slots[parent_slot].row] & AX_DIRTY_STYLE)) {
            resolved += resolve(scene, parent_slot, pt);
        }
        pc = &pt->computed;
    } else {
        defaults(&parent);
        pc = &parent;
    }
    next = *pc; /* strings borrowed from the parent: retained by set_str below */
#define OWN(prop) ((t->own & AX_TEXT_PROP_BIT(prop)) != 0)
    if (OWN(AX_PROP_FONT_FAMILY)) {
        next.family = t->family;
    }
    if (OWN(AX_PROP_FONT_FEATURES)) {
        next.features = t->features;
    }
    if (OWN(AX_PROP_FONT_SIZE)) {
        next.size = t->size;
    }
    if (OWN(AX_PROP_FONT_WEIGHT)) {
        next.weight = t->weight;
    }
    if (OWN(AX_PROP_FONT_WIDTH)) {
        next.width = t->width;
    }
    if (OWN(AX_PROP_LINE_HEIGHT)) {
        next.line_height = t->line_height;
    }
    if (OWN(AX_PROP_FONT_STYLE)) {
        next.style = t->style;
    }
    if (OWN(AX_PROP_TEXT_ALIGN)) {
        next.align = t->align;
    }
    if (OWN(AX_PROP_COLOR)) {
        next.color = t->color;
    }
#undef OWN
    layout = !same_str(next.family, t->computed.family) ||
             !same_str(next.features, t->computed.features) || next.size != t->computed.size ||
             next.weight != t->computed.weight || next.width != t->computed.width ||
             next.line_height != t->computed.line_height || next.style != t->computed.style;
    paint = next.align != t->computed.align ||
            memcmp(&next.color, &t->computed.color, sizeof next.color) != 0;
    set_str(&t->computed.family, next.family);
    set_str(&t->computed.features, next.features);
    t->computed.size = next.size;
    t->computed.weight = next.weight;
    t->computed.width = next.width;
    t->computed.line_height = next.line_height;
    t->computed.style = next.style;
    t->computed.align = next.align;
    t->computed.color = next.color;
    t->layout_changed = layout;
    if (layout && t->content != NULL) {
        t->paragraph_stale = true;
        ax_scene_mark_layout(scene, ax_node_store_handle(s, slot));
    }
    if (paint) {
        s->dirty[s->slots[slot].row] |= AX_DIRTY_PAINT;
    }
    return resolved;
}

uint32_t ax_scene_resolve_styles(ax_scene *scene)
{
    ax_node_store *s = &scene->store;
    ax_component_storage *cs = &s->components[scene->text_component];
    uint32_t resolved = 0;
    uint32_t i;
    if (cs->count == 0) {
        return 0;
    }
    if (++scene->style_pass == 0) {
        scene->style_pass = 1; /* 0 means never */
    }
    for (i = 0; i < cs->count; ++i) {
        const uint32_t slot = cs->owner_slot[i];
        ax_node_text *t = (ax_node_text *)(void *)(cs->data + cs->stride * i);
        if ((s->dirty[s->slots[slot].row] & AX_DIRTY_STYLE) != 0 && t->style_pass != scene->style_pass) {
            resolved += resolve(scene, slot, t);
        }
    }
    return resolved;
}
