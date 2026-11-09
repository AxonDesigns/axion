/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Glyph runs in the Render IR (minor 1, decision 101): the record and its validation, the dump,
 * serialization with the glyph table (a serialized snapshot draws on its own), the mailbox's
 * oldest epoch, the coverage correction table, and the software renderer drawing a deserialized
 * snapshot exactly as the original.
 */
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_arena.h"
#include "axion/render/ax_ir_mailbox.h"
#include "axion/render/ax_render_ir.h"
#include "axion/render/ax_render_soft.h"

/* A glyph source with two glyphs: a 3x2 block (glyph 7) and a 2x2 one (glyph 9, bin 1). */
static const uint8_t k_block[6] = {255, 128, 0, 64, 255, 32};
static const uint8_t k_small[4] = {10, 20, 30, 40};

static bool fake_lookup(const ax_ir_glyph_source *self, uint32_t instance, uint32_t glyph,
                        uint32_t bin, ax_ir_glyph_bitmap *out)
{
    (void)self;
    if (instance != 3) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (glyph == 7 && bin == 0) {
        out->width = 3;
        out->height = 2;
        out->left = 1;
        out->top = 2;
        out->stride = 3;
        out->pixels = k_block;
        return true;
    }
    if (glyph == 9 && bin == 1) {
        out->width = 2;
        out->height = 2;
        out->left = 0;
        out->top = 2;
        out->stride = 2;
        out->pixels = k_small;
        return true;
    }
    return false;
}

static const ax_ir_glyph_source k_source = {fake_lookup, NULL};

static const ax_render_ir *record(ax_ir_builder *b)
{
    ax_ir_glyph_run_item run;
    ax_ir_glyph g[3];
    const ax_render_ir *ir = NULL;
    ax_ir_builder_begin(b, 40.0f, 20.0f, 0, ax_color_make(1.0f, 1.0f, 1.0f, 1.0f));
    ax_ir_builder_set_resources(b, &k_source, 42);
    memset(&run, 0, sizeof run);
    run.color = ax_color_make(0.0f, 0.0f, 0.0f, 1.0f);
    run.font_instance = 3;
    run.raster_scale = 1.0f;
    g[0].id = 7;
    g[0].bin = 0;
    g[0].x = 4;
    g[0].y = 10;
    g[1].id = 9;
    g[1].bin = 1;
    g[1].x = 12;
    g[1].y = 10;
    g[2].id = 7; /* again: the table holds it once */
    g[2].bin = 0;
    g[2].x = 20;
    g[2].y = 14;
    ax_ir_glyph_run(b, &run, g, 3);
    if (ax_ir_builder_end(b, &ir) != AX_OK) {
        return NULL;
    }
    return ir;
}

typedef struct text_buf {
    char buf[2048];
    size_t len;
} text_buf;

static void write_text(void *ctx, const char *s, size_t n)
{
    text_buf *t = (text_buf *)ctx;
    if (t->len + n < sizeof t->buf) {
        memcpy(t->buf + t->len, s, n);
        t->len += n;
        t->buf[t->len] = '\0';
    }
}

AX_TEST(a_glyph_run_records_validates_and_dumps)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    static text_buf out;
    ax_ir_builder_init(&b, ax_default_allocator());
    ir = record(&b);
    AX_REQUIRE(ir != NULL);
    AX_CHECK_EQ_U(ir->version_minor, 1);
    AX_CHECK(ir->glyphs == &k_source);
    AX_CHECK_EQ_U(ir->resource_epoch, 42);
    AX_CHECK_RESULT(ax_ir_validate(ir), AX_OK);
    out.len = 0;
    ax_ir_dump(ir, write_text, &out);
    AX_CHECK(strstr(out.buf, "glyph_run instance 3 scale 1 glyphs 3 color (0 0 0 1)") != NULL);
    AX_CHECK(strstr(out.buf, "7@4,10 9@12,10/1 7@20,14") != NULL);

    /* A run claiming more glyphs than its record holds is malformed. */
    {
        uint8_t *items = (uint8_t *)malloc(ir->items_size);
        ax_render_ir bad = *ir;
        ax_ir_glyph_run_item run;
        AX_REQUIRE(items != NULL);
        memcpy(items, ir->items, ir->items_size);
        memcpy(&run, items, sizeof run);
        run.glyph_count = 4;
        memcpy(items, &run, sizeof run);
        bad.items = items;
        AX_CHECK_RESULT(ax_ir_validate(&bad), AX_ERR_INVALID_ARGUMENT);
        free(items);
    }
    ax_ir_builder_deinit(&b);
}

static uint8_t *draw(const ax_render_ir *ir)
{
    ax_render_soft *r = NULL;
    ax_soft_target t;
    uint8_t *px = (uint8_t *)calloc(40u * 20u * 4u, 1);
    if (px == NULL || ax_render_soft_create(NULL, NULL, &r) != AX_OK) {
        free(px);
        return NULL;
    }
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = px;
    t.width = 40;
    t.height = 20;
    t.stride = 40u * 4u;
    t.format = AX_SOFT_RGBA8;
    t.scale = 1.0f;
    if (ax_render_soft_draw_ir(r, &t, ir, NULL) != AX_OK) {
        free(px);
        px = NULL;
    }
    ax_render_soft_destroy(r);
    return px;
}

AX_TEST(a_serialized_snapshot_carries_its_glyphs_and_draws_on_its_own)
{
    ax_ir_builder b;
    ax_arena arena;
    const ax_render_ir *ir, *back;
    uint8_t *bytes, *again, *a, *c;
    size_t size, size2 = 0;
    ax_ir_glyph_bitmap bm;
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    ir = record(&b);
    AX_REQUIRE(ir != NULL);
    size = ax_ir_serialized_size(ir);
    /* The table: two distinct glyphs, each a 24-byte entry and its pixels padded to 4. */
    AX_CHECK_EQ_U(size, 96 + (24 + 8) + (24 + 4) + 100 + ir->items_size);
    bytes = (uint8_t *)malloc(size);
    AX_REQUIRE(bytes != NULL);
    AX_REQUIRE(ax_ir_serialize(ir, bytes, size, NULL) == AX_OK);
    AX_REQUIRE(ax_ir_deserialize(bytes, size, &arena, &back) == AX_OK);
    AX_REQUIRE(back->glyphs != NULL);
    AX_CHECK_EQ_U(back->resource_epoch, 42);
    AX_REQUIRE(back->glyphs->lookup(back->glyphs, 3, 7, 0, &bm));
    AX_CHECK(bm.width == 3 && bm.height == 2 && bm.left == 1 && bm.top == 2);
    AX_CHECK(memcmp(bm.pixels, k_block, 6) == 0);
    AX_CHECK(!back->glyphs->lookup(back->glyphs, 3, 8, 0, &bm));

    /* Round trip byte for byte, and the same pixels from the copy as from the original. */
    again = (uint8_t *)malloc(size);
    AX_REQUIRE(again != NULL);
    AX_REQUIRE(ax_ir_serialize(back, again, size, &size2) == AX_OK);
    AX_CHECK_EQ_U(size2, size);
    AX_CHECK(memcmp(again, bytes, size) == 0);
    a = draw(ir);
    c = draw(back);
    AX_REQUIRE(a != NULL && c != NULL);
    AX_CHECK(memcmp(a, c, 40u * 20u * 4u) == 0);
    /* The glyph's 255 pixel is black over white at (4 + 1, 10 - 2). */
    AX_CHECK_EQ_U(a[(8u * 40u + 5u) * 4u + 0u], 0u);
    free(a);
    free(c);
    free(again);
    free(bytes);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}

AX_TEST(without_a_glyph_source_a_run_is_skipped_and_counted)
{
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_render_ir bare;
    ax_render_soft *r = NULL;
    ax_soft_target t;
    ax_render_counts counts;
    uint8_t px[40u * 20u * 4u];
    ax_ir_builder_init(&b, ax_default_allocator());
    ir = record(&b);
    AX_REQUIRE(ir != NULL);
    bare = *ir;
    bare.glyphs = NULL;
    AX_REQUIRE(ax_render_soft_create(NULL, NULL, &r) == AX_OK);
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = px;
    t.width = 40;
    t.height = 20;
    t.stride = 40u * 4u;
    t.format = AX_SOFT_RGBA8;
    t.scale = 1.0f;
    memset(&counts, 0, sizeof counts);
    AX_CHECK_RESULT(ax_render_soft_draw_ir(r, &t, &bare, &counts), AX_OK);
    AX_CHECK_EQ_U(counts.skipped_items, 1u);
    ax_render_soft_destroy(r);
    ax_ir_builder_deinit(&b);
}

AX_TEST(the_mailbox_knows_the_oldest_epoch_a_reader_may_draw)
{
    ax_ir_mailbox mb;
    ax_ir_builder *b;
    uint32_t i;
    ax_ir_mailbox_init(&mb, ax_default_allocator());
    AX_CHECK_EQ_U(ax_ir_mailbox_oldest_epoch(&mb), UINT64_MAX);
    for (i = 1; i <= 2; ++i) {
        b = ax_ir_mailbox_begin(&mb, 10.0f, 10.0f, 0, ax_color_make(0, 0, 0, 1));
        ax_ir_builder_set_resources(b, NULL, 10u * i);
        AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    }
    AX_CHECK_EQ_U(ax_ir_mailbox_oldest_epoch(&mb), 10u);
    (void)ax_ir_mailbox_acquire(&mb); /* the reader holds epoch 20 now */
    for (i = 3; i <= 5; ++i) {
        b = ax_ir_mailbox_begin(&mb, 10.0f, 10.0f, 0, ax_color_make(0, 0, 0, 1));
        ax_ir_builder_set_resources(b, NULL, 10u * i);
        AX_REQUIRE(ax_ir_mailbox_publish(&mb) == AX_OK);
    }
    /* The reader's 20 is still drawable; 10, 30 and 40 were replaced. */
    AX_CHECK_EQ_U(ax_ir_mailbox_oldest_epoch(&mb), 20u);
    ax_ir_mailbox_deinit(&mb);
}

AX_TEST(the_coverage_correction_is_monotonic_and_pinned_at_the_ends)
{
    uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256];
    const ax_ir_text_lut_params p = ax_ir_text_lut_default();
    ax_ir_text_lut_params none;
    uint32_t bkt, i;
    ax_ir_text_lut_build(&p, lut);
    for (bkt = 0; bkt < AX_IR_TEXT_LUT_BUCKETS; ++bkt) {
        AX_CHECK_EQ_U(lut[bkt][0], 0u);
        AX_CHECK_EQ_U(lut[bkt][255], 255u);
        for (i = 1; i < 256u; ++i) {
            AX_CHECK(lut[bkt][i] >= lut[bkt][i - 1u]);
        }
    }
    AX_CHECK_EQ_U(ax_ir_text_lut_bucket(ax_color_make(0, 0, 0, 1)), 0u);
    AX_CHECK_EQ_U(ax_ir_text_lut_bucket(ax_color_make(1, 1, 1, 1)), AX_IR_TEXT_LUT_BUCKETS - 1u);
    /* Gamma 1 without contrast imitates linear light in the sRGB space: half coverage of white
     * on black gives 188 (the issue's 50 % white over black), of black on white 255 - 188. */
    none.contrast = 0.0f;
    none.gamma = 1.0f;
    ax_ir_text_lut_build(&none, lut);
    AX_CHECK(lut[AX_IR_TEXT_LUT_BUCKETS - 1u][128] >= 187u &&
             lut[AX_IR_TEXT_LUT_BUCKETS - 1u][128] <= 189u);
    AX_CHECK(lut[0][128] >= 66u && lut[0][128] <= 68u);
    /* Contrast thickens dark text only. */
    none.contrast = 0.5f;
    ax_ir_text_lut_build(&none, lut);
    AX_CHECK(lut[0][128] > 68u);
    AX_CHECK(lut[AX_IR_TEXT_LUT_BUCKETS - 1u][128] >= 187u &&
             lut[AX_IR_TEXT_LUT_BUCKETS - 1u][128] <= 189u);
}
