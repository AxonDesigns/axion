/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shaping with HarfBuzz through the paragraph (decision 99): ligatures, kerning and OpenType
 * features of the bundled Noto Sans, and the shaping cache.
 */
#include "ax_test.h"
#include "test_text_helpers.h"

/* The glyph ids of an unwrapped one-line paragraph, and their count. */
static uint32_t glyphs_of(ax_text_system *ts, const char *text, const char *features,
                          uint32_t *ids, uint32_t max)
{
    ax_text_paragraph *p = tt_paragraph(ts, text, 16.0f, features, 0);
    const ax_text_layout *l;
    uint32_t i, n;
    if (p == NULL) {
        return 0;
    }
    l = ax_text_paragraph_layout(p, NAN, NAN);
    n = l != NULL ? l->glyph_count : 0;
    for (i = 0; i < n && i < max; ++i) {
        ids[i] = l->glyphs[i].id;
    }
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    return n;
}

AX_TEST(standard_ligatures_form_and_can_be_turned_off)
{
    ax_text_system *ts = tt_system();
    uint32_t a[8], b[8];
    AX_REQUIRE(ts != NULL);
    AX_CHECK_EQ_U(glyphs_of(ts, "fi", NULL, a, 8), 1u);
    AX_CHECK_EQ_U(glyphs_of(ts, "fl", NULL, a, 8), 1u);
    AX_CHECK_EQ_U(glyphs_of(ts, "fi", "\"liga\" 0", b, 8), 2u);
    AX_CHECK_EQ_U(glyphs_of(ts, "office", NULL, a, 8), 4u); /* o, ffi, c, e */
    ax_text_system_destroy(ts);
}

AX_TEST(kerning_tightens_pairs_and_can_be_turned_off)
{
    ax_text_system *ts = tt_system();
    float av, a, v, av_off, to, t, o;
    AX_REQUIRE(ts != NULL);
    av = tt_width(ts, "AV", 100.0f, NULL);
    a = tt_width(ts, "A", 100.0f, NULL);
    v = tt_width(ts, "V", 100.0f, NULL);
    av_off = tt_width(ts, "AV", 100.0f, "kern 0");
    to = tt_width(ts, "To", 100.0f, NULL);
    t = tt_width(ts, "T", 100.0f, NULL);
    o = tt_width(ts, "o", 100.0f, NULL);
    AX_CHECK(av < a + v - 1.0f); /* a negative kern, over a unit at 100 */
    AX_CHECK_NEAR_F(av_off, a + v, 0.001f);
    AX_CHECK(to < t + o - 1.0f);
    ax_text_system_destroy(ts);
}

AX_TEST(opentype_features_of_the_bundled_font_apply)
{
    ax_text_system *ts = tt_system();
    uint32_t plain[8], with[8];
    float one_p, zero_p, one_t, zero_t;
    AX_REQUIRE(ts != NULL);
    /* tnum: every digit as wide as the others; pnum: proportional. */
    one_t = tt_width(ts, "1", 100.0f, "tnum");
    zero_t = tt_width(ts, "0", 100.0f, "tnum");
    one_p = tt_width(ts, "1", 100.0f, "pnum");
    zero_p = tt_width(ts, "0", 100.0f, "pnum");
    AX_CHECK_NEAR_F(one_t, zero_t, 0.0001f);
    AX_CHECK(fabsf(one_p - zero_p) > 1.0f);
    /* smcp: small capitals replace lowercase. */
    AX_CHECK_EQ_U(glyphs_of(ts, "a", NULL, plain, 8), 1u);
    AX_CHECK_EQ_U(glyphs_of(ts, "a", "smcp", with, 8), 1u);
    AX_CHECK(plain[0] != with[0]);
    /* zero: a slashed zero. */
    AX_CHECK_EQ_U(glyphs_of(ts, "0", NULL, plain, 8), 1u);
    AX_CHECK_EQ_U(glyphs_of(ts, "0", "zero", with, 8), 1u);
    AX_CHECK(plain[0] != with[0]);
    /* frac: 1/2 becomes numerator, fraction slash, denominator. */
    AX_CHECK_EQ_U(glyphs_of(ts, "1/2", NULL, plain, 8), 3u);
    AX_CHECK_EQ_U(glyphs_of(ts, "1/2", "frac", with, 8), 3u);
    AX_CHECK(plain[0] != with[0] && plain[1] != with[1] && plain[2] != with[2]);
    ax_text_system_destroy(ts);
}

AX_TEST(the_shaping_cache_serves_every_size_and_paragraph)
{
    ax_text_system *ts = tt_system();
    ax_text_counters c;
    AX_REQUIRE(ts != NULL);
    ax_text_get_counters(ts, &c, true);
    (void)tt_width(ts, "Hello, world", 12.0f, NULL);
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.runs_shaped, 1u);
    AX_CHECK_EQ_U(c.runs_cached, 0u);
    /* Positions are in font units: another size and another paragraph reuse the run. */
    AX_CHECK_NEAR_F(tt_width(ts, "Hello, world", 24.0f, NULL),
                    2.0f * tt_width(ts, "Hello, world", 12.0f, NULL), 0.001f);
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.runs_shaped, 1u);
    AX_CHECK_EQ_U(c.runs_cached, 2u);
    /* Features are part of the key. */
    (void)tt_width(ts, "Hello, world", 12.0f, "kern 0");
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.runs_shaped, 2u);
    ax_text_system_destroy(ts);
}
