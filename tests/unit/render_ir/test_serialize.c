/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_tracking_allocator.h"
#include "axion/render/ax_render_ir.h"
#include "ir_test_util.h"

static const float k_radii[4] = {2.0f, 4.0f, 8.0f, 0.5f};

/* Every feature the format has: nested and implicit layers, transforms, clips of each kind,
 * opacity, blend modes, linear blending, unknown items (one REQUIRED), damage rects, and colors
 * outside [0, 1]. */
static const ax_render_ir *record_everything(ax_ir_builder *b)
{
    const ax_render_ir *ir = NULL;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const uint8_t blob[5] = {9, 8, 7, 6, 5};

    ax_ir_builder_begin(b, 1093.5f, 636.25f, 123456789012345ull,
                        ax_color_make(-0.25f, 0.5f, 1.75f, 1.0f));
    ax_ir_damage_rect(b, ax_rect_make(1, 2, 3, 4));
    ax_ir_damage_rect(b, ax_rect_make(-5, 6.5f, 70, 8));
    ax_ir_rect(b, ax_rect_make(0.125f, 0.25f, 100, 50), ax_color_make(0.5f, 0, 0, 0.5f));
    d.transform = ax_affine2_mul(ax_affine2_translate(10, 20), ax_affine2_rotate(0.5f));
    d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
    d.clip.rect = ax_rect_make(0, 0, 40, 30);
    memcpy(d.clip.radii, k_radii, sizeof k_radii);
    d.opacity = 0.75f;
    d.blend_mode = AX_IR_BLEND_SCREEN;
    d.flags = AX_IR_LAYER_LINEAR_BLEND;
    ax_ir_push_layer(b, &d);
    ax_ir_rounded_rect(b, ax_rect_make(1, 1, 38, 28), k_radii, ax_color_make(2.0f, 1, 0, 1));
    ax_ir_item(b, 4242, 0, blob, sizeof blob);
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_PATH;
    d.clip.path = 3;
    d.blend_mode = 99; /* unknown to this build */
    ax_ir_push_layer(b, &d);
    ax_ir_item(b, 4243, AX_IR_ITEM_REQUIRED, NULL, 0);
    ax_ir_pop_layer(b);
    ax_ir_rect(b, ax_rect_make(3, 3, 0.25f, 9), ax_color_make(0, 0, 0, 0));
    ax_ir_pop_layer(b);
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_RECT;
    d.clip.rect = ax_rect_make(5, 5, 5, 5);
    d.clip.path = AX_IR_NO_INDEX;
    ax_ir_push_layer(b, &d);
    ax_ir_pop_layer(b);
    if (ax_ir_builder_end(b, &ir) != AX_OK) {
        return NULL;
    }
    return ir;
}

static uint8_t *serialize(const ax_render_ir *ir, size_t *size)
{
    uint8_t *buf;
    *size = ax_ir_serialized_size(ir);
    buf = (uint8_t *)malloc(*size);
    if (buf != NULL && ax_ir_serialize(ir, buf, *size, size) != AX_OK) {
        free(buf);
        return NULL;
    }
    return buf;
}

AX_TEST(round_trip_is_byte_exact)
{
    static axt_text before, after;
    ax_ir_builder b;
    ax_arena arena;
    const ax_render_ir *ir, *back;
    uint8_t *bytes, *again;
    size_t size, size2;

    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    ir = record_everything(&b);
    AX_REQUIRE(ir != NULL);
    bytes = serialize(ir, &size);
    AX_REQUIRE(bytes != NULL);
    AX_CHECK_EQ_U(size, 96 + 2 * 16 + ir->layer_count * 100 + ir->items_size); /* no glyphs */

    AX_REQUIRE(ax_ir_deserialize(bytes, size, &arena, &back) == AX_OK);
    AX_CHECK_EQ_U(back->layer_count, ir->layer_count);
    AX_CHECK_EQ_U(back->items_size, ir->items_size);
    AX_CHECK_EQ_U(back->frame_time_ns, 123456789012345ull);
    AX_CHECK_EQ_U(back->damage_count, 2);
    AX_CHECK(memcmp(back->items, ir->items, ir->items_size) == 0); /* unknown items kept */
    AX_CHECK(memcmp(ax_ir_layer_at(back, 1), ax_ir_layer_at(ir, 1), sizeof(ax_ir_layer)) == 0);

    again = serialize(back, &size2);
    AX_REQUIRE(again != NULL);
    AX_CHECK_EQ_U(size2, size);
    AX_CHECK(memcmp(again, bytes, size) == 0);

    axt_dump(ir, &before);
    axt_dump(back, &after);
    AX_CHECK(strcmp(before.buf, after.buf) == 0);
    AX_CHECK(strstr(before.buf, "item type 4243 flags 0x1 size 8") != NULL);
    AX_CHECK(strstr(before.buf, "blend 99") != NULL);
    AX_CHECK(strstr(before.buf, "clip path 3") != NULL);

    free(again);
    free(bytes);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}

AX_TEST(header_layout_is_pinned)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    uint8_t *bytes;
    size_t size;
    uint32_t u;
    float f;

    ax_ir_builder_init(&b, ax_default_allocator());
    ir = record_everything(&b);
    AX_REQUIRE(ir != NULL);
    bytes = serialize(ir, &size);
    AX_REQUIRE(bytes != NULL);
    AX_CHECK(memcmp(bytes, "AXIR", 4) == 0);
    AX_CHECK_EQ_U(bytes[4] | (bytes[5] << 8), AX_IR_VERSION_MAJOR);
    AX_CHECK_EQ_U(bytes[8], 96); /* header_size: minor 1, an empty glyph table */
    memcpy(&f, bytes + 16, 4);
    AX_CHECK_NEAR_F(f, 1093.5f, 0.0f);
    {
        uint64_t t;
        memcpy(&t, bytes + 24, 8);
        AX_CHECK_EQ_U(t, 123456789012345ull);
    }
    memcpy(&u, bytes + 60, 4);
    AX_CHECK_EQ_U(u, 2); /* damage_count */
    memcpy(&u, bytes + 68, 4);
    AX_CHECK_EQ_U(u, 100); /* layer stride */
    /* The first layer record follows the damage rects: the root, parent = none. */
    memcpy(&u, bytes + 96 + 32, 4);
    AX_CHECK_EQ_U(u, AX_IR_NO_LAYER);
    free(bytes);
    ax_ir_builder_deinit(&b);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

AX_TEST(newer_minor_with_larger_records_reads)
{
    /* Rewrite a snapshot the way a writer of minor + 1 would: a header 8 bytes larger and layer
     * records 12 bytes larger, the extra bytes holding fields this build does not know. */
    static axt_text before, after;
    ax_ir_builder b;
    ax_arena arena;
    const ax_render_ir *ir, *back;
    uint8_t *bytes, *newer, *w;
    const uint8_t *r;
    size_t size, newer_size;
    uint32_t i;

    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    ir = record_everything(&b);
    AX_REQUIRE(ir != NULL);
    bytes = serialize(ir, &size);
    AX_REQUIRE(bytes != NULL);
    newer_size = size + 8 + 12 * ir->layer_count;
    newer = (uint8_t *)malloc(newer_size);
    AX_REQUIRE(newer != NULL);
    memset(newer, 0xEE, newer_size);
    memcpy(newer, bytes, 96);
    newer[6] = (uint8_t)(AX_IR_VERSION_MINOR + 1);
    put_u32(newer + 8, 104);
    put_u32(newer + 68, 112);
    w = newer + 104;
    r = bytes + 96;
    memcpy(w, r, 2 * 16);
    w += 32;
    r += 32;
    for (i = 0; i < ir->layer_count; ++i, w += 112, r += 100) {
        memcpy(w, r, 100);
    }
    memcpy(w, r, ir->items_size);

    AX_REQUIRE(ax_ir_deserialize(newer, newer_size, &arena, &back) == AX_OK);
    AX_CHECK_EQ_U(back->version_minor, AX_IR_VERSION_MINOR + 1);
    AX_CHECK_EQ_U(back->layer_count, ir->layer_count);
    axt_dump(ir, &before);
    axt_dump(back, &after);
    /* Identical apart from the version in the first line. */
    AX_CHECK(strcmp(strchr(before.buf, '\n'), strchr(after.buf, '\n')) == 0);

    newer[4] = (uint8_t)(AX_IR_VERSION_MAJOR + 1);
    AX_CHECK_RESULT(ax_ir_deserialize(newer, newer_size, &arena, &back), AX_ERR_ABI_MISMATCH);
    AX_CHECK(back == NULL);

    free(newer);
    free(bytes);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}

AX_TEST(truncated_and_corrupted_input_is_rejected)
{
    ax_ir_builder b;
    ax_arena arena;
    const ax_render_ir *ir, *back;
    uint8_t *bytes, *copy;
    size_t size, n, i;
    unsigned seed = 12345u;
    int accepted = 0;

    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    ir = record_everything(&b);
    AX_REQUIRE(ir != NULL);
    bytes = serialize(ir, &size);
    copy = (uint8_t *)malloc(size);
    AX_REQUIRE(bytes != NULL && copy != NULL);

    /* Every prefix is rejected, never read past. */
    for (n = 0; n < size; ++n) {
        memcpy(copy, bytes, n);
        AX_REQUIRE(ax_ir_deserialize(copy, n, &arena, &back) == AX_ERR_INVALID_ARGUMENT);
        ax_arena_reset(&arena);
    }
    AX_CHECK_RESULT(ax_ir_deserialize(NULL, 0, &arena, &back), AX_ERR_INVALID_ARGUMENT);

    /* Random byte corruption: either rejected, or accepted as a valid snapshot (when the bytes
     * hit floats or padding); sanitizers check nothing is read out of bounds. */
    for (i = 0; i < 4000; ++i) {
        ax_result r;
        memcpy(copy, bytes, size);
        seed = seed * 1103515245u + 12345u;
        copy[(seed >> 8) % size] ^= (uint8_t)(1u << ((seed >> 4) % 8));
        r = ax_ir_deserialize(copy, size, &arena, &back);
        AX_REQUIRE(r == AX_OK || r == AX_ERR_INVALID_ARGUMENT || r == AX_ERR_ABI_MISMATCH);
        if (r == AX_OK) {
            AX_REQUIRE(ax_ir_validate(back) == AX_OK);
            ++accepted;
        }
        ax_arena_reset(&arena);
    }
    AX_CHECK(accepted > 0 && accepted < 4000);

    free(copy);
    free(bytes);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}

AX_TEST(small_buffer_writes_nothing)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    uint8_t buf[64];
    size_t need = 0;

    ax_ir_builder_init(&b, ax_default_allocator());
    ir = record_everything(&b);
    AX_REQUIRE(ir != NULL);
    memset(buf, 0x5A, sizeof buf);
    AX_CHECK_RESULT(ax_ir_serialize(ir, buf, sizeof buf, &need), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK_EQ_U(need, ax_ir_serialized_size(ir));
    AX_CHECK_EQ_U(buf[0], 0x5A);
    ax_ir_builder_deinit(&b);
}

/* A snapshot written by minor 0 (an 80-byte header, no glyph table) still reads. */
AX_TEST(a_minor_0_snapshot_still_reads)
{
    ax_ir_builder b;
    ax_arena arena;
    const ax_render_ir *ir, *back;
    uint8_t *bytes, *old;
    size_t size, old_size;
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    ir = record_everything(&b);
    AX_REQUIRE(ir != NULL);
    bytes = serialize(ir, &size);
    AX_REQUIRE(bytes != NULL);
    old_size = size - 16;
    old = (uint8_t *)malloc(old_size);
    AX_REQUIRE(old != NULL);
    memcpy(old, bytes, 80);
    old[6] = 0;
    put_u32(old + 8, 80);
    memcpy(old + 80, bytes + 96, size - 96);
    AX_REQUIRE(ax_ir_deserialize(old, old_size, &arena, &back) == AX_OK);
    AX_CHECK_EQ_U(back->version_minor, 0);
    AX_CHECK(back->glyphs == NULL);
    AX_CHECK_EQ_U(back->items_size, ir->items_size);
    free(old);
    free(bytes);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}
