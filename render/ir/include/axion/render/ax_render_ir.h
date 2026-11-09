/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (for now): the Render IR, the contract between the scene and every renderer.
 *
 * A snapshot (ax_render_ir) is immutable plain-old-data: a layer tree and, per layer, a stream of
 * draw items. It holds indices and byte offsets, never pointers between its parts, so it can be
 * serialized, replayed in tests, inspected and sent to another process. It is versioned, and
 * meant to be consumed by renderers built separately from the core (render_gl and other plugins),
 * so its layout only grows in compatible ways (see "Compatibility" below).
 *
 * Units. Every coordinate is in logical units, in the space of the layer that holds it. The
 * snapshot records the logical size it was recorded for; a renderer maps logical units to device
 * pixels with the surface's scale and never stretches a snapshot to fit another size.
 *
 * Color. ax_color (axion/base/ax_math.h): linear-light, premultiplied, extended-range RGBA. How
 * colors blend (sRGB space by default, or linear light) is decided per layer by the renderer.
 *
 * Layers are stored in pre-order: a layer's descendants follow it, up to `subtree_end`. Painting
 * order is: a layer's own items, in stream order, then its children in order. Layer 0 is the root.
 *
 * Compatibility (major.minor, AX_IR_VERSION_*):
 *   - A different major is a different format: consumers reject it (AX_ERR_ABI_MISMATCH).
 *   - A newer minor only adds: fields at the end of the layer record (layers are indexed with
 *     `layer_stride`, never sizeof), fields at the end of item records (items are stepped with
 *     their `size`), new item types, clip kinds, blend modes and header fields.
 *   - A consumer handles what it does not know as follows, and counts each case:
 *       * an item of an unknown type is skipped;
 *       * an unknown item with AX_IR_ITEM_REQUIRED set makes the consumer skip the rest of that
 *         layer's own items (its children are still drawn): for items that change how their
 *         siblings draw, where drawing the siblings alone would be wrong;
 *       * a clip it cannot apply skips the layer and its whole subtree, since drawing unclipped
 *         content could cover what the clip protects;
 *       * an unknown blend mode, or effects it cannot run, draw the layer as if they were
 *         neutral (normal blending, no effects).
 *     A consumer never draws a known item from a prefix it cannot validate.
 */
#ifndef AX_RENDER_IR_H
#define AX_RENDER_IR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/ax_types.h>
#include <axion/base/ax_arena.h>
#include <axion/base/ax_math.h>

AX_EXTERN_C_BEGIN

#define AX_IR_VERSION_MAJOR 1u
#define AX_IR_VERSION_MINOR 1u /* 1.1: glyph runs, the glyph source, resource epochs */

#define AX_IR_NO_LAYER UINT32_MAX
#define AX_IR_NO_INDEX UINT32_MAX

/* ---- layers ---------------------------------------------------------------------------------- */

typedef enum ax_ir_clip_kind {
    AX_IR_CLIP_NONE = 0,
    AX_IR_CLIP_RECT = 1,         /* rect, in the layer's space */
    AX_IR_CLIP_ROUNDED_RECT = 2, /* rect and radii */
    AX_IR_CLIP_PATH = 3          /* path index (paths are not in the format yet) */
} ax_ir_clip_kind;

typedef struct ax_ir_clip {
    uint32_t kind; /* ax_ir_clip_kind */
    uint32_t path; /* AX_IR_CLIP_PATH: index of the path; AX_IR_NO_INDEX otherwise */
    ax_rect rect;
    float radii[4]; /* top-left, top-right, bottom-right, bottom-left */
} ax_ir_clip;

typedef enum ax_ir_blend_mode {
    AX_IR_BLEND_NORMAL = 0, /* source over */
    AX_IR_BLEND_MULTIPLY = 1,
    AX_IR_BLEND_SCREEN = 2,
    AX_IR_BLEND_OVERLAY = 3,
    AX_IR_BLEND_DARKEN = 4,
    AX_IR_BLEND_LIGHTEN = 5,
    AX_IR_BLEND_PLUS = 6
} ax_ir_blend_mode;

/* ax_ir_layer.flags */
#define AX_IR_LAYER_IMPLICIT 0x1u /* made by the builder for items recorded after a child */
#define AX_IR_LAYER_LINEAR_BLEND \
    0x2u /* blend this layer's content in linear light (issue #4's per-layer option) */

typedef struct ax_ir_layer {
    uint32_t parent;       /* AX_IR_NO_LAYER for the root */
    uint32_t subtree_end;  /* one past the last descendant's index */
    uint32_t items_offset; /* byte offset of the layer's own items in the item stream */
    uint32_t items_size;   /* bytes of the layer's own items */
    uint32_t item_count;
    uint32_t flags;       /* AX_IR_LAYER_* */
    ax_affine2 transform; /* layer space -> parent's space */
    ax_ir_clip clip;      /* in the layer's space */
    float opacity;        /* group opacity, [0, 1] */
    uint32_t blend_mode;  /* ax_ir_blend_mode: how the layer composites onto what is below */
    uint32_t effects;     /* index of the layer's effect chain; AX_IR_NO_INDEX (none in M0) */
} ax_ir_layer;

/* ---- draw items ------------------------------------------------------------------------------ */

/* Item types. Numbers are stable; the document's other items have their numbers reserved. */
typedef enum ax_ir_item_type {
    AX_IR_ITEM_RECT = 1,
    AX_IR_ITEM_ROUNDED_RECT = 2,
    AX_IR_ITEM_GLYPH_RUN = 10, /* minor 1 (decision 101) */
    /* Reserved: 3 border, 4 box shadow, 5 linear gradient, 6 radial gradient, 7 conic gradient,
     * 8 path, 9 image, 11 user shader quad, 12 backdrop-filter region,
     * 13 3D viewport. 0x8000 and above: private to a consumer, never written by AXION. */
    AX_IR_ITEM_FIRST_PRIVATE = 0x8000
} ax_ir_item_type;

/* ax_ir_item_header.flags */
#define AX_IR_ITEM_REQUIRED 0x1u /* see "Compatibility": a consumer must understand this item */

/* Every item record starts with this header. Records are 8-byte aligned and `size` (header
 * included) is a multiple of 8. */
typedef struct ax_ir_item_header {
    uint16_t type;  /* ax_ir_item_type */
    uint16_t flags; /* AX_IR_ITEM_* */
    uint32_t size;  /* bytes of the whole record */
} ax_ir_item_header;

#define AX_IR_ITEM_ALIGN 8u

/* A solid rectangle. */
typedef struct ax_ir_rect_item {
    ax_ir_item_header header;
    ax_rect rect;
    ax_color color;
} ax_ir_rect_item;

/* A solid rectangle with a circular radius per corner. Radii are clamped by the renderer the way
 * CSS does: when two radii on one side add up to more than the side, all four are scaled down by
 * the same factor. */
typedef struct ax_ir_rounded_rect_item {
    ax_ir_item_header header;
    ax_rect rect;
    float radii[4]; /* top-left, top-right, bottom-right, bottom-left */
    ax_color color;
} ax_ir_rounded_rect_item;

/*
 * A run of glyphs of one font instance at one raster scale, in one color (minor 1, decision 101).
 * glyph_count ax_ir_glyph records follow the fixed part.
 *
 * Positions are integers on the run's raster grid: a glyph's pen position in the layer's space,
 * times raster_scale, which the scene chose so that one raster pixel is one device pixel when the
 * snapshot is drawn at the scale it was recorded for (the window's scale, and the layers' scale at
 * rest). The glyph's bitmap (from the snapshot's glyph source) has its top-left pixel at
 * (x + left, y - top) raster pixels; a renderer maps raster pixels to layer units by dividing by
 * raster_scale, then draws through the layer's transform. When that lands raster pixels exactly on
 * device pixels, the bitmap is copied as it is; otherwise it is sampled bilinearly (a layer
 * scaling or turning under an animation, until the scene records it again at the new scale).
 *
 * Coverage is corrected for the text's luminance (ax_ir_text_lut) before blending. A consumer
 * without a glyph source draws nothing for the run (counted as skipped).
 */
typedef struct ax_ir_glyph_run_item {
    ax_ir_item_header header;
    ax_color color;         /* premultiplied linear light, as every item */
    uint32_t font_instance; /* the glyph source's key for the font at its raster size */
    uint32_t glyph_count;
    float raster_scale; /* raster pixels per layer unit */
    uint32_t flags;     /* none defined; 0 */
} ax_ir_glyph_run_item;

typedef struct ax_ir_glyph {
    uint32_t id;  /* glyph id in the font */
    uint32_t bin; /* subpixel position: the bitmap is rendered bin/4 pixel to the right */
    int32_t x, y; /* pen position (y the baseline) on the raster grid */
} ax_ir_glyph;

/* A glyph's bitmap: 8-bit coverage, rows top-down, top-left pixel at (x + left, y - top). */
typedef struct ax_ir_glyph_bitmap {
    uint16_t width, height;
    int16_t left, top;
    uint32_t stride;
    const uint8_t *pixels;
} ax_ir_glyph_bitmap;

/*
 * Where a snapshot's glyph bitmaps come from: the scene's glyph cache (text), or the table a
 * deserialized snapshot carries. lookup may be called from the render thread at any time while
 * the snapshot is alive, and the bitmaps it returns stay valid as long (the producer keeps every
 * glyph a live snapshot uses: decision 100). False when the glyph has no bitmap.
 */
typedef struct ax_ir_glyph_source {
    bool (*lookup)(const struct ax_ir_glyph_source *self, uint32_t font_instance, uint32_t glyph,
                   uint32_t bin, ax_ir_glyph_bitmap *out);
    void *user;
} ax_ir_glyph_source;

/* ---- text coverage correction ---------------------------------------------------------------- */

#define AX_IR_TEXT_LUT_BUCKETS 8u

/*
 * The correction of glyph coverage for the text's luminance (the document's "gamma and contrast
 * correction tuned to match platform text"; decision 101), shared by every renderer so they draw
 * the same text: alpha = lut[bucket][coverage]. Built like Skia's mask gamma: for a text luminance,
 * the alpha that, blended in the renderer's space against the opposite luminance, gives the blend
 * a `gamma` space would, with `contrast` darkening thin strokes of dark text.
 */
typedef struct ax_ir_text_lut_params {
    float contrast; /* 0..1: thicker stems for dark text */
    float gamma;    /* the space whose blend it imitates: 1 linear light, ~2.2 the sRGB space */
} ax_ir_text_lut_params;

/* AXION's defaults (decision 101). */
AX_API ax_ir_text_lut_params ax_ir_text_lut_default(void);

/* UNSUPPORTED, for development comparisons only (tools/diag/text_weight): replaces the defaults
 * for renderers created afterwards in this process; NULL restores them. thread: before any
 * renderer exists */
AX_API void ax_ir_text_lut_override(const ax_ir_text_lut_params *params);

/* Builds the table for text blended in sRGB space (AX_IR_LUT_TEXT_SRGB). Deterministic (base's
 * own exp2 and log2). thread: any */
AX_API void ax_ir_text_lut_build(const ax_ir_text_lut_params *params,
                                 uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256]);

/*
 * Coverage correction tables (issue #4's comparison). The same construction as the text table,
 * for what a renderer blends and in which space:
 *   AX_IR_LUT_TEXT_SRGB    glyphs blended in sRGB space (the default; decision 101);
 *   AX_IR_LUT_TEXT_LINEAR  glyphs blended in linear light (AX_RENDER_BLEND_LINEAR, and layers with
 *                          AX_IR_LAYER_LINEAR_BLEND): the alpha that gives, against the opposite
 *                          luminance, the blend the params' gamma space would;
 *   AX_IR_LUT_SHAPE_SRGB   rects and rounded rects blended in sRGB space with
 *                          AX_RENDER_BLEND_SRGB_CORRECTED: by default the blend linear light would
 *                          give (gamma 1), against the opposite luminance, so thin lines do not
 *                          rope while translucency stays in sRGB space.
 * Rows are AX_IR_TEXT_LUT_BUCKETS luminances of the item's color (ax_ir_text_lut_bucket).
 */
typedef enum ax_ir_lut_table {
    AX_IR_LUT_TEXT_SRGB = 0,
    AX_IR_LUT_TEXT_LINEAR = 1,
    AX_IR_LUT_SHAPE_SRGB = 2,
    AX_IR_LUT_TABLES = 3
} ax_ir_lut_table;

/* A table's defaults; AX_IR_LUT_TEXT_SRGB's is ax_ir_text_lut_default(). */
AX_API ax_ir_text_lut_params ax_ir_lut_default(ax_ir_lut_table table);

/* UNSUPPORTED, as ax_ir_text_lut_override, for one table (tools/diag/blend_measure). */
AX_API void ax_ir_lut_override(ax_ir_lut_table table, const ax_ir_text_lut_params *params);

/* Builds a table, in its blend space. thread: any */
AX_API void ax_ir_lut_build(ax_ir_lut_table table, const ax_ir_text_lut_params *params,
                            uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256]);

/* The bucket of a text color (premultiplied linear): its luminance, quantized. */
AX_API uint32_t ax_ir_text_lut_bucket(ax_color color);

/* ---- the snapshot ------------------------------------------------------------------------------
 */

/* ax_render_ir.damage_flags */
#define AX_IR_DAMAGE_FULL 0x1u /* everything changed; damage rects are ignored */

typedef struct ax_render_ir {
    uint32_t struct_size; /* sizeof(ax_render_ir) of the producer */
    uint16_t version_major;
    uint16_t version_minor;
    float logical_width; /* the size it was recorded for, in logical units */
    float logical_height;
    uint64_t frame_time_ns; /* the predicted present time it was recorded for */
    uint64_t sequence;      /* publication number (ax_ir_mailbox), 0 when not published */
    ax_color background;    /* fills the surface under the root layer */
    uint32_t damage_flags;  /* AX_IR_DAMAGE_* */
    uint32_t damage_count;
    const ax_rect *damage; /* changed regions, in root logical units */
    uint32_t layer_count;  /* at least 1: layer 0 is the root */
    uint32_t layer_stride; /* bytes between layer records; >= the fields a consumer reads */
    const void *layers;    /* use ax_ir_layer_at */
    uint32_t items_size;   /* bytes in the item stream */
    const uint8_t *items;
    /* Minor 1 (check struct_size). */
    const ax_ir_glyph_source *glyphs; /* the glyph runs' bitmaps; NULL without glyph runs */
    uint64_t resource_epoch;          /* the epoch the producer stamped the glyphs it uses with */
} ax_render_ir;

static inline const ax_ir_layer *ax_ir_layer_at(const ax_render_ir *ir, uint32_t index)
{
    return (const ax_ir_layer *)(const void *)((const uint8_t *)ir->layers +
                                               (size_t)index * ir->layer_stride);
}

/* Walks one layer's own items. */
typedef struct ax_ir_item_iter {
    const uint8_t *next;
    const uint8_t *end;
} ax_ir_item_iter;

static inline ax_ir_item_iter ax_ir_items(const ax_render_ir *ir, const ax_ir_layer *layer)
{
    ax_ir_item_iter it;
    it.next = ir->items + layer->items_offset;
    it.end = it.next + layer->items_size;
    return it;
}

/* The next item, NULL at the end. The snapshot must be valid (built, or deserialized). */
static inline const ax_ir_item_header *ax_ir_item_next(ax_ir_item_iter *it)
{
    const ax_ir_item_header *h;
    if (it->next >= it->end) {
        return NULL;
    }
    h = (const ax_ir_item_header *)(const void *)it->next;
    it->next += h->size;
    return h;
}

/* Checks the structure: version, tree shape, item ranges and record sizes, and that every known
 * item is at least as large as its record. AX_ERR_ABI_MISMATCH for another major, otherwise
 * AX_ERR_INVALID_ARGUMENT with ax_last_error() saying what is wrong. thread: any */
AX_API ax_result ax_ir_validate(const ax_render_ir *ir);

/* ---- building ---------------------------------------------------------------------------------
 */

/*
 * Records one snapshot into memory it owns (an arena), without an allocation per item: arrays
 * grow geometrically inside the arena, and each frame reserves what the largest previous frame
 * needed, so once warmed up a frame allocates nothing from the parent allocator.
 *
 *     ax_ir_builder_begin(b, width, height, frame_time_ns, background);
 *     ax_ir_rounded_rect(b, rect, radii, color);       // into the root layer
 *     ax_ir_push_layer(b, &layer_desc);                // a child of the root
 *     ax_ir_rect(b, rect, color);
 *     ax_ir_pop_layer(b);
 *     ax_ir_builder_end(b, &ir);                       // valid until the next begin
 *
 * Errors are sticky: recording calls return nothing, and ax_ir_builder_end reports the first one.
 * Items recorded into a layer after one of its children become an implicit child layer (identity,
 * AX_IR_LAYER_IMPLICIT) so painting order is kept.
 *
 * One thread at a time. The struct is visible so it can be embedded; its fields are private.
 */
typedef struct ax_ir_builder {
    ax_arena arena;
    ax_render_ir ir;
    ax_ir_layer *layers;
    uint32_t layer_count, layer_cap;
    uint8_t *items;
    uint32_t items_size, items_cap;
    ax_rect *damage;
    uint32_t damage_count, damage_cap;
    uint32_t *stack; /* open layers, innermost last */
    uint32_t depth, stack_cap;
    uint32_t peak_layers, peak_items, peak_damage, peak_depth; /* reserved at begin */
    ax_result error;
    bool recording;
} ax_ir_builder;

/* The properties of a pushed layer. ax_ir_layer_desc_default gives the neutral ones. */
typedef struct ax_ir_layer_desc {
    ax_affine2 transform;
    ax_ir_clip clip;
    float opacity;
    uint32_t blend_mode;
    uint32_t flags; /* AX_IR_LAYER_LINEAR_BLEND */
} ax_ir_layer_desc;

AX_API ax_ir_layer_desc ax_ir_layer_desc_default(void);

/* parent is copied. Nothing is allocated until the first begin. thread: any */
AX_API void ax_ir_builder_init(ax_ir_builder *b, const ax_allocator *parent);
AX_API void ax_ir_builder_deinit(ax_ir_builder *b);

/* Starts a snapshot, forgetting the previous one, with the root layer open. thread: any */
AX_API void ax_ir_builder_begin(ax_ir_builder *b, float logical_width, float logical_height,
                                uint64_t frame_time_ns, ax_color background);

AX_API void ax_ir_push_layer(ax_ir_builder *b, const ax_ir_layer_desc *desc);
AX_API void ax_ir_pop_layer(ax_ir_builder *b);

AX_API void ax_ir_rect(ax_ir_builder *b, ax_rect rect, ax_color color);
AX_API void ax_ir_rounded_rect(ax_ir_builder *b, ax_rect rect, const float radii[4],
                               ax_color color);

/* A glyph run (see ax_ir_glyph_run_item): `item`'s header and glyph_count are filled in. */
AX_API void ax_ir_glyph_run(ax_ir_builder *b, const ax_ir_glyph_run_item *item,
                            const ax_ir_glyph *glyphs, uint32_t glyph_count);

/* The snapshot's glyph source and resource epoch (decision 100), kept until the next begin. */
AX_API void ax_ir_builder_set_resources(ax_ir_builder *b, const ax_ir_glyph_source *glyphs,
                                        uint64_t resource_epoch);

/* Any item record: `payload_size` bytes follow the header, padded to AX_IR_ITEM_ALIGN. For item
 * types without a helper, and for tests. */
AX_API void ax_ir_item(ax_ir_builder *b, uint16_t type, uint16_t flags, const void *payload,
                       uint32_t payload_size);

/* Adds a damaged region. Without one, or after ax_ir_damage_full, the damage is full (M0 records
 * full damage only). */
AX_API void ax_ir_damage_rect(ax_ir_builder *b, ax_rect rect);
AX_API void ax_ir_damage_full(ax_ir_builder *b);

/*
 * Closes the root layer and returns the snapshot, valid until the next ax_ir_builder_begin or
 * ax_ir_builder_deinit. Every pushed layer must have been popped.
 * Errors: AX_ERR_OUT_OF_MEMORY, AX_ERR_INVALID_ARGUMENT (unbalanced layers, end without begin),
 * AX_ERR_UNSUPPORTED (a layer or stream too large for 32-bit offsets).
 */
AX_API ax_result ax_ir_builder_end(ax_ir_builder *b, const ax_render_ir **out_ir);

/* ---- serialization ----------------------------------------------------------------------------
 */

/*
 * The binary form: little-endian, with explicit sizes so it reads across versions:
 *     header   "AXIR", u16 major, u16 minor, u32 header_size, then the snapshot's fields; since
 *              minor 1 also the resource epoch and the glyph table: every glyph the snapshot's
 *              runs use, with its bitmap, so a serialized snapshot draws on its own (decision 101)
 *     damage   damage_count rects (16 bytes each)
 *     layers   layer_count records of layer_stride bytes
 *     items    items_size bytes, the item stream unchanged (unknown items are kept)
 * A reader of minor 0 skips the larger header, glyph table included. The exact layout is in
 * render/ir/src/serialize.c.
 */

/* Bytes ax_ir_serialize writes for this snapshot. thread: any */
AX_API size_t ax_ir_serialized_size(const ax_render_ir *ir);

/* Writes the binary form into out (capacity bytes). Errors: AX_ERR_OUT_OF_MEMORY when capacity
 * is too small (nothing is written). thread: any */
AX_API ax_result ax_ir_serialize(const ax_render_ir *ir, void *out, size_t capacity,
                                 size_t *out_size);

/*
 * Reads a binary snapshot into memory from `arena`, validated (ax_ir_validate). A glyph table
 * becomes the snapshot's glyph source (in the arena). A newer minor's
 * extra layer fields are dropped and a missing field of an older minor takes its neutral value;
 * the item stream is kept as it is.
 * Errors: AX_ERR_ABI_MISMATCH (another major), AX_ERR_INVALID_ARGUMENT (not a snapshot, truncated or
 * malformed; ax_last_error() says why), AX_ERR_UNSUPPORTED (a big-endian host),
 * AX_ERR_OUT_OF_MEMORY.
 * thread: any
 */
AX_API ax_result ax_ir_deserialize(const void *data, size_t size, ax_arena *arena,
                                   const ax_render_ir **out_ir);

/* ---- text dump ------------------------------------------------------------------------------- */

typedef void (*ax_ir_write_fn)(void *ctx, const char *text, size_t length);

/* A readable, indented description of the snapshot, for debugging and test expectations: one line
 * per layer and per item, numbers with 6 significant digits. thread: any */
AX_API void ax_ir_dump(const ax_render_ir *ir, ax_ir_write_fn write, void *ctx);

AX_EXTERN_C_END

#endif /* AX_RENDER_IR_H */
