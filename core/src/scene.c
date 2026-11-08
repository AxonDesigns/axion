/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The scene: property values by id, validation and dirty propagation over the node store.
 */
#include <math.h>
#include <string.h>

#include <axion/base/ax_assert.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_scene.h>
#include <axion/core/ax_text_node.h>
#include <axion/text/ax_text.h>

size_t ax__anim_node_size(void); /* anim/animator.c */

/* The stable value types mirror base's math types, so values cross without conversion. */
AX_STATIC_ASSERT(sizeof(ax_rgba) == sizeof(ax_color), "ax_rgba mirrors ax_color");
AX_STATIC_ASSERT(sizeof(ax_transform2d) == sizeof(ax_affine2), "ax_transform2d mirrors ax_affine2");
AX_STATIC_ASSERT(sizeof(ax_vec2f) == sizeof(ax_vec2), "ax_vec2f mirrors ax_vec2");

/* The text component's references go with it. */
static void text_removed(void *element, void *user)
{
    ax_node_text *t = (ax_node_text *)element;
    (void)user;
    ax_shared_str_release(t->content);
    ax_shared_str_release(t->family);
    ax_shared_str_release(t->features);
    ax_shared_str_release(t->computed.family);
    ax_shared_str_release(t->computed.features);
    if (t->paragraph != NULL) {
        ax_text_paragraph_release(t->paragraph);
    }
    memset(t, 0, sizeof *t);
}

void ax_scene_init(ax_scene *scene, const ax_node_store_desc *desc)
{
    memset(scene, 0, sizeof *scene);
    ax_node_store_init(&scene->store, desc);
    ax_prop_registry_init(&scene->props, &scene->store.allocator);
    {
        /* Layout's measure functions (ax_layout.h): the first component type, always. */
        ax_component_desc cd;
        ax_result r;
        memset(&cd, 0, sizeof cd);
        cd.size = sizeof(void *) * 2u;
        cd.align = AX_ALIGNOF(void *);
        r = ax_node_store_register_component(&scene->store, &cd, &scene->measure_component);
        AX_ASSERT(r == AX_OK);
        /* The animator's (decision 94): a node's own transitions and its running animations. */
        cd.size = sizeof(uint16_t) * 8u;
        cd.align = AX_ALIGNOF(uint16_t);
        r = ax_node_store_register_component(&scene->store, &cd, &scene->transitions_component);
        AX_ASSERT(r == AX_OK);
        cd.size = ax__anim_node_size();
        cd.align = 8u;
        r = ax_node_store_register_component(&scene->store, &cd, &scene->anim_component);
        AX_ASSERT(r == AX_OK);
        /* Text (decision 99): own and computed values, the paragraph. */
        cd.size = sizeof(ax_node_text);
        cd.align = AX_ALIGNOF(ax_node_text);
        cd.on_remove = text_removed;
        r = ax_node_store_register_component(&scene->store, &cd, &scene->text_component);
        AX_ASSERT(r == AX_OK);
        (void)r;
    }
}

void ax_scene_deinit(ax_scene *scene)
{
    ax_mem_free(&scene->store.allocator, scene->dirty.layout_roots,
                sizeof(ax_node) * scene->dirty.layout_root_capacity);
    ax_prop_registry_deinit(&scene->props);
    ax_node_store_deinit(&scene->store);
    memset(scene, 0, sizeof *scene);
}

/* ---- Dirty propagation ----------------------------------------------------------------------- */

static uint32_t row_of_slot(const ax_scene *scene, uint32_t slot)
{
    return scene->store.slots[slot].row;
}

static bool is_boundary_row(const ax_node_store *s, uint32_t row)
{
    const ax_node_style *st = &s->style[row];
    if (s->links[row].parent == AX_NODE_NONE) {
        return true;
    }
    /* A static node is not a containing block: its absolute descendants are laid out by an
     * ancestor, so it cannot be laid out alone (decision 90). */
    return st->width.unit == AX_UNIT_POINTS && st->height.unit == AX_UNIT_POINTS &&
           st->position != AX_POSITION_STATIC && st->display != AX_DISPLAY_NONE;
}

static void add_root(ax_scene *scene, uint32_t slot)
{
    ax_dirty_set *d = &scene->dirty;
    uint32_t row = row_of_slot(scene, slot);
    if (scene->store.dirty[row] & AX_DIRTY_LAYOUT_ROOT) {
        return;
    }
    if (d->layout_root_count == d->layout_root_capacity) {
        uint32_t cap = d->layout_root_capacity < 16u ? 16u : d->layout_root_capacity * 2u;
        ax_node *fresh = (ax_node *)ax_mem_alloc(&scene->store.allocator, sizeof(ax_node) * cap,
                                                 AX_ALIGNOF(ax_node));
        if (fresh == NULL) {
            d->roots_overflowed = true;
            return;
        }
        if (d->layout_roots != NULL) {
            memcpy(fresh, d->layout_roots, sizeof(ax_node) * d->layout_root_count);
            ax_mem_free(&scene->store.allocator, d->layout_roots,
                        sizeof(ax_node) * d->layout_root_capacity);
        }
        d->layout_roots = fresh;
        d->layout_root_capacity = cap;
    }
    d->layout_roots[d->layout_root_count++] = ax_node_store_handle(&scene->store, slot);
    scene->store.dirty[row] |= AX_DIRTY_LAYOUT_ROOT;
}

/* Marks slot and its ancestors LAYOUT_CHILD up to the nearest boundary, which becomes a root. A
 * node already marked LAYOUT_CHILD has its chain marked up to its root, so climbing stops. */
static void climb(ax_scene *scene, uint32_t slot)
{
    ax_node_store *s = &scene->store;
    for (;;) {
        uint32_t row = row_of_slot(scene, slot);
        if (is_boundary_row(s, row)) {
            s->dirty[row] |= AX_DIRTY_LAYOUT_CHILD;
            add_root(scene, slot);
            return;
        }
        if (s->dirty[row] & AX_DIRTY_LAYOUT_CHILD) {
            return;
        }
        s->dirty[row] |= AX_DIRTY_LAYOUT_CHILD;
        slot = s->links[row].parent;
    }
}

/* The node's own layout inputs changed: its size or position may change. */
static void layout_changed(ax_scene *scene, uint32_t slot)
{
    ax_node_store *s = &scene->store;
    uint32_t row = row_of_slot(scene, slot);
    uint32_t parent = s->links[row].parent;
    s->dirty[row] |= AX_DIRTY_LAYOUT;
    if (parent == AX_NODE_NONE) {
        add_root(scene, slot);
    } else {
        climb(scene, parent);
    }
}

/* A node's children were added, removed or reordered. */
static void children_changed(ax_scene *scene, uint32_t slot)
{
    ax_node_store *s = &scene->store;
    uint32_t row = row_of_slot(scene, slot);
    s->dirty[row] |= AX_DIRTY_LAYOUT;
    if (is_boundary_row(s, row)) {
        add_root(scene, slot); /* its size does not depend on its children */
    } else {
        climb(scene, s->links[row].parent);
    }
}

static void mark_effect(ax_scene *scene, uint32_t slot, ax_effect_class effect)
{
    switch (effect) {
    case AX_EFFECT_LAYOUT:
        layout_changed(scene, slot);
        break;
    case AX_EFFECT_PAINT:
        scene->store.dirty[row_of_slot(scene, slot)] |= AX_DIRTY_PAINT;
        break;
    case AX_EFFECT_COMPOSITE:
        scene->store.dirty[row_of_slot(scene, slot)] |= AX_DIRTY_COMPOSITE;
        break;
    }
}

/* An inherited property changed on root: STYLE and the effect on the whole subtree (pre-order,
 * without a stack). */
static void mark_inherited(ax_scene *scene, uint32_t root, ax_effect_class effect)
{
    ax_node_store *s = &scene->store;
    uint32_t cur = root;
    for (;;) {
        const ax_node_links *l;
        s->dirty[row_of_slot(scene, cur)] |= AX_DIRTY_STYLE;
        mark_effect(scene, cur, effect);
        l = &s->links[row_of_slot(scene, cur)];
        if (l->first_child != AX_NODE_NONE) {
            cur = l->first_child;
            continue;
        }
        while (cur != root && s->links[row_of_slot(scene, cur)].next_sibling == AX_NODE_NONE) {
            cur = s->links[row_of_slot(scene, cur)].parent;
        }
        if (cur == root) {
            return;
        }
        cur = s->links[row_of_slot(scene, cur)].next_sibling;
    }
}

/* STYLE on a subtree (a node moved to another parent): pre-order, without a stack. */
static void mark_style_subtree(ax_scene *scene, uint32_t root)
{
    ax_node_store *s = &scene->store;
    uint32_t cur = root;
    for (;;) {
        const ax_node_links *l = &s->links[row_of_slot(scene, cur)];
        s->dirty[row_of_slot(scene, cur)] |= AX_DIRTY_STYLE;
        if (l->first_child != AX_NODE_NONE) {
            cur = l->first_child;
            continue;
        }
        while (cur != root && s->links[row_of_slot(scene, cur)].next_sibling == AX_NODE_NONE) {
            cur = s->links[row_of_slot(scene, cur)].parent;
        }
        if (cur == root) {
            return;
        }
        cur = s->links[row_of_slot(scene, cur)].next_sibling;
    }
}

/* ---- Tree ------------------------------------------------------------------------------------ */

static void mark_new(ax_scene *scene, ax_node node)
{
    uint32_t slot = ax_handle_slot(node.h);
    scene->store.dirty[row_of_slot(scene, slot)] |= AX_DIRTY_NEW_NODE;
    add_root(scene, slot);
}

ax_result ax_scene_create(ax_scene *scene, ax_node_kind kind, ax_node *out)
{
    ax_result r = ax_node_store_create(&scene->store, kind, out);
    if (r == AX_OK) {
        mark_new(scene, *out);
    }
    return r;
}

ax_result ax_scene_create_at(ax_scene *scene, ax_node node, ax_node_kind kind)
{
    ax_result r = ax_node_store_create_at(&scene->store, node, kind);
    if (r == AX_OK) {
        mark_new(scene, node);
    }
    return r;
}

ax_result ax_scene_insert_before(ax_scene *scene, ax_node parent, ax_node child, ax_node before)
{
    ax_node_store *s = &scene->store;
    uint32_t child_row = ax_node_store_row(s, child);
    uint32_t old_parent;
    uint32_t old_next;
    ax_result r;

    if (child_row == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    old_parent = s->links[child_row].parent;
    old_next = s->links[child_row].next_sibling;
    r = ax_node_store_insert_before(s, parent, child, before);
    if (r != AX_OK) {
        return r;
    }
    if (old_parent == ax_handle_slot(parent.h) &&
        old_next == (AX_HANDLE_IS_NULL(before) ? AX_NODE_NONE : ax_handle_slot(before.h))) {
        return AX_OK; /* already there: nothing changed */
    }
    if (old_parent != AX_NODE_NONE && old_parent != ax_handle_slot(parent.h)) {
        children_changed(scene, old_parent);
    }
    children_changed(scene, ax_handle_slot(parent.h));
    s->dirty[ax_node_store_row(s, child)] |= AX_DIRTY_LAYOUT;
    if (old_parent != ax_handle_slot(parent.h)) {
        mark_style_subtree(scene, ax_handle_slot(child.h)); /* what it inherits may change */
    }
    return AX_OK;
}

ax_result ax_scene_destroy(ax_scene *scene, ax_node node)
{
    ax_node_store *s = &scene->store;
    uint32_t row = ax_node_store_row(s, node);
    uint32_t parent;
    ax_result r;
    if (row == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    parent = s->links[row].parent;
    r = ax_node_store_destroy(s, node);
    if (r == AX_OK && parent != AX_NODE_NONE) {
        children_changed(scene, parent);
    }
    return r;
}

ax_result ax_scene_detach(ax_scene *scene, ax_node node)
{
    ax_node_store *s = &scene->store;
    uint32_t row = ax_node_store_row(s, node);
    uint32_t parent;
    if (row == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    parent = s->links[row].parent;
    if (parent == AX_NODE_NONE) {
        return AX_OK;
    }
    ax_node_store_detach(s, node);
    children_changed(scene, parent);
    s->dirty[row] |= AX_DIRTY_LAYOUT;
    add_root(scene, ax_handle_slot(node.h));
    return AX_OK;
}

/* ---- Values ---------------------------------------------------------------------------------- */

static size_t value_size(ax_value_type type)
{
    switch (type) {
    case AX_VALUE_F32:
        return sizeof(float);
    case AX_VALUE_DIMENSION:
        return sizeof(ax_dimension);
    case AX_VALUE_VEC2:
        return sizeof(ax_vec2);
    case AX_VALUE_COLOR:
        return sizeof(ax_color);
    case AX_VALUE_TRANSFORM2D:
        return sizeof(ax_affine2);
    case AX_VALUE_U32:
        return sizeof(uint32_t);
    case AX_VALUE_BOOL:
        return sizeof(bool);
    case AX_VALUE_STRING:
        return sizeof(const ax_shared_str *);
    }
    return 0;
}

ax_result ax_scene_check_value(const ax_scene *scene, ax_prop prop, ax_value_type type,
                               const ax_value *value)
{
    const ax_prop_desc *d = ax_prop_registry_find(&scene->props, prop);
    if (d == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "unknown property 0x%x", (unsigned)prop);
    }
    return ax_prop_check_value(d, type, value);
}

/* Address of a built-in property's value in its hot column. */
static unsigned char *column_value(const ax_node_store *s, uint32_t row, const ax_prop_desc *d)
{
    switch (d->storage) {
    case AX_PROP_IN_STYLE:
        return (unsigned char *)&s->style[row] + d->offset;
    case AX_PROP_IN_PAINT:
        return (unsigned char *)&s->paint[row] + d->offset;
    case AX_PROP_IN_COMPOSITE:
        return (unsigned char *)&s->composite[row] + d->offset;
    default:
        return NULL;
    }
}

static void mark_set(ax_scene *scene, ax_node node, const ax_prop_desc *d)
{
    if (d->flags & AX_PROP_INHERITED) {
        mark_inherited(scene, ax_handle_slot(node.h), d->effect);
    } else {
        mark_effect(scene, ax_handle_slot(node.h), d->effect);
    }
}

/* Sets a text property's own value (decision 99): the node gets the text component, the
 * property's bit, and a string's reference. Setting an inherited property to the value it already
 * computes still sets the bit (it stops inheriting) but marks nothing when the own value is
 * unchanged. */
static ax_result set_text(ax_scene *scene, ax_node node, const ax_prop_desc *d,
                          const ax_value *value)
{
    ax_node_text *t = (ax_node_text *)ax_node_store_get_component(&scene->store, node,
                                                                  scene->text_component);
    const uint32_t bit = AX_TEXT_PROP_BIT(d->id);
    unsigned char *dst;
    bool was_set;
    if (t == NULL) {
        ax_result r = ax_node_store_add_component(&scene->store, node, scene->text_component,
                                                  (void **)&t);
        if (r != AX_OK) {
            return r;
        }
        memset(t, 0, sizeof *t);
        /* Its computed values are resolved by the next style pass. */
        scene->store.dirty[ax_node_store_row(&scene->store, node)] |= AX_DIRTY_STYLE;
    }
    was_set = (t->own & bit) != 0;
    dst = (unsigned char *)t + d->offset;
    if (d->type == AX_VALUE_STRING) {
        const ax_shared_str **slot = (const ax_shared_str **)(void *)dst;
        ax_str a = ax_shared_str_view(*slot), b = ax_shared_str_view(value->str);
        t->own |= bit;
        if (was_set && a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0) {
            return AX_OK;
        }
        ax_shared_str_retain(value->str);
        ax_shared_str_release(*slot);
        *slot = value->str;
        if (d->id == AX_PROP_TEXT) {
            t->paragraph_stale = true;
        }
    } else {
        size_t size = value_size(d->type);
        t->own |= bit;
        if (was_set && memcmp(dst, value, size) == 0) {
            return AX_OK;
        }
        memcpy(dst, value, size);
    }
    mark_set(scene, node, d);
    return AX_OK;
}

/* Sets one longhand (or plain) property; the value has been checked. */
static ax_result set_one(ax_scene *scene, ax_node node, uint32_t row, const ax_prop_desc *d,
                         const ax_value *value)
{
    size_t size = value_size(d->type);
    unsigned char *dst;
    if (d->storage == AX_PROP_IN_TEXT) {
        return set_text(scene, node, d, value);
    }
    if (d->storage == AX_PROP_IN_COMPONENT) {
        ax_value *stored =
            (ax_value *)ax_node_store_get_component(&scene->store, node, d->component);
        if (stored == NULL) {
            ax_result r;
            if (memcmp(&d->default_value, value, size) == 0) {
                return AX_OK; /* setting the default on a node without its own value */
            }
            r = ax_node_store_add_component(&scene->store, node, d->component, (void **)&stored);
            if (r != AX_OK) {
                return r;
            }
            *stored = d->default_value;
        }
        dst = (unsigned char *)stored;
    } else {
        dst = column_value(&scene->store, row, d);
    }
    AX_ASSERT(dst != NULL);
    if (d->stored_u8) {
        uint8_t byte = (uint8_t)value->u32; /* checked: at most the enumeration's last value */
        if (*dst == byte) {
            return AX_OK;
        }
        *dst = byte;
        mark_effect(scene, ax_handle_slot(node.h), d->effect);
        return AX_OK;
    }
    if (memcmp(dst, value, size) == 0) {
        return AX_OK; /* unchanged: nothing to mark */
    }
    memcpy(dst, value, size);
    mark_set(scene, node, d);
    return AX_OK;
}

ax_result ax_scene_set(ax_scene *scene, ax_node node, ax_prop prop, ax_value_type type,
                       const ax_value *value)
{
    const ax_prop_desc *d = ax_prop_registry_find(&scene->props, prop);
    uint32_t row;
    ax_result r;
    uint32_t i;

    if (d == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "unknown property 0x%x", (unsigned)prop);
    }
    r = ax_prop_check_value(d, type, value);
    if (r != AX_OK) {
        return r;
    }
    row = ax_node_store_row(&scene->store, node);
    if (row == AX_NODE_NONE) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "property %s: stale or unknown node", d->name);
    }
    if (d->storage != AX_PROP_EXPANDS) {
        return set_one(scene, node, row, d, value);
    }
    for (i = 0; i < d->longhand_count; ++i) {
        const ax_prop_desc *l = ax_prop_registry_find(&scene->props, d->longhands[i]);
        AX_ASSERT(l != NULL && l->type == d->type && l->storage != AX_PROP_IN_COMPONENT);
        r = set_one(scene, node, row, l, value); /* hot columns: cannot fail */
        AX_ASSERT(r == AX_OK);
    }
    return AX_OK;
}

static void get_one(const ax_scene *scene, ax_node node, uint32_t row, const ax_prop_desc *d,
                    ax_value *out)
{
    memset(out, 0, sizeof *out);
    if (d->storage == AX_PROP_IN_TEXT) {
        const ax_node_text *t = (const ax_node_text *)ax_node_store_get_component(
            &scene->store, node, scene->text_component);
        if (t == NULL || (t->own & AX_TEXT_PROP_BIT(d->id)) == 0) {
            *out = d->default_value;
        } else {
            memcpy(out, (const unsigned char *)t + d->offset, value_size(d->type));
        }
        return;
    }
    if (d->storage == AX_PROP_IN_COMPONENT) {
        const ax_value *stored =
            (const ax_value *)ax_node_store_get_component(&scene->store, node, d->component);
        *out = stored != NULL ? *stored : d->default_value;
        return;
    }
    if (d->stored_u8) {
        out->u32 = *column_value(&scene->store, row, d);
        return;
    }
    memcpy(out, column_value(&scene->store, row, d), value_size(d->type));
}

ax_result ax_scene_get(const ax_scene *scene, ax_node node, ax_prop prop, ax_value *out)
{
    const ax_prop_desc *d = ax_prop_registry_find(&scene->props, prop);
    uint32_t row;
    uint32_t i;

    memset(out, 0, sizeof *out);
    if (d == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "unknown property 0x%x", (unsigned)prop);
    }
    row = ax_node_store_row(&scene->store, node);
    if (row == AX_NODE_NONE) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "property %s: stale or unknown node", d->name);
    }
    if (d->storage != AX_PROP_EXPANDS) {
        get_one(scene, node, row, d, out);
        return AX_OK;
    }
    for (i = 0; i < d->longhand_count; ++i) {
        ax_value v;
        get_one(scene, node, row, ax_prop_registry_find(&scene->props, d->longhands[i]), &v);
        if (i == 0) {
            *out = v;
        } else if (memcmp(&v, out, value_size(d->type)) != 0) {
            memset(out, 0, sizeof *out);
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "property %s: the longhands differ; read each of them", d->name);
        }
    }
    return AX_OK;
}

/* ---- Dirty state ----------------------------------------------------------------------------- */

bool ax_scene_is_relayout_boundary(const ax_scene *scene, ax_node node)
{
    uint32_t row = ax_node_store_row(&scene->store, node);
    return row != AX_NODE_NONE && is_boundary_row(&scene->store, row);
}

uint32_t ax_scene_dirty_flags(const ax_scene *scene, ax_node node)
{
    uint32_t row = ax_node_store_row(&scene->store, node);
    return row == AX_NODE_NONE ? 0u : scene->store.dirty[row];
}

void ax_scene_mark_layout(ax_scene *scene, ax_node node)
{
    if (ax_node_store_is_live(&scene->store, node)) {
        layout_changed(scene, ax_handle_slot(node.h));
    }
}

void ax_scene_clear_dirty(ax_scene *scene)
{
    if (scene->store.row_count > 0) {
        memset(scene->store.dirty, 0, sizeof(uint32_t) * scene->store.row_count);
    }
    scene->dirty.layout_root_count = 0;
    scene->dirty.roots_overflowed = false;
}

/* ---- Text values (decision 99) --------------------------------------------------------------- */

ax_node_text *ax_scene_text(const ax_scene *scene, ax_node node)
{
    return (ax_node_text *)ax_node_store_get_component(&scene->store, node,
                                                       scene->text_component);
}

static const ax_prop_desc *text_desc(const ax_scene *scene, ax_prop prop)
{
    const ax_prop_desc *d = ax_prop_registry_find(&scene->props, prop);
    return d != NULL && d->storage == AX_PROP_IN_TEXT ? d : NULL;
}

ax_result ax_scene_get_own(const ax_scene *scene, ax_node node, ax_prop prop, ax_value *out,
                           bool *out_set)
{
    const ax_prop_desc *d = text_desc(scene, prop);
    const ax_node_text *t;
    memset(out, 0, sizeof *out);
    *out_set = false;
    if (d == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property 0x%x is not a text property",
                            (unsigned)prop);
    }
    if (ax_node_store_row(&scene->store, node) == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    t = ax_scene_text(scene, node);
    if (t == NULL || (t->own & AX_TEXT_PROP_BIT(prop)) == 0) {
        *out = d->default_value;
        return AX_OK;
    }
    *out_set = true;
    memcpy(out, (const unsigned char *)t + d->offset, value_size(d->type));
    return AX_OK;
}

ax_result ax_scene_unset(ax_scene *scene, ax_node node, ax_prop prop)
{
    const ax_prop_desc *d = text_desc(scene, prop);
    ax_node_text *t;
    if (d == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property 0x%x is not a text property",
                            (unsigned)prop);
    }
    if (ax_node_store_row(&scene->store, node) == AX_NODE_NONE) {
        return AX_ERR_INVALID_HANDLE;
    }
    t = ax_scene_text(scene, node);
    if (t == NULL || (t->own & AX_TEXT_PROP_BIT(prop)) == 0) {
        return AX_OK;
    }
    t->own &= ~AX_TEXT_PROP_BIT(prop);
    if (d->type == AX_VALUE_STRING) {
        const ax_shared_str **slot = (const ax_shared_str **)(void *)((unsigned char *)t + d->offset);
        ax_shared_str_release(*slot);
        *slot = NULL;
        if (prop == AX_PROP_TEXT) {
            t->paragraph_stale = true;
        }
    }
    mark_set(scene, node, d);
    return AX_OK;
}

ax_result ax_scene_set_str(ax_scene *scene, ax_node node, ax_prop prop, ax_str value)
{
    ax_value v;
    ax_result r;
    memset(&v, 0, sizeof v);
    v.str = ax_shared_str_create(&scene->store.allocator, value.ptr, value.len);
    if (v.str == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_scene_set_str: out of memory");
    }
    r = ax_scene_set(scene, node, prop, AX_VALUE_STRING, &v);
    ax_shared_str_release(v.str);
    return r;
}
