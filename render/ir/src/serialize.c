/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Render IR: the binary form. Little-endian throughout.
 *
 *   offset  header (HEADER_SIZE_1_0 bytes in version 1.0; readers skip a larger header_size)
 *        0  "AXIR"
 *        4  u16 version_major          6  u16 version_minor
 *        8  u32 header_size           12  u32 flags (0)
 *       16  f32 logical_width         20  f32 logical_height
 *       24  u64 frame_time_ns         32  u64 sequence
 *       40  f32 background r, g, b, a
 *       56  u32 damage_flags          60  u32 damage_count
 *       64  u32 layer_count           68  u32 layer_stride
 *       72  u32 items_size            76  u32 reserved (0)
 *   then    damage_count x (f32 x, y, w, h)
 *   then    layer_count x layer_stride bytes, each starting with the version 1.0 record:
 *        0  u32 parent, subtree_end, items_offset, items_size, item_count, flags
 *       24  f32 transform a, b, c, d, tx, ty
 *       48  u32 clip kind, clip path; f32 clip rect x, y, w, h; f32 clip radii[4]
 *       88  f32 opacity; u32 blend_mode; u32 effects
 *   then    items_size bytes: the item stream, unchanged
 *
 * The item stream is copied as it is, so unknown items survive a round trip. Its records are
 * little-endian in memory too, which every AXION target is; a big-endian host is rejected.
 */
#include <string.h>

#include "axion/base/ax_error_set.h"
#include "axion/render/ax_render_ir.h"
#include "ir_internal.h"

#define HEADER_SIZE_1_0 80u

static bool host_is_little_endian(void)
{
    const uint16_t probe = 1;
    uint8_t first;
    memcpy(&first, &probe, 1);
    return first == 1;
}

/* ---- little-endian writers and readers --------------------------------------------------------
 */

typedef struct writer {
    uint8_t *p;
} writer;

static void w_u16(writer *w, uint16_t v)
{
    w->p[0] = (uint8_t)v;
    w->p[1] = (uint8_t)(v >> 8);
    w->p += 2;
}

static void w_u32(writer *w, uint32_t v)
{
    w->p[0] = (uint8_t)v;
    w->p[1] = (uint8_t)(v >> 8);
    w->p[2] = (uint8_t)(v >> 16);
    w->p[3] = (uint8_t)(v >> 24);
    w->p += 4;
}

static void w_u64(writer *w, uint64_t v)
{
    w_u32(w, (uint32_t)v);
    w_u32(w, (uint32_t)(v >> 32));
}

static void w_f32(writer *w, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    w_u32(w, v);
}

static uint16_t r_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t r_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t r_u64(const uint8_t *p)
{
    return (uint64_t)r_u32(p) | ((uint64_t)r_u32(p + 4) << 32);
}

static float r_f32(const uint8_t *p)
{
    const uint32_t v = r_u32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* ---- writing --------------------------------------------------------------------------------- */

size_t ax_ir_serialized_size(const ax_render_ir *ir)
{
    return HEADER_SIZE_1_0 + (size_t)ir->damage_count * 16u +
           (size_t)ir->layer_count * AX__IR_LAYER_SIZE_1_0 + ir->items_size;
}

static void write_layer(writer *w, const ax_ir_layer *l)
{
    int i;
    w_u32(w, l->parent);
    w_u32(w, l->subtree_end);
    w_u32(w, l->items_offset);
    w_u32(w, l->items_size);
    w_u32(w, l->item_count);
    w_u32(w, l->flags);
    w_f32(w, l->transform.a);
    w_f32(w, l->transform.b);
    w_f32(w, l->transform.c);
    w_f32(w, l->transform.d);
    w_f32(w, l->transform.tx);
    w_f32(w, l->transform.ty);
    w_u32(w, l->clip.kind);
    w_u32(w, l->clip.path);
    w_f32(w, l->clip.rect.x);
    w_f32(w, l->clip.rect.y);
    w_f32(w, l->clip.rect.w);
    w_f32(w, l->clip.rect.h);
    for (i = 0; i < 4; ++i) {
        w_f32(w, l->clip.radii[i]);
    }
    w_f32(w, l->opacity);
    w_u32(w, l->blend_mode);
    w_u32(w, l->effects);
}

ax_result ax_ir_serialize(const ax_render_ir *ir, void *out, size_t capacity, size_t *out_size)
{
    const size_t size = ax_ir_serialized_size(ir);
    writer w;
    uint32_t i;
    ax_result r;

    if (out_size != NULL) {
        *out_size = size;
    }
    if (!host_is_little_endian()) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: big-endian hosts are not supported");
    }
    r = ax_ir_validate(ir);
    if (r != AX_OK) {
        return r;
    }
    if (capacity < size) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: %zu bytes needed, %zu given", size,
                            capacity);
    }
    w.p = (uint8_t *)out;
    memcpy(w.p, "AXIR", 4);
    w.p += 4;
    w_u16(&w, ir->version_major);
    w_u16(&w, ir->version_minor);
    w_u32(&w, HEADER_SIZE_1_0);
    w_u32(&w, 0);
    w_f32(&w, ir->logical_width);
    w_f32(&w, ir->logical_height);
    w_u64(&w, ir->frame_time_ns);
    w_u64(&w, ir->sequence);
    w_f32(&w, ir->background.r);
    w_f32(&w, ir->background.g);
    w_f32(&w, ir->background.b);
    w_f32(&w, ir->background.a);
    w_u32(&w, ir->damage_flags);
    w_u32(&w, ir->damage_count);
    w_u32(&w, ir->layer_count);
    w_u32(&w, AX__IR_LAYER_SIZE_1_0);
    w_u32(&w, ir->items_size);
    w_u32(&w, 0);
    for (i = 0; i < ir->damage_count; ++i) {
        w_f32(&w, ir->damage[i].x);
        w_f32(&w, ir->damage[i].y);
        w_f32(&w, ir->damage[i].w);
        w_f32(&w, ir->damage[i].h);
    }
    for (i = 0; i < ir->layer_count; ++i) {
        write_layer(&w, ax_ir_layer_at(ir, i));
    }
    if (ir->items_size != 0) {
        memcpy(w.p, ir->items, ir->items_size);
    }
    return AX_OK;
}

/* ---- reading --------------------------------------------------------------------------------- */

static void read_layer(const uint8_t *p, ax_ir_layer *l)
{
    int i;
    l->parent = r_u32(p + 0);
    l->subtree_end = r_u32(p + 4);
    l->items_offset = r_u32(p + 8);
    l->items_size = r_u32(p + 12);
    l->item_count = r_u32(p + 16);
    l->flags = r_u32(p + 20);
    l->transform.a = r_f32(p + 24);
    l->transform.b = r_f32(p + 28);
    l->transform.c = r_f32(p + 32);
    l->transform.d = r_f32(p + 36);
    l->transform.tx = r_f32(p + 40);
    l->transform.ty = r_f32(p + 44);
    l->clip.kind = r_u32(p + 48);
    l->clip.path = r_u32(p + 52);
    l->clip.rect.x = r_f32(p + 56);
    l->clip.rect.y = r_f32(p + 60);
    l->clip.rect.w = r_f32(p + 64);
    l->clip.rect.h = r_f32(p + 68);
    for (i = 0; i < 4; ++i) {
        l->clip.radii[i] = r_f32(p + 72 + 4 * i);
    }
    l->opacity = r_f32(p + 88);
    l->blend_mode = r_u32(p + 92);
    l->effects = r_u32(p + 96);
}

ax_result ax_ir_deserialize(const void *data, size_t size, ax_arena *arena,
                            const ax_render_ir **out_ir)
{
    const uint8_t *p = (const uint8_t *)data;
    ax_render_ir *ir;
    ax_rect *damage = NULL;
    ax_ir_layer *layers;
    uint8_t *items = NULL;
    uint32_t header_size, stride, i;
    uint64_t need;
    ax_result r;

    *out_ir = NULL;
    if (!host_is_little_endian()) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: big-endian hosts are not supported");
    }
    if (data == NULL || size < 8 || memcmp(p, "AXIR", 4) != 0) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: not a serialized snapshot");
    }
    if (r_u16(p + 4) != AX_IR_VERSION_MAJOR) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "render IR: version %u.%u, this build reads %u.x",
                            r_u16(p + 4), r_u16(p + 6), AX_IR_VERSION_MAJOR);
    }
    if (size < HEADER_SIZE_1_0) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: truncated header");
    }
    header_size = r_u32(p + 8);
    stride = r_u32(p + 68);
    if (header_size < HEADER_SIZE_1_0 || header_size > size) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: header size %u", header_size);
    }
    if (stride < AX__IR_LAYER_SIZE_1_0 || stride % 4u != 0) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: layer stride %u", stride);
    }
    need = (uint64_t)header_size + (uint64_t)r_u32(p + 60) * 16u +
           (uint64_t)r_u32(p + 64) * stride + r_u32(p + 72);
    if (need != size) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: %zu bytes, the header describes %llu",
                            size, (unsigned long long)need);
    }

    ir = AX_ARENA_NEW(arena, ax_render_ir);
    if (ir == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
    }
    ir->struct_size = sizeof *ir;
    ir->version_major = r_u16(p + 4);
    ir->version_minor = r_u16(p + 6);
    ir->logical_width = r_f32(p + 16);
    ir->logical_height = r_f32(p + 20);
    ir->frame_time_ns = r_u64(p + 24);
    ir->sequence = r_u64(p + 32);
    ir->background = ax_color_make(r_f32(p + 40), r_f32(p + 44), r_f32(p + 48), r_f32(p + 52));
    ir->damage_flags = r_u32(p + 56);
    ir->damage_count = r_u32(p + 60);
    ir->layer_count = r_u32(p + 64);
    ir->layer_stride = sizeof(ax_ir_layer);
    ir->items_size = r_u32(p + 72);
    p += header_size;

    if (ir->damage_count != 0) {
        damage = AX_ARENA_NEW_ARRAY(arena, ax_rect, ir->damage_count);
        if (damage == NULL) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
        }
        for (i = 0; i < ir->damage_count; ++i, p += 16) {
            damage[i] = ax_rect_make(r_f32(p), r_f32(p + 4), r_f32(p + 8), r_f32(p + 12));
        }
    }
    layers = AX_ARENA_NEW_ARRAY(arena, ax_ir_layer, ir->layer_count != 0 ? ir->layer_count : 1);
    if (layers == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
    }
    for (i = 0; i < ir->layer_count; ++i, p += stride) {
        read_layer(p, &layers[i]); /* fields a newer minor appended are skipped by the stride */
    }
    if (ir->items_size != 0) {
        items = (uint8_t *)ax_arena_alloc(arena, ir->items_size, AX_IR_ITEM_ALIGN);
        if (items == NULL) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
        }
        memcpy(items, p, ir->items_size);
    }
    ir->damage = damage;
    ir->layers = layers;
    ir->items = items;

    r = ax_ir_validate(ir);
    if (r != AX_OK) {
        return r;
    }
    *out_ir = ir;
    return AX_OK;
}
