/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fonts (decision 98): the bundled set as versioned in text/fonts/, its features and axes, the
 * sources, matching, metrics and raster instances.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hb-ot.h>
#include <hb.h>

#include <axion/base/ax_memory.h>
#include <axion/text/ax_text.h>

#include "ax_test.h"

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long len;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)len);
    if (data != NULL && fread(data, 1, (size_t)len, f) != (size_t)len) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)len;
    return data;
}

static bool has_feature(hb_face_t *face, hb_tag_t table, hb_tag_t feature)
{
    hb_tag_t tags[128];
    unsigned int count = 128, i;
    (void)hb_ot_layout_table_get_feature_tags(face, table, 0, &count, tags);
    for (i = 0; i < count; ++i) {
        if (tags[i] == feature) {
            return true;
        }
    }
    return false;
}

/* The owner's adjustment: the cut keeps GSUB and GPOS with the features the shaping tests use,
 * checked on the files as versioned, and the embedded copies are those same bytes. */
AX_TEST(the_versioned_fonts_keep_the_features_the_shaping_tests_use)
{
    uint32_t i;
    for (i = 0;; ++i) {
        const uint8_t *data;
        size_t size, disk_size = 0;
        const char *name;
        char path[1024];
        unsigned char *disk;
        hb_blob_t *blob;
        hb_face_t *face;
        hb_font_t *font;
        hb_buffer_t *buf;
        hb_ot_var_axis_info_t axis;
        unsigned int n;
        if (!ax_font_source_bundled_file(i, &data, &size, &name)) {
            break;
        }
        (void)snprintf(path, sizeof path, "%s/%s", AX_TEXT_FONTS_DIR, name);
        disk = read_file(path, &disk_size);
        AX_REQUIRE(disk != NULL);
        AX_CHECK_EQ_U(disk_size, size);
        AX_CHECK(memcmp(disk, data, size) == 0);

        blob = hb_blob_create((const char *)disk, (unsigned int)disk_size, HB_MEMORY_MODE_READONLY,
                              NULL, NULL);
        face = hb_face_create(blob, 0);
        AX_CHECK(has_feature(face, HB_OT_TAG_GSUB, HB_TAG('l', 'i', 'g', 'a')));
        AX_CHECK(has_feature(face, HB_OT_TAG_GPOS, HB_TAG('k', 'e', 'r', 'n')));
        AX_CHECK(has_feature(face, HB_OT_TAG_GSUB, HB_TAG('t', 'n', 'u', 'm')));
        AX_CHECK(has_feature(face, HB_OT_TAG_GSUB, HB_TAG('s', 'm', 'c', 'p')));
        AX_CHECK(has_feature(face, HB_OT_TAG_GSUB, HB_TAG('z', 'e', 'r', 'o')));
        AX_CHECK(has_feature(face, HB_OT_TAG_GSUB, HB_TAG('f', 'r', 'a', 'c')));
        AX_CHECK(hb_ot_var_find_axis_info(face, HB_TAG('w', 'g', 'h', 't'), &axis));
        AX_CHECK(axis.min_value <= 100.0f && axis.max_value >= 900.0f);
        AX_CHECK(hb_ot_var_find_axis_info(face, HB_TAG('w', 'd', 't', 'h'), &axis));

        /* Latin coverage, combining marks included. */
        font = hb_font_create(face);
        {
            static const hb_codepoint_t cps[] = {'A', 'z', 0xE9, 0x141, 0x301, 0x2014, 0x20AC};
            size_t k;
            for (k = 0; k < sizeof cps / sizeof cps[0]; ++k) {
                hb_codepoint_t g = 0;
                AX_CHECK(hb_font_get_nominal_glyph(font, cps[k], &g));
            }
        }
        /* "fi" becomes one glyph with liga (on by default), two without. */
        buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, "fi", -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(font, buf, NULL, 0);
        n = hb_buffer_get_length(buf);
        AX_CHECK_EQ_U(n, 1u);
        hb_buffer_clear_contents(buf);
        hb_buffer_add_utf8(buf, "fi", -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        {
            hb_feature_t off;
            AX_CHECK(hb_feature_from_string("liga=0", -1, &off));
            hb_shape(font, buf, &off, 1);
        }
        AX_CHECK_EQ_U(hb_buffer_get_length(buf), 2u);
        hb_buffer_destroy(buf);
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        free(disk);
    }
    AX_CHECK_EQ_U(i, 2u);
}

static ax_text_system *make_system(void)
{
    ax_text_system *ts = NULL;
    ax_text_system_desc d;
    memset(&d, 0, sizeof d);
    AX_CHECK_RESULT(ax_text_system_create(&d, &ts), AX_OK);
    return ts;
}

static ax_font_query query(const char *family, float weight, uint32_t style)
{
    ax_font_query q;
    memset(&q, 0, sizeof q);
    q.family.ptr = family;
    q.family.len = strlen(family);
    q.weight = weight;
    q.style = style;
    q.width = 100.0f;
    return q;
}

AX_TEST(the_bundled_set_answers_every_query)
{
    ax_text_system *ts = make_system();
    ax_font_query q;
    ax_font_id regular, bold, italic, unknown, again;
    ax_font_metrics m;
    AX_REQUIRE(ts != NULL);

    q = query("", 400.0f, AX_FONT_STYLE_NORMAL);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &regular), AX_OK);
    AX_CHECK(strcmp(ax_text_font_family(ts, regular), "Noto Sans") == 0);
    q = query("Noto Sans", 700.0f, AX_FONT_STYLE_NORMAL);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &bold), AX_OK);
    AX_CHECK(bold != regular); /* the same face at another weight */
    q = query("noto sans", 400.0f, AX_FONT_STYLE_ITALIC);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &italic), AX_OK);
    AX_CHECK(italic != regular);
    q = query("No Such Family", 400.0f, AX_FONT_STYLE_NORMAL);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &unknown), AX_OK);
    AX_CHECK_EQ_U(unknown, regular); /* the fallback, at the same weight: the same font */
    q = query("", 400.0f, AX_FONT_STYLE_NORMAL);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &again), AX_OK);
    AX_CHECK_EQ_U(again, regular);

    /* Noto Sans: ascender 1069, descender -293, line gap 0, in 1000 units per em. */
    ax_text_font_metrics(ts, regular, &m);
    AX_CHECK_NEAR_F(m.units_per_em, 1000.0f, 0.0f);
    AX_CHECK_NEAR_F(m.ascent, 1.069f, 0.0005f);
    AX_CHECK_NEAR_F(m.descent, 0.293f, 0.0005f);
    AX_CHECK_NEAR_F(m.line_gap, 0.0f, 0.0f);
    ax_text_system_destroy(ts);
}

AX_TEST(raster_instances_are_shared_and_large_sizes_are_scaled)
{
    ax_text_system *ts = make_system();
    ax_font_query q = query("", 400.0f, AX_FONT_STYLE_NORMAL);
    ax_font_id font;
    ax_font_instance a, b, c, big;
    float scale = 0.0f;
    AX_REQUIRE(ts != NULL);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &font), AX_OK);
    a = ax_text_font_instance(ts, font, 14.0f, 0, NULL);
    b = ax_text_font_instance(ts, font, 14.0f, 0, &scale);
    c = ax_text_font_instance(ts, font, 14.0f * 1.325f, 0, NULL);
    AX_CHECK(a != 0);
    AX_CHECK_EQ_U(a, b);
    AX_CHECK_NEAR_F(scale, 1.0f, 0.0f);
    AX_CHECK(c != a);
    AX_CHECK_NEAR_F(ax_text_instance_size(ts, c), 18.546875f, 0.0f); /* 18.55 to 1/64 */
    big = ax_text_font_instance(ts, font, 512.0f, 0, &scale);
    AX_CHECK_NEAR_F(ax_text_instance_size(ts, big), 256.0f, 0.0f);
    AX_CHECK_NEAR_F(scale, 2.0f, 0.0f);
    AX_CHECK_EQ_U(ax_text_font_instance(ts, 999u, 12.0f, 0, NULL), 0u);
    ax_text_system_destroy(ts);
}

AX_TEST(fontconfig_matches_the_systems_interface_font)
{
    ax_font_source *fc = NULL;
    ax_text_system *ts = NULL;
    ax_text_system_desc d;
    ax_font_query q = query("", 400.0f, AX_FONT_STYLE_NORMAL);
    ax_font_id font;
    ax_result r = ax_font_source_fontconfig_create(NULL, &fc);
    if (r == AX_ERR_UNSUPPORTED) {
        AX_SKIP("no fontconfig in this build or system");
    }
    AX_REQUIRE(r == AX_OK);
    memset(&d, 0, sizeof d);
    d.sources = &fc;
    d.source_count = 1;
    AX_CHECK_RESULT(ax_text_system_create(&d, &ts), AX_OK);
    AX_CHECK_RESULT(ax_text_match_font(ts, &q, &font), AX_OK);
    AX_CHECK(font != 0);
    AX_CHECK(ax_text_font_family(ts, font)[0] != '\0');
    ax_text_system_destroy(ts);
    ax_font_source_destroy(fc);
}
