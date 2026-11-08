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
 * Minor 1 appends to the header (readers of 1.0 skip it with header_size):
 *       80  u64 resource_epoch
 *       88  u32 glyph_table_size          92  u32 glyph_count
 *       96  glyph_count entries sorted by (instance, glyph, bin), each
 *              u32 instance, u32 glyph, u32 bin, u16 width, u16 height, i16 left, i16 top,
 *              u32 reserved (0), then width x height coverage bytes, padded to 4
 *   Every glyph the snapshot's runs use whose bitmap its glyph source has: a deserialized snapshot
 *   draws its text with no text system (decision 101).
 *
 * The item stream is copied as it is, so unknown items survive a round trip. Its records are
 * little-endian in memory too, which every AXION target is; a big-endian host is rejected.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"
#include "axion/render/ax_render_ir.h"
#include "ir_internal.h"

#define HEADER_SIZE_1_0 80u
#define HEADER_SIZE_1_1 96u
#define GLYPH_ENTRY     24u

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

/* ---- the glyph table ------------------------------------------------------------------------- */

typedef struct glyph_key {
    uint32_t instance, glyph, bin;
} glyph_key;

static int key_cmp(const void *a, const void *b)
{
    const glyph_key *x = (const glyph_key *)a, *y = (const glyph_key *)b;
    if (x->instance != y->instance) {
        return x->instance < y->instance ? -1 : 1;
    }
    if (x->glyph != y->glyph) {
        return x->glyph < y->glyph ? -1 : 1;
    }
    if (x->bin != y->bin) {
        return x->bin < y->bin ? -1 : 1;
    }
    return 0;
}

/* The distinct glyphs of the snapshot's runs, sorted (allocated with the default allocator; the
 * caller frees `*cap` keys). */
static ax_result collect_glyphs(const ax_render_ir *ir, glyph_key **out, uint32_t *out_count,
                                uint32_t *cap)
{
    const ax_allocator *a = ax_default_allocator();
    glyph_key *keys = NULL;
    uint32_t n = 0, c = 0, i;
    *out = NULL;
    *out_count = 0;
    *cap = 0;
    if (ir->glyphs == NULL) {
        return AX_OK;
    }
    for (i = 0; i < ir->layer_count; ++i) {
        ax_ir_item_iter it = ax_ir_items(ir, ax_ir_layer_at(ir, i));
        const ax_ir_item_header *h;
        while ((h = ax_ir_item_next(&it)) != NULL) {
            ax_ir_glyph_run_item run;
            uint32_t k;
            if (h->type != AX_IR_ITEM_GLYPH_RUN) {
                continue;
            }
            memcpy(&run, h, sizeof run);
            if (n + run.glyph_count > c) {
                uint32_t nc = c < 256u ? 256u : c;
                glyph_key *fresh;
                while (nc < n + run.glyph_count) {
                    nc *= 2u;
                }
                fresh = (glyph_key *)ax_mem_alloc(a, sizeof(glyph_key) * nc, 4);
                if (fresh == NULL) {
                    ax_mem_free(a, keys, sizeof(glyph_key) * c);
                    return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
                }
                if (keys != NULL) {
                    memcpy(fresh, keys, sizeof(glyph_key) * n);
                    ax_mem_free(a, keys, sizeof(glyph_key) * c);
                }
                keys = fresh;
                c = nc;
            }
            for (k = 0; k < run.glyph_count; ++k) {
                ax_ir_glyph g;
                memcpy(&g, (const uint8_t *)h + sizeof run + sizeof g * k, sizeof g);
                keys[n].instance = run.font_instance;
                keys[n].glyph = g.id;
                keys[n].bin = g.bin;
                n++;
            }
        }
    }
    if (n > 1) {
        uint32_t w = 1;
        qsort(keys, n, sizeof(glyph_key), key_cmp);
        for (i = 1; i < n; ++i) {
            if (key_cmp(&keys[i], &keys[w - 1u]) != 0) {
                keys[w++] = keys[i];
            }
        }
        n = w;
    }
    *out = keys;
    *out_count = n;
    *cap = c;
    return AX_OK;
}

/* Bytes of the glyph table for these keys (those the source has). */
static size_t table_size(const ax_render_ir *ir, const glyph_key *keys, uint32_t n,
                         uint32_t *out_present)
{
    size_t size = 0;
    uint32_t i, present = 0;
    for (i = 0; i < n; ++i) {
        ax_ir_glyph_bitmap b;
        if (ir->glyphs->lookup(ir->glyphs, keys[i].instance, keys[i].glyph, keys[i].bin, &b)) {
            size += GLYPH_ENTRY + (((size_t)b.width * b.height + 3u) & ~(size_t)3u);
            present++;
        }
    }
    *out_present = present;
    return size;
}

/* ---- writing --------------------------------------------------------------------------------- */

static size_t body_size(const ax_render_ir *ir)
{
    return (size_t)ir->damage_count * 16u + (size_t)ir->layer_count * AX__IR_LAYER_SIZE_1_0 +
           ir->items_size;
}

size_t ax_ir_serialized_size(const ax_render_ir *ir)
{
    glyph_key *keys;
    uint32_t n = 0, cap = 0, present = 0;
    size_t table = 0;
    if (collect_glyphs(ir, &keys, &n, &cap) == AX_OK && keys != NULL) {
        table = table_size(ir, keys, n, &present);
        ax_mem_free(ax_default_allocator(), keys, sizeof(glyph_key) * cap);
    }
    return HEADER_SIZE_1_1 + table + body_size(ir);
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
    glyph_key *keys = NULL;
    uint32_t n = 0, cap = 0, present = 0;
    size_t table = 0, size;
    writer w;
    uint32_t i;
    ax_result r;

    if (out_size != NULL) {
        *out_size = 0;
    }
    if (!host_is_little_endian()) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "render IR: big-endian hosts are not supported");
    }
    r = ax_ir_validate(ir);
    if (r != AX_OK) {
        return r;
    }
    r = collect_glyphs(ir, &keys, &n, &cap);
    if (r != AX_OK) {
        return r;
    }
    if (keys != NULL) {
        table = table_size(ir, keys, n, &present);
    }
    size = HEADER_SIZE_1_1 + table + body_size(ir);
    if (out_size != NULL) {
        *out_size = size;
    }
    if (capacity < size || table > UINT32_MAX - HEADER_SIZE_1_1) {
        ax_mem_free(ax_default_allocator(), keys, sizeof(glyph_key) * cap);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: %zu bytes needed, %zu given", size,
                            capacity);
    }
    w.p = (uint8_t *)out;
    memcpy(w.p, "AXIR", 4);
    w.p += 4;
    w_u16(&w, ir->version_major);
    w_u16(&w, ir->version_minor);
    w_u32(&w, (uint32_t)(HEADER_SIZE_1_1 + table));
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
    /* Minor 1: the resource epoch and the glyph table. */
    w_u64(&w, ir->struct_size >= offsetof(ax_render_ir, resource_epoch) + sizeof(uint64_t)
                  ? ir->resource_epoch
                  : 0u);
    w_u32(&w, (uint32_t)table);
    w_u32(&w, present);
    for (i = 0; i < n; ++i) {
        ax_ir_glyph_bitmap b;
        uint32_t y;
        size_t pad;
        if (!ir->glyphs->lookup(ir->glyphs, keys[i].instance, keys[i].glyph, keys[i].bin, &b)) {
            continue;
        }
        w_u32(&w, keys[i].instance);
        w_u32(&w, keys[i].glyph);
        w_u32(&w, keys[i].bin);
        w_u16(&w, b.width);
        w_u16(&w, b.height);
        w_u16(&w, (uint16_t)b.left);
        w_u16(&w, (uint16_t)b.top);
        w_u32(&w, 0);
        for (y = 0; y < b.height; ++y) {
            memcpy(w.p, b.pixels + (size_t)y * b.stride, b.width);
            w.p += b.width;
        }
        pad = (4u - ((size_t)b.width * b.height) % 4u) % 4u;
        memset(w.p, 0, pad);
        w.p += pad;
    }
    ax_mem_free(ax_default_allocator(), keys, sizeof(glyph_key) * cap);
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

/* A deserialized snapshot's glyph source: the table's entries, sorted, in the arena. */
typedef struct table_entry {
    glyph_key key;
    ax_ir_glyph_bitmap bitmap;
} table_entry;

typedef struct table_source {
    ax_ir_glyph_source base;
    const table_entry *entries;
    uint32_t count;
} table_source;

static bool table_lookup(const ax_ir_glyph_source *self, uint32_t instance, uint32_t glyph,
                         uint32_t bin, ax_ir_glyph_bitmap *out)
{
    const table_source *t = (const table_source *)self;
    glyph_key k;
    uint32_t lo = 0, hi = t->count;
    k.instance = instance;
    k.glyph = glyph;
    k.bin = bin;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2u;
        const int c = key_cmp(&t->entries[mid].key, &k);
        if (c == 0) {
            *out = t->entries[mid].bitmap;
            return true;
        }
        if (c < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return false;
}

static ax_result read_glyph_table(const uint8_t *p, uint32_t table, uint32_t count, ax_arena *arena,
                                  const ax_ir_glyph_source **out)
{
    table_source *src;
    table_entry *entries;
    uint32_t at = 0, i;
    *out = NULL;
    if (count == 0) {
        return AX_OK;
    }
    if (count > table / GLYPH_ENTRY) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: glyph table of %u bytes, %u glyphs",
                            table, count);
    }
    src = AX_ARENA_NEW(arena, table_source);
    entries = AX_ARENA_NEW_ARRAY(arena, table_entry, count);
    if (src == NULL || entries == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
    }
    for (i = 0; i < count; ++i) {
        table_entry *e = &entries[i];
        size_t bytes;
        uint8_t *pixels = NULL;
        if (table - at < GLYPH_ENTRY) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: truncated glyph table");
        }
        e->key.instance = r_u32(p + at);
        e->key.glyph = r_u32(p + at + 4);
        e->key.bin = r_u32(p + at + 8);
        e->bitmap.width = r_u16(p + at + 12);
        e->bitmap.height = r_u16(p + at + 14);
        e->bitmap.left = (int16_t)r_u16(p + at + 16);
        e->bitmap.top = (int16_t)r_u16(p + at + 18);
        e->bitmap.stride = e->bitmap.width;
        at += GLYPH_ENTRY;
        bytes = (size_t)e->bitmap.width * e->bitmap.height;
        if ((size_t)(table - at) < ((bytes + 3u) & ~(size_t)3u) ||
            (i > 0 && key_cmp(&entries[i - 1u].key, &e->key) >= 0)) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: malformed glyph table");
        }
        if (bytes > 0) {
            pixels = (uint8_t *)ax_arena_alloc(arena, bytes, 4);
            if (pixels == NULL) {
                return ax_set_error(AX_ERR_OUT_OF_MEMORY, "render IR: out of memory");
            }
            memcpy(pixels, p + at, bytes);
        }
        e->bitmap.pixels = pixels;
        at += (uint32_t)((bytes + 3u) & ~(size_t)3u);
    }
    src->base.lookup = table_lookup;
    src->base.user = NULL;
    src->entries = entries;
    src->count = count;
    *out = &src->base;
    return AX_OK;
}

ax_result ax_ir_deserialize(const void *data, size_t size, ax_arena *arena,
                            const ax_render_ir **out_ir)
{
    const ax_ir_glyph_source *glyphs = NULL;
    uint64_t epoch = 0;
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
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: not a serialized snapshot");
    }
    if (r_u16(p + 4) != AX_IR_VERSION_MAJOR) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "render IR: version %u.%u, this build reads %u.x",
                            r_u16(p + 4), r_u16(p + 6), AX_IR_VERSION_MAJOR);
    }
    if (size < HEADER_SIZE_1_0) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: truncated header");
    }
    header_size = r_u32(p + 8);
    stride = r_u32(p + 68);
    if (header_size < HEADER_SIZE_1_0 || header_size > size) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: header size %u", header_size);
    }
    if (stride < AX__IR_LAYER_SIZE_1_0 || stride % 4u != 0) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: layer stride %u", stride);
    }
    need = (uint64_t)header_size + (uint64_t)r_u32(p + 60) * 16u +
           (uint64_t)r_u32(p + 64) * stride + r_u32(p + 72);
    if (need != size) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "render IR: %zu bytes, the header describes %llu", size,
                            (unsigned long long)need);
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
    if (r_u16(p + 6) >= 1u && header_size >= HEADER_SIZE_1_1) {
        const uint32_t table = r_u32(p + 88);
        epoch = r_u64(p + 80);
        if (table > header_size - HEADER_SIZE_1_1) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: glyph table of %u bytes",
                                table);
        }
        r = read_glyph_table(p + HEADER_SIZE_1_1, table, r_u32(p + 92), arena, &glyphs);
        if (r != AX_OK) {
            return r;
        }
    }
    ir->glyphs = glyphs;
    ir->resource_epoch = epoch;
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
