/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The app thread's mirror: three copies of the published state behind base's triple buffer
 * (decision 50). The scene thread owns the copy it writes and the app thread the copy it latched;
 * the triple buffer swaps them with an atomic exchange, so neither side ever waits or sees the
 * other's copy mid-write.
 *
 * Bringing a copy up to date: every change stamps its slot with the publication that will carry
 * it. A copy remembers the publication it last reflected, and publishing copies only the slots
 * stamped after that, which also covers a copy several publications behind (the app thread held
 * it). The scan over the stamps is O(slots); the copying is O(changed slots).
 */
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/ax_mirror.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>

void ax__mirror_init(world_impl *w)
{
    memset(w->mirror, 0, sizeof w->mirror);
    ax_tb_init(&w->mirror_tb);
    w->mirror_read = ax_tb_acquire(&w->mirror_tb);
}

void ax__mirror_free(world_impl *w)
{
    int i;
    for (i = 0; i < 3; ++i) {
        ax_mem_free(&w->allocator, w->mirror[i].nodes, sizeof(mirror_node) * w->mirror[i].capacity);
        w->mirror[i].nodes = NULL;
        w->mirror[i].capacity = 0;
    }
    ax_mem_free(&w->allocator, w->changed_at, sizeof(uint64_t) * w->changed_capacity);
    w->changed_at = NULL;
    w->changed_capacity = 0;
}

/* ---- Scene thread ---------------------------------------------------------------------------- */

void ax__mirror_touch_slot(world_impl *w, uint32_t slot)
{
    w->mirror_dirty = true;
    if (slot >= w->changed_capacity) {
        uint32_t cap = w->changed_capacity < 256u ? 256u : w->changed_capacity;
        uint64_t *fresh;
        while (cap <= slot) {
            cap = cap > 0x7FFFFFFFu ? slot + 1u : cap * 2u;
        }
        fresh =
            (uint64_t *)ax_mem_alloc(&w->allocator, sizeof(uint64_t) * cap, AX_ALIGNOF(uint64_t));
        if (fresh == NULL) {
            /* Out of memory: the next publication copies every slot instead. */
            w->changed_all_at = w->mirror_pub + 1u;
            return;
        }
        memset(fresh, 0, sizeof(uint64_t) * cap);
        if (w->changed_at != NULL) {
            memcpy(fresh, w->changed_at, sizeof(uint64_t) * w->changed_capacity);
            ax_mem_free(&w->allocator, w->changed_at, sizeof(uint64_t) * w->changed_capacity);
        }
        w->changed_at = fresh;
        w->changed_capacity = cap;
    }
    w->changed_at[slot] = w->mirror_pub + 1u;
}

void ax_world_touch_node(ax_world world, ax_node node)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_touch_node: not the scene thread");
    if (!AX_HANDLE_IS_NULL(node)) {
        ax__mirror_touch_slot(w, ax_handle_slot(node.h));
    }
}

static void write_node(const ax_node_store *s, uint32_t slot, mirror_node *out)
{
    uint32_t row;
    memset(out, 0, sizeof *out);
    if (slot >= s->slot_count) {
        return;
    }
    row = s->slots[slot].row;
    if (row >= AX_NODE_SLOT_RETIRED) {
        return; /* free or retired: dead */
    }
    out->handle = ax_handle_make(slot, s->slots[slot].generation);
    out->parent = ax_node_store_handle(s, s->links[row].parent).h;
    out->kind = s->kind[row];
    out->style = s->style[row];
    out->paint = s->paint[row];
    out->composite = s->composite[row];
    out->layout = s->layout[row];
}

bool ax_world_publish_mirror(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    const ax_node_store *s;
    mirror_copy *c;
    uint64_t pub;
    uint32_t slots;
    uint32_t slot;
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_publish_mirror: not the scene thread");
    if (w == NULL || !w->mirror_dirty) {
        return false;
    }
    s = &w->scene.store;
    slots = s->slot_count;
    c = &w->mirror[ax_tb_write_index(&w->mirror_tb)];
    if (c->capacity < slots) {
        uint32_t cap = c->capacity < 256u ? 256u : c->capacity;
        mirror_node *fresh;
        while (cap < slots) {
            cap = cap > 0x7FFFFFFFu ? slots : cap * 2u;
        }
        fresh = (mirror_node *)ax_mem_alloc(&w->allocator, sizeof(mirror_node) * cap,
                                            AX_ALIGNOF(mirror_node));
        if (fresh == NULL) {
            return false; /* stays dirty: the next publication tries again */
        }
        memset(fresh, 0, sizeof(mirror_node) * cap);
        if (c->nodes != NULL) {
            memcpy(fresh, c->nodes, sizeof(mirror_node) * c->count);
            ax_mem_free(&w->allocator, c->nodes, sizeof(mirror_node) * c->capacity);
        }
        c->nodes = fresh;
        c->capacity = cap;
    }

    pub = w->mirror_pub + 1u;
    for (slot = 0; slot < slots; ++slot) {
        uint64_t stamp = slot < w->changed_capacity ? w->changed_at[slot] : 0;
        if (stamp > c->pub || w->changed_all_at > c->pub) {
            write_node(s, slot, &c->nodes[slot]);
        }
    }
    c->count = slots;
    c->pub = pub;
    c->seq = ax_atomic_load_u64(&w->applied_seq, AX_MO_RELAXED);
    ax_tb_publish(&w->mirror_tb);
    w->mirror_pub = pub;
    w->mirror_dirty = false;
    return true;
}

/* ---- App thread ------------------------------------------------------------------------------ */

static const mirror_copy *latched(ax_world world, world_impl **out_w)
{
    world_impl *w = ax__world_from_handle(world);
    *out_w = w;
    if (w == NULL) {
        return NULL;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_mirror: not the world's app thread");
    return &w->mirror[w->mirror_read];
}

uint64_t ax_world_mirror_sync(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    if (w == NULL) {
        (void)ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_mirror_sync: invalid world");
        return 0;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_world_mirror_sync: not the world's app thread");
    w->mirror_read = ax_tb_acquire(&w->mirror_tb);
    return w->mirror[w->mirror_read].seq;
}

static const mirror_node *find(const mirror_copy *c, ax_node node)
{
    uint32_t slot = ax_handle_slot(node.h);
    if (c == NULL || node.h == 0 || slot >= c->count || c->nodes[slot].handle != node.h) {
        return NULL;
    }
    return &c->nodes[slot];
}

bool ax_mirror_is_live(ax_world world, ax_node node)
{
    world_impl *w;
    return find(latched(world, &w), node) != NULL;
}

ax_node ax_mirror_parent(ax_world world, ax_node node)
{
    world_impl *w;
    const mirror_node *n = find(latched(world, &w), node);
    ax_node p = AX_HANDLE_NULL_INIT;
    if (n != NULL) {
        p.h = n->parent;
    }
    return p;
}

static void read_builtin(const mirror_node *n, const ax_prop_desc *d, ax_value *out)
{
    const unsigned char *base = NULL;
    size_t size = 0;
    memset(out, 0, sizeof *out);
    switch (d->storage) {
    case AX_PROP_IN_STYLE:
        base = (const unsigned char *)&n->style;
        break;
    case AX_PROP_IN_PAINT:
        base = (const unsigned char *)&n->paint;
        break;
    case AX_PROP_IN_COMPOSITE:
        base = (const unsigned char *)&n->composite;
        break;
    default:
        return;
    }
    switch (d->type) {
    case AX_VALUE_F32:
        size = sizeof(float);
        break;
    case AX_VALUE_DIMENSION:
        size = sizeof(ax_dimension);
        break;
    case AX_VALUE_COLOR:
        size = sizeof(ax_color);
        break;
    case AX_VALUE_TRANSFORM2D:
        size = sizeof(ax_affine2);
        break;
    default:
        return; /* no core property has another type yet */
    }
    memcpy(out, base + d->offset, size);
}

static ax_result get_value(ax_world world, ax_node node, ax_prop prop, ax_value_type type,
                           ax_value *out)
{
    world_impl *w;
    const mirror_copy *c = latched(world, &w);
    const ax_prop_desc *d;
    const mirror_node *n;
    uint32_t i;
    memset(out, 0, sizeof *out);
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_mirror: invalid world");
    }
    if (prop >= AX_PROP_FIRST_IDL) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_mirror: property 0x%x was registered at run time; those are not "
                            "mirrored yet",
                            (unsigned)prop);
    }
    d = ax_prop_builtin(prop);
    if (d == NULL || d->type != type) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_mirror: property 0x%x is unknown or of another type",
                            (unsigned)prop);
    }
    n = find(c, node);
    if (n == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE,
                            "ax_mirror: the node is not live in the latched publication");
    }
    if (d->storage != AX_PROP_EXPANDS) {
        read_builtin(n, d, out);
        return AX_OK;
    }
    for (i = 0; i < d->longhand_count; ++i) {
        ax_value v;
        read_builtin(n, ax_prop_builtin(d->longhands[i]), &v);
        if (i == 0) {
            *out = v;
        } else if (memcmp(&v, out, sizeof(float)) != 0) {
            memset(out, 0, sizeof *out);
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "property %s: the longhands differ; read each of them", d->name);
        }
    }
    return AX_OK;
}

ax_result ax_mirror_get_f32(ax_world world, ax_node node, ax_prop prop, float *out)
{
    ax_value v;
    ax_result r = get_value(world, node, prop, AX_VALUE_F32, &v);
    *out = v.f32;
    return r;
}

ax_result ax_mirror_get_dimension(ax_world world, ax_node node, ax_prop prop, ax_dimension *out)
{
    ax_value v;
    ax_result r = get_value(world, node, prop, AX_VALUE_DIMENSION, &v);
    *out = v.dimension;
    return r;
}

ax_result ax_mirror_get_color(ax_world world, ax_node node, ax_prop prop, ax_rgba *out)
{
    ax_value v;
    ax_result r = get_value(world, node, prop, AX_VALUE_COLOR, &v);
    out->r = v.color.r;
    out->g = v.color.g;
    out->b = v.color.b;
    out->a = v.color.a;
    return r;
}

ax_result ax_mirror_get_transform(ax_world world, ax_node node, ax_prop prop, ax_transform2d *out)
{
    ax_value v;
    ax_result r = get_value(world, node, prop, AX_VALUE_TRANSFORM2D, &v);
    out->a = v.transform2d.a;
    out->b = v.transform2d.b;
    out->c = v.transform2d.c;
    out->d = v.transform2d.d;
    out->tx = v.transform2d.tx;
    out->ty = v.transform2d.ty;
    return r;
}

ax_result ax_mirror_get_frame(ax_world world, ax_node node, ax_rectf *out)
{
    world_impl *w;
    const mirror_node *n = find(latched(world, &w), node);
    memset(out, 0, sizeof *out);
    if (n == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE,
                            "ax_mirror: the node is not live in the latched publication");
    }
    out->x = n->layout.frame.x;
    out->y = n->layout.frame.y;
    out->w = n->layout.frame.w;
    out->h = n->layout.frame.h;
    return AX_OK;
}
