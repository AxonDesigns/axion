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

AX_TEST(empty_snapshot_has_only_the_root)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 800.0f, 500.0f, 1000u, ax_color_make(0.1f, 0.2f, 0.3f, 1.0f));
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    AX_CHECK_EQ_U(ir->version_major, AX_IR_VERSION_MAJOR);
    AX_CHECK_EQ_U(ir->version_minor, AX_IR_VERSION_MINOR);
    AX_CHECK_EQ_U(ir->layer_count, 1);
    AX_CHECK_EQ_U(ir->items_size, 0);
    AX_CHECK_EQ_U(ir->damage_flags, AX_IR_DAMAGE_FULL);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 0)->parent, AX_IR_NO_LAYER);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 0)->subtree_end, 1);
    AX_CHECK_RESULT(ax_ir_validate(ir), AX_OK);
    axt_check_dump(ir, "render_ir 1.0 size 800x500 time_ns 1000 seq 0 background (0.1 0.2 0.3 1)\n"
                       "damage full\n"
                       "layer 0 root items 0\n");
    ax_ir_builder_deinit(&b);
}

AX_TEST(layers_items_and_painting_order)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const ax_color red = ax_color_make(1, 0, 0, 1);

    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 320.0f, 240.0f, 7u, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&b, ax_rect_make(0, 0, 10, 10), red); /* A: root */
    d.transform = ax_affine2_translate(5.0f, 6.0f);
    d.opacity = 0.5f;
    ax_ir_push_layer(&b, &d);                                       /* L1 */
    ax_ir_rounded_rect(&b, ax_rect_make(1, 2, 3, 4), k_radii, red); /* B */
    d = ax_ir_layer_desc_default();
    d.transform = ax_affine2_scale(2, 2);
    d.blend_mode = AX_IR_BLEND_MULTIPLY;
    ax_ir_push_layer(&b, &d);                             /* L2 */
    ax_ir_rect(&b, ax_rect_make(0.25f, 0.5f, 1, 1), red); /* C */
    ax_ir_pop_layer(&b);
    ax_ir_rect(&b, ax_rect_make(9, 9, 1, 1), red); /* D: after a child -> implicit layer */
    ax_ir_rect(&b, ax_rect_make(8, 8, 1, 1), red); /* still the same implicit layer */
    ax_ir_pop_layer(&b);
    ax_ir_rect(&b, ax_rect_make(7, 7, 1, 1), red); /* E: implicit layer under the root */
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);

    AX_CHECK_EQ_U(ir->layer_count, 5);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 0)->subtree_end, 5);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 1)->parent, 0);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 1)->subtree_end, 4);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 2)->parent, 1);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 2)->subtree_end, 3);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 3)->parent, 1);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 3)->flags, AX_IR_LAYER_IMPLICIT);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 3)->item_count, 2);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 4)->parent, 0);
    AX_CHECK_EQ_U(ax_ir_layer_at(ir, 4)->flags, AX_IR_LAYER_IMPLICIT);
    AX_CHECK_EQ_U(ir->items_size, 5 * sizeof(ax_ir_rect_item) + sizeof(ax_ir_rounded_rect_item));
    AX_CHECK_RESULT(ax_ir_validate(ir), AX_OK);
    axt_check_dump(ir, "render_ir 1.0 size 320x240 time_ns 7 seq 0 background (0 0 0 1)\n"
                       "damage full\n"
                       "layer 0 root items 1\n"
                       "  rect (0 0 10 10) color (1 0 0 1)\n"
                       "  layer 1 transform (1 0 0 1 5 6) opacity 0.5 items 1\n"
                       "    rounded_rect (1 2 3 4) radii (2 4 8 0.5) color (1 0 0 1)\n"
                       "    layer 2 transform (2 0 0 2 0 0) blend multiply items 1\n"
                       "      rect (0.25 0.5 1 1) color (1 0 0 1)\n"
                       "    layer 3 implicit items 2\n"
                       "      rect (9 9 1 1) color (1 0 0 1)\n"
                       "      rect (8 8 1 1) color (1 0 0 1)\n"
                       "  layer 4 implicit items 1\n"
                       "    rect (7 7 1 1) color (1 0 0 1)\n");
    ax_ir_builder_deinit(&b);
}

AX_TEST(items_iterate_in_order_with_unknown_types)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_ir_item_iter it;
    const ax_ir_item_header *h;
    const uint8_t payload[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};

    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 0));
    ax_ir_rect(&b, ax_rect_make(0, 0, 1, 1), ax_color_make(1, 1, 1, 1));
    ax_ir_item(&b, 77, 0, payload, sizeof payload);
    ax_ir_item(&b, 78, AX_IR_ITEM_REQUIRED, NULL, 0);
    ax_ir_rounded_rect(&b, ax_rect_make(0, 0, 4, 4), k_radii, ax_color_make(0, 1, 0, 1));
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);

    it = ax_ir_items(ir, ax_ir_layer_at(ir, 0));
    h = ax_ir_item_next(&it);
    AX_REQUIRE(h != NULL);
    AX_CHECK_EQ_U(h->type, AX_IR_ITEM_RECT);
    h = ax_ir_item_next(&it);
    AX_REQUIRE(h != NULL);
    AX_CHECK_EQ_U(h->type, 77);
    AX_CHECK_EQ_U(h->size, 24); /* 8 + 13, padded to 8 */
    AX_CHECK(memcmp((const uint8_t *)h + 8, payload, sizeof payload) == 0);
    AX_CHECK_EQ_U(((const uint8_t *)h)[8 + 13], 0); /* padding is zeroed */
    h = ax_ir_item_next(&it);
    AX_REQUIRE(h != NULL);
    AX_CHECK_EQ_U(h->type, 78);
    AX_CHECK_EQ_U(h->flags, AX_IR_ITEM_REQUIRED);
    AX_CHECK_EQ_U(h->size, 8);
    h = ax_ir_item_next(&it);
    AX_REQUIRE(h != NULL);
    AX_CHECK_EQ_U(h->type, AX_IR_ITEM_ROUNDED_RECT);
    AX_CHECK(ax_ir_item_next(&it) == NULL);
    axt_check_dump(ir, "render_ir 1.0 size 10x10 time_ns 0 seq 0 background (0 0 0 0)\n"
                       "damage full\n"
                       "layer 0 root items 4\n"
                       "  rect (0 0 1 1) color (1 1 1 1)\n"
                       "  item type 77 flags 0x0 size 24\n"
                       "  item type 78 flags 0x1 size 8\n"
                       "  rounded_rect (0 0 4 4) radii (2 4 8 0.5) color (0 1 0 1)\n");
    ax_ir_builder_deinit(&b);
}

AX_TEST(known_item_grown_by_a_newer_minor_is_valid)
{
    /* A newer minor may append fields to a rect: the record is larger, and a consumer reads the
     * prefix it knows and steps by `size`. */
    ax_ir_builder b;
    const ax_render_ir *ir;
    uint8_t payload[sizeof(ax_ir_rect_item) - 8 + 16];
    ax_ir_rect_item known;
    ax_ir_item_iter it;
    const ax_ir_item_header *h;

    memset(payload, 0xAB, sizeof payload);
    known.rect = ax_rect_make(1, 2, 3, 4);
    known.color = ax_color_make(0.5f, 0, 0, 0.5f);
    memcpy(payload, (const uint8_t *)&known + 8, sizeof known - 8);
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_item(&b, AX_IR_ITEM_RECT, 0, payload, sizeof payload);
    ax_ir_rect(&b, ax_rect_make(5, 5, 1, 1), ax_color_make(1, 1, 1, 1));
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    AX_CHECK_RESULT(ax_ir_validate(ir), AX_OK);
    it = ax_ir_items(ir, ax_ir_layer_at(ir, 0));
    h = ax_ir_item_next(&it);
    AX_REQUIRE(h != NULL && h->size == sizeof(ax_ir_rect_item) + 16);
    AX_CHECK_NEAR_F(((const ax_ir_rect_item *)(const void *)h)->rect.w, 3.0f, 0.0f);
    h = ax_ir_item_next(&it);
    AX_REQUIRE(h != NULL);
    AX_CHECK_NEAR_F(((const ax_ir_rect_item *)(const void *)h)->rect.x, 5.0f, 0.0f);
    ax_ir_builder_deinit(&b);
}

AX_TEST(damage_rects_or_full)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_ir_builder_init(&b, ax_default_allocator());

    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_damage_rect(&b, ax_rect_make(1, 2, 3, 4));
    ax_ir_damage_rect(&b, ax_rect_make(5, 6, 1, 1));
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    AX_CHECK_EQ_U(ir->damage_flags, 0);
    AX_CHECK_EQ_U(ir->damage_count, 2);
    AX_CHECK_NEAR_F(ir->damage[1].x, 5.0f, 0.0f);

    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_damage_rect(&b, ax_rect_make(1, 2, 3, 4));
    ax_ir_damage_full(&b);
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    AX_CHECK_EQ_U(ir->damage_flags, AX_IR_DAMAGE_FULL);
    ax_ir_builder_deinit(&b);
}

static void record_scene(ax_ir_builder *b, int n, uint64_t t)
{
    int i;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    ax_ir_builder_begin(b, 800, 500, t, ax_color_make(0, 0, 0, 1));
    for (i = 0; i < n; ++i) {
        if (i % 50 == 0) {
            d.transform = ax_affine2_rotate((float)i * 0.01f);
            ax_ir_push_layer(b, &d);
        }
        if (i % 2 == 0) {
            ax_ir_rect(b, ax_rect_make((float)i, 0, 1, 1), ax_color_make(1, 1, 1, 1));
        } else {
            ax_ir_rounded_rect(b, ax_rect_make((float)i, 0, 4, 4), k_radii,
                               ax_color_make(0, 0, 1, 1));
        }
        if (i % 50 == 49) {
            ax_ir_pop_layer(b);
        }
    }
    if (n % 50 != 0) {
        ax_ir_pop_layer(b);
    }
}

AX_TEST(steady_state_frames_allocate_nothing)
{
    ax_tracking_allocator t;
    ax_allocator a;
    ax_ir_builder b;
    const ax_render_ir *ir;
    size_t allocs_after_warmup;
    int frame;

    ax_tracking_allocator_init(&t, ax_default_allocator());
    a = ax_tracking_allocator_get(&t);
    ax_ir_builder_init(&b, &a);
    /* Warm up: the first frames grow the arrays and the arena. */
    record_scene(&b, 5000, 0);
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    record_scene(&b, 5000, 1);
    AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    allocs_after_warmup = t.alloc_count;
    for (frame = 2; frame < 50; ++frame) {
        record_scene(&b, frame % 2 == 0 ? 5000 : 3000, (uint64_t)frame);
        AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
    }
    AX_CHECK_EQ_U(t.alloc_count, allocs_after_warmup);
    AX_CHECK_EQ_U(ir->layer_count, 1 + 3000 / 50);
    AX_CHECK_RESULT(ax_ir_validate(ir), AX_OK);
    ax_ir_builder_deinit(&b);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&t), 0);
}

static void *fail_alloc(void *ctx, size_t size, size_t align)
{
    (void)ctx;
    (void)size;
    (void)align;
    return NULL;
}

AX_TEST(out_of_memory_is_reported_at_end)
{
    ax_allocator none = {fail_alloc, NULL, NULL, NULL};
    ax_ir_builder b;
    const ax_render_ir *ir = (const ax_render_ir *)&b;
    ax_ir_builder_init(&b, &none);
    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&b, ax_rect_make(0, 0, 1, 1), ax_color_make(1, 1, 1, 1));
    {
        const ax_ir_layer_desc d = ax_ir_layer_desc_default();
        ax_ir_push_layer(&b, &d);
    }
    ax_ir_pop_layer(&b);
    AX_CHECK_RESULT(ax_ir_builder_end(&b, &ir), AX_ERR_OUT_OF_MEMORY);
    AX_CHECK(ir == NULL);
    ax_ir_builder_deinit(&b);
}

AX_DEATH_TEST(pop_without_push_asserts)
{
    ax_ir_builder b;
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_pop_layer(&b);
}

AX_DEATH_TEST(end_with_open_layers_asserts)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_push_layer(&b, &d);
    ax_ir_builder_end(&b, &ir);
}

/* ---- validation of hand-made snapshots ---------------------------------------------------------
 */

typedef struct handmade {
    ax_render_ir ir;
    ax_ir_layer layers[3];
    uint64_t items[sizeof(ax_ir_rect_item) * 2 / 8]; /* 8-byte aligned stream */
} handmade;

/* Root with one rect, and a child with one rect: valid as made. */
static void handmade_init(handmade *h)
{
    ax_ir_rect_item r;
    memset(h, 0, sizeof *h);
    memset(&r, 0, sizeof r);
    r.header.type = AX_IR_ITEM_RECT;
    r.header.size = sizeof r;
    memcpy((uint8_t *)h->items, &r, sizeof r);
    memcpy((uint8_t *)h->items + sizeof r, &r, sizeof r);
    h->layers[0].parent = AX_IR_NO_LAYER;
    h->layers[0].subtree_end = 2;
    h->layers[0].items_size = sizeof r;
    h->layers[0].item_count = 1;
    h->layers[1].parent = 0;
    h->layers[1].subtree_end = 2;
    h->layers[1].items_offset = sizeof r;
    h->layers[1].items_size = sizeof r;
    h->layers[1].item_count = 1;
    h->ir.struct_size = sizeof h->ir;
    h->ir.version_major = AX_IR_VERSION_MAJOR;
    h->ir.layer_count = 2;
    h->ir.layer_stride = sizeof(ax_ir_layer);
    h->ir.layers = h->layers;
    h->ir.items_size = sizeof h->items;
    h->ir.items = (const uint8_t *)h->items;
}

AX_TEST(validate_rejects_malformed_snapshots)
{
    handmade h;
    ax_ir_item_header ih;

    handmade_init(&h);
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_OK);

    h.ir.version_major = AX_IR_VERSION_MAJOR + 1;
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_ABI_MISMATCH);

    handmade_init(&h);
    h.ir.version_minor = AX_IR_VERSION_MINOR + 7; /* newer minor: still readable */
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_OK);

    handmade_init(&h);
    h.layers[1].parent = 1; /* its own parent */
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.layers[0].subtree_end = 1; /* the child is outside the root */
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.layers[1].subtree_end = 3; /* past the end */
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.layers[1].items_offset = 8; /* inside another record */
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.layers[1].item_count = 2;
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.layers[1].items_size = sizeof(ax_ir_rect_item) + 8; /* past the stream */
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    ih.type = AX_IR_ITEM_RECT;
    ih.flags = 0;
    ih.size = 16; /* a known rect smaller than its record */
    memcpy((uint8_t *)h.items + sizeof(ax_ir_rect_item), &ih, sizeof ih);
    h.layers[1].items_size = 16;
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    ih.type = 999;
    ih.size = 12; /* not a multiple of 8 */
    memcpy((uint8_t *)h.items + sizeof(ax_ir_rect_item), &ih, sizeof ih);
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    ih.size = 0; /* would never advance */
    memcpy((uint8_t *)h.items, &ih, sizeof ih);
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.ir.layer_stride = sizeof(ax_ir_layer) - 4;
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);

    handmade_init(&h);
    h.ir.layer_count = 0;
    AX_CHECK_RESULT(ax_ir_validate(&h.ir), AX_ERR_INVALID_ARGUMENT);
}
