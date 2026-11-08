/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Render IR: building and validating snapshots.
 */
#include <stddef.h>
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_error_set.h"
#include "axion/render/ax_render_ir.h"
#include "ir_internal.h"

AX_STATIC_ASSERT(sizeof(ax_ir_item_header) == 8, "item header is 8 bytes");
AX_STATIC_ASSERT(sizeof(ax_ir_rect_item) % AX_IR_ITEM_ALIGN == 0, "rect item padded");
AX_STATIC_ASSERT(sizeof(ax_ir_rounded_rect_item) % AX_IR_ITEM_ALIGN == 0, "rrect item padded");
AX_STATIC_ASSERT(sizeof(ax_ir_glyph_run_item) % AX_IR_ITEM_ALIGN == 0, "glyph run padded");
AX_STATIC_ASSERT(sizeof(ax_ir_glyph) == 16, "glyph record is 16 bytes");
AX_STATIC_ASSERT(sizeof(ax_ir_layer) == AX__IR_LAYER_SIZE_1_0, "layer record of version 1.0");

enum { MIN_LAYERS = 16, MIN_ITEMS = 1024, MIN_DAMAGE = 4, MIN_DEPTH = 8 };

/* ---- validation -------------------------------------------------------------------------------
 */

static size_t known_item_size(uint16_t type)
{
    switch (type) {
    case AX_IR_ITEM_RECT:
        return sizeof(ax_ir_rect_item);
    case AX_IR_ITEM_ROUNDED_RECT:
        return sizeof(ax_ir_rounded_rect_item);
    case AX_IR_ITEM_GLYPH_RUN:
        return sizeof(ax_ir_glyph_run_item);
    default:
        return sizeof(ax_ir_item_header);
    }
}

ax_result ax_ir_validate(const ax_render_ir *ir)
{
    uint32_t stack[64];
    uint32_t depth = 0;
    uint32_t i;

    if (ir == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: NULL snapshot");
    }
    if (ir->version_major != AX_IR_VERSION_MAJOR) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "render IR: version %u.%u, this build reads %u.x",
                            ir->version_major, ir->version_minor, AX_IR_VERSION_MAJOR);
    }
    if (ir->layer_count == 0 || ir->layers == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: no root layer");
    }
    if (ir->layer_stride < AX__IR_LAYER_SIZE_1_0 || ir->layer_stride % 4u != 0) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: layer stride %u",
                            ir->layer_stride);
    }
    if (ir->damage_count != 0 && ir->damage == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: damage rects missing");
    }
    if (ir->items_size % AX_IR_ITEM_ALIGN != 0 || (ir->items_size != 0 && ir->items == NULL)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: item stream of %u bytes",
                            ir->items_size);
    }

    for (i = 0; i < ir->layer_count; ++i) {
        const ax_ir_layer *l = ax_ir_layer_at(ir, i);
        uint32_t offset, count = 0;

        /* Tree shape: in pre-order, a layer's parent is the innermost layer still open. */
        while (depth > 0 && ax_ir_layer_at(ir, stack[depth - 1])->subtree_end <= i) {
            --depth;
        }
        if (i == 0) {
            if (l->parent != AX_IR_NO_LAYER || l->subtree_end != ir->layer_count) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: malformed root layer");
            }
        } else if (depth == 0 || l->parent != stack[depth - 1] || l->subtree_end <= i ||
                   l->subtree_end > ax_ir_layer_at(ir, l->parent)->subtree_end) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: layer %u is not in pre-order",
                                i);
        }
        if (l->subtree_end > i + 1) {
            if (depth == AX_COUNTOF(stack)) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "render IR: layers nested deeper than %u",
                                    (unsigned)AX_COUNTOF(stack));
            }
            stack[depth++] = i;
        }

        /* Item range and records. */
        if (l->items_offset % AX_IR_ITEM_ALIGN != 0 || l->items_offset > ir->items_size ||
            l->items_size > ir->items_size - l->items_offset) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: layer %u items out of range",
                                i);
        }
        offset = l->items_offset;
        while (offset < l->items_offset + l->items_size) {
            ax_ir_item_header h;
            memcpy(&h, ir->items + offset, sizeof h);
            if (h.size < sizeof h || h.size % AX_IR_ITEM_ALIGN != 0 ||
                h.size > l->items_offset + l->items_size - offset ||
                h.size < known_item_size(h.type)) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "render IR: layer %u, item at byte %u: type %u, size %u", i,
                                    offset, h.type, h.size);
            }
            if (h.type == AX_IR_ITEM_GLYPH_RUN) {
                ax_ir_glyph_run_item run;
                memcpy(&run, ir->items + offset, sizeof run);
                if (run.glyph_count > (h.size - sizeof run) / sizeof(ax_ir_glyph)) {
                    return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                        "render IR: layer %u, glyph run at byte %u: %u glyphs in "
                                        "%u bytes",
                                        i, offset, run.glyph_count, h.size);
                }
            }
            offset += h.size;
            ++count;
        }
        if (count != l->item_count) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "render IR: layer %u has %u items, says %u", i, count,
                                l->item_count);
        }
    }
    return AX_OK;
}

/* ---- building ---------------------------------------------------------------------------------
 */

ax_ir_layer_desc ax_ir_layer_desc_default(void)
{
    ax_ir_layer_desc d;
    memset(&d, 0, sizeof d);
    d.transform = ax_affine2_identity();
    d.clip.kind = AX_IR_CLIP_NONE;
    d.clip.path = AX_IR_NO_INDEX;
    d.opacity = 1.0f;
    d.blend_mode = AX_IR_BLEND_NORMAL;
    return d;
}

void ax_ir_builder_init(ax_ir_builder *b, const ax_allocator *parent)
{
    memset(b, 0, sizeof *b);
    ax_arena_init(&b->arena, parent, 0);
}

void ax_ir_builder_deinit(ax_ir_builder *b)
{
    ax_arena_deinit(&b->arena);
    memset(b, 0, sizeof *b);
}

static void fail(ax_ir_builder *b, ax_result r)
{
    if (b->error == AX_OK) {
        b->error = r;
    }
}

/* Grows an array in the arena to hold at least `need` elements. The old copy stays in the arena
 * until the next begin; begin reserves the peak so this does not happen in a steady state. */
static bool grow(ax_ir_builder *b, void **array, uint32_t *cap, uint32_t need, size_t elem,
                 size_t align, uint32_t used)
{
    uint64_t new_cap;
    void *p;
    if (need <= *cap) {
        return true;
    }
    new_cap = (uint64_t)(*cap != 0 ? *cap : 1u) * 2u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    if (new_cap > UINT32_MAX) {
        new_cap = UINT32_MAX;
    }
    p = ax_arena_alloc(&b->arena, (size_t)new_cap * elem, align);
    if (p == NULL) {
        fail(b, AX_ERR_OUT_OF_MEMORY);
        return false;
    }
    if (used != 0) {
        memcpy(p, *array, (size_t)used * elem);
    }
    *array = p;
    *cap = (uint32_t)new_cap;
    return true;
}

static uint32_t max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static void open_layer(ax_ir_builder *b, const ax_ir_layer_desc *desc, uint32_t extra_flags)
{
    ax_ir_layer *l;
    if (b->layer_count == UINT32_MAX - 1u) {
        fail(b, AX_ERR_UNSUPPORTED);
        return;
    }
    if (!grow(b, (void **)&b->layers, &b->layer_cap, b->layer_count + 1u, sizeof(ax_ir_layer),
              AX_ALIGNOF(ax_ir_layer), b->layer_count) ||
        !grow(b, (void **)&b->stack, &b->stack_cap, b->depth + 1u, sizeof(uint32_t),
              AX_ALIGNOF(uint32_t), b->depth)) {
        return;
    }
    l = &b->layers[b->layer_count];
    memset(l, 0, sizeof *l);
    l->parent = b->depth != 0 ? b->stack[b->depth - 1] : AX_IR_NO_LAYER;
    l->subtree_end = b->layer_count + 1u;
    l->items_offset = b->items_size;
    l->flags = desc->flags | extra_flags;
    l->transform = desc->transform;
    l->clip = desc->clip;
    l->opacity = desc->opacity;
    l->blend_mode = desc->blend_mode;
    l->effects = AX_IR_NO_INDEX;
    b->stack[b->depth++] = b->layer_count++;
}

static void close_layer(ax_ir_builder *b)
{
    b->layers[b->stack[--b->depth]].subtree_end = b->layer_count;
}

static void close_implicit(ax_ir_builder *b)
{
    if (b->depth > 1 && (b->layers[b->stack[b->depth - 1]].flags & AX_IR_LAYER_IMPLICIT) != 0) {
        close_layer(b);
    }
}

void ax_ir_builder_begin(ax_ir_builder *b, float logical_width, float logical_height,
                         uint64_t frame_time_ns, ax_color background)
{
    const ax_ir_layer_desc root = ax_ir_layer_desc_default();
    ax_arena_reset(&b->arena);
    b->layers = NULL;
    b->items = NULL;
    b->damage = NULL;
    b->stack = NULL;
    b->layer_count = b->layer_cap = 0;
    b->items_size = b->items_cap = 0;
    b->damage_count = b->damage_cap = 0;
    b->depth = b->stack_cap = 0;
    b->error = AX_OK;
    b->recording = true;

    memset(&b->ir, 0, sizeof b->ir);
    b->ir.struct_size = sizeof b->ir;
    b->ir.version_major = AX_IR_VERSION_MAJOR;
    b->ir.version_minor = AX_IR_VERSION_MINOR;
    b->ir.logical_width = logical_width;
    b->ir.logical_height = logical_height;
    b->ir.frame_time_ns = frame_time_ns;
    b->ir.background = background;
    b->ir.layer_stride = sizeof(ax_ir_layer);

    /* Reserve what the largest frame so far needed. */
    grow(b, (void **)&b->layers, &b->layer_cap, max_u32(b->peak_layers, MIN_LAYERS),
         sizeof(ax_ir_layer), AX_ALIGNOF(ax_ir_layer), 0);
    grow(b, (void **)&b->items, &b->items_cap, max_u32(b->peak_items, MIN_ITEMS), 1,
         AX_IR_ITEM_ALIGN, 0);
    grow(b, (void **)&b->damage, &b->damage_cap, max_u32(b->peak_damage, MIN_DAMAGE),
         sizeof(ax_rect), AX_ALIGNOF(ax_rect), 0);
    grow(b, (void **)&b->stack, &b->stack_cap, max_u32(b->peak_depth, MIN_DEPTH), sizeof(uint32_t),
         AX_ALIGNOF(uint32_t), 0);
    if (b->error == AX_OK) {
        open_layer(b, &root, 0);
    }
}

void ax_ir_push_layer(ax_ir_builder *b, const ax_ir_layer_desc *desc)
{
    AX_ASSERT_MSG(b->recording, "ax_ir_push_layer outside begin/end");
    if (b->error != AX_OK || !b->recording) {
        return;
    }
    close_implicit(b);
    open_layer(b, desc, 0);
}

void ax_ir_pop_layer(ax_ir_builder *b)
{
    AX_ASSERT_MSG(b->recording, "ax_ir_pop_layer outside begin/end");
    if (b->error != AX_OK || !b->recording) {
        return;
    }
    close_implicit(b);
    AX_ASSERT_MSG(b->depth > 1, "ax_ir_pop_layer without a matching push");
    if (b->depth <= 1) {
        fail(b, AX_ERR_INVALID_ARGUMENT);
        return;
    }
    close_layer(b);
}

void ax_ir_item(ax_ir_builder *b, uint16_t type, uint16_t flags, const void *payload,
                uint32_t payload_size)
{
    ax_ir_item_header h;
    ax_ir_layer *l;
    uint64_t size;
    AX_ASSERT_MSG(b->recording, "recording an item outside begin/end");
    if (b->error != AX_OK || !b->recording) {
        return;
    }
    size = ((uint64_t)sizeof h + payload_size + (AX_IR_ITEM_ALIGN - 1u)) &
           ~(uint64_t)(AX_IR_ITEM_ALIGN - 1u);
    if (size > UINT32_MAX - (uint64_t)b->items_size) {
        fail(b, AX_ERR_UNSUPPORTED);
        return;
    }
    /* Items after a child keep painting order in an implicit layer drawn after the children. */
    if (b->layer_count - 1u > b->stack[b->depth - 1]) {
        const ax_ir_layer_desc d = ax_ir_layer_desc_default();
        open_layer(b, &d, AX_IR_LAYER_IMPLICIT);
        if (b->error != AX_OK) {
            return;
        }
    }
    if (!grow(b, (void **)&b->items, &b->items_cap, b->items_size + (uint32_t)size, 1,
              AX_IR_ITEM_ALIGN, b->items_size)) {
        return;
    }
    h.type = type;
    h.flags = flags;
    h.size = (uint32_t)size;
    memcpy(b->items + b->items_size, &h, sizeof h);
    if (payload_size != 0) {
        memcpy(b->items + b->items_size + sizeof h, payload, payload_size);
    }
    memset(b->items + b->items_size + sizeof h + payload_size, 0,
           (size_t)size - sizeof h - payload_size);
    b->items_size += (uint32_t)size;
    l = &b->layers[b->stack[b->depth - 1]];
    l->items_size += (uint32_t)size;
    l->item_count++;
}

void ax_ir_rect(ax_ir_builder *b, ax_rect rect, ax_color color)
{
    ax_ir_rect_item it;
    it.rect = rect;
    it.color = color;
    ax_ir_item(b, AX_IR_ITEM_RECT, 0, (const uint8_t *)&it + sizeof(ax_ir_item_header),
               (uint32_t)(sizeof it - sizeof(ax_ir_item_header)));
}

void ax_ir_rounded_rect(ax_ir_builder *b, ax_rect rect, const float radii[4], ax_color color)
{
    ax_ir_rounded_rect_item it;
    it.rect = rect;
    memcpy(it.radii, radii, sizeof it.radii);
    it.color = color;
    ax_ir_item(b, AX_IR_ITEM_ROUNDED_RECT, 0, (const uint8_t *)&it + sizeof(ax_ir_item_header),
               (uint32_t)(sizeof it - sizeof(ax_ir_item_header)));
}

void ax_ir_damage_rect(ax_ir_builder *b, ax_rect rect)
{
    if (b->error != AX_OK || !b->recording) {
        return;
    }
    if (!grow(b, (void **)&b->damage, &b->damage_cap, b->damage_count + 1u, sizeof(ax_rect),
              AX_ALIGNOF(ax_rect), b->damage_count)) {
        return;
    }
    b->damage[b->damage_count++] = rect;
}

void ax_ir_damage_full(ax_ir_builder *b)
{
    b->ir.damage_flags |= AX_IR_DAMAGE_FULL;
}

ax_result ax_ir_builder_end(ax_ir_builder *b, const ax_render_ir **out_ir)
{
    AX_ASSERT_MSG(b->recording, "ax_ir_builder_end without ax_ir_builder_begin");
    *out_ir = NULL;
    if (!b->recording) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "render IR: end without begin");
    }
    b->recording = false;
    if (b->error == AX_OK) {
        close_implicit(b);
        AX_ASSERT_MSG(b->depth == 1, "ax_ir_builder_end with layers still pushed");
        if (b->depth != 1) {
            b->error = AX_ERR_INVALID_ARGUMENT;
        }
    }
    if (b->error != AX_OK) {
        return ax_set_error(b->error, "render IR: recording failed (%s)",
                            b->error == AX_ERR_OUT_OF_MEMORY ? "out of memory"
                                                             : "unbalanced or oversized");
    }
    close_layer(b);

    b->peak_layers = max_u32(b->peak_layers, b->layer_count);
    b->peak_items = max_u32(b->peak_items, b->items_size);
    b->peak_damage = max_u32(b->peak_damage, b->damage_count);
    b->peak_depth = max_u32(b->peak_depth, b->stack_cap);

    if (b->damage_count == 0) {
        b->ir.damage_flags |= AX_IR_DAMAGE_FULL;
    }
    b->ir.damage_count = b->damage_count;
    b->ir.damage = b->damage;
    b->ir.layer_count = b->layer_count;
    b->ir.layers = b->layers;
    b->ir.items_size = b->items_size;
    b->ir.items = b->items;
#if AX_ENABLE_ASSERTS
    AX_ASSERT(ax_ir_validate(&b->ir) == AX_OK);
#endif
    *out_ir = &b->ir;
    return AX_OK;
}

void ax_ir_glyph_run(ax_ir_builder *b, const ax_ir_glyph_run_item *item, const ax_ir_glyph *glyphs,
                     uint32_t glyph_count)
{
    const uint32_t fixed = (uint32_t)(sizeof *item - sizeof item->header);
    uint8_t *payload;
    uint32_t bytes;
    if (glyph_count > (UINT32_MAX - (uint32_t)sizeof *item) / (uint32_t)sizeof(ax_ir_glyph)) {
        if (b->error == AX_OK) {
            b->error = ax_set_error(AX_ERR_UNSUPPORTED, "ax_ir_glyph_run: too many glyphs");
        }
        return;
    }
    bytes = fixed + glyph_count * (uint32_t)sizeof(ax_ir_glyph);
    payload = (uint8_t *)ax_arena_alloc(&b->arena, bytes > 0 ? bytes : 1, 8);
    if (payload == NULL) {
        if (b->error == AX_OK) {
            b->error = ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_ir_glyph_run: out of memory");
        }
        return;
    }
    memcpy(payload, (const uint8_t *)item + sizeof item->header, fixed);
    memcpy(payload + offsetof(ax_ir_glyph_run_item, glyph_count) - sizeof item->header,
           &glyph_count, sizeof glyph_count);
    if (glyph_count > 0) {
        memcpy(payload + fixed, glyphs, sizeof(ax_ir_glyph) * glyph_count);
    }
    ax_ir_item(b, AX_IR_ITEM_GLYPH_RUN, item->header.flags, payload, bytes);
}

void ax_ir_builder_set_resources(ax_ir_builder *b, const ax_ir_glyph_source *glyphs,
                                 uint64_t resource_epoch)
{
    b->ir.glyphs = glyphs;
    b->ir.resource_epoch = resource_epoch;
}
