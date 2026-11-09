/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * A line's characters and words for accessibility's text ranges (decision 108).
 */
#include <string.h>

#include "ax_test.h"
#include "test_text_helpers.h"

static uint32_t chars_of(ax_text_paragraph *p, const ax_text_layout *l, uint32_t line,
                         ax_text_char *out, uint32_t cap)
{
    const uint32_t n = ax_text_line_chars(p, l, line, NULL, 0);
    AX_CHECK(n <= cap);
    return ax_text_line_chars(p, l, line, out, cap);
}

AX_TEST(characters_are_grapheme_clusters_placed_by_the_glyphs)
{
    ax_text_system *ts = tt_system();
    /* "e" + COMBINING ACUTE is one character; "ffi" is a ligature: three characters. */
    static const char TEXT[] = "office cafe\xCC\x81 x";
    ax_text_paragraph *p;
    const ax_text_layout *l;
    ax_text_char c[32];
    uint32_t n, i, bytes = 0, words[32], nw, sum = 0;
    AX_REQUIRE(ts != NULL);
    p = tt_paragraph(ts, TEXT, 20.0f, NULL, 0);
    AX_REQUIRE(p != NULL && ax_text_paragraph_shape(p));
    l = ax_text_paragraph_layout(p, NAN, NAN);
    AX_REQUIRE(l != NULL && l->line_count == 1);
    n = chars_of(p, l, 0, c, 32);
    AX_CHECK_EQ_U(n, 13); /* o f f i c e _ c a f é _ x */
    for (i = 0; i < n; ++i) {
        bytes += c[i].length;
        AX_CHECK(c[i].width >= 0.0f);
        if (i > 0) {
            AX_CHECK(c[i].x >= c[i - 1].x);
            AX_CHECK_NEAR_F(c[i - 1].x + c[i - 1].width, c[i].x, 1e-3f);
        }
    }
    AX_CHECK_EQ_U(bytes, (uint32_t)strlen(TEXT));
    AX_CHECK_EQ_U(c[10].length, 3); /* é: e and the combining mark */
    /* Inside the ligature each character gets a share of it. */
    AX_CHECK(c[2].width > 0.0f && c[3].width > 0.0f);
    nw = ax_text_line_words(p, c, n, words, 32);
    AX_CHECK_EQ_U(nw, 3); /* "office ", "café ", "x" */
    for (i = 0; i < nw; ++i) {
        sum += words[i];
    }
    AX_CHECK_EQ_U(sum, n);
    AX_CHECK_EQ_U(words[0], 7);
    AX_CHECK_EQ_U(words[1], 5);
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}

AX_TEST(each_wrapped_line_has_its_own_characters)
{
    ax_text_system *ts = tt_system();
    static const char TEXT[] = "Lines break at word boundaries and wrap again.";
    ax_text_paragraph *p;
    const ax_text_layout *l;
    ax_text_char c[64];
    uint32_t i, total = 0, at = 0;
    AX_REQUIRE(ts != NULL);
    p = tt_paragraph(ts, TEXT, 16.0f, NULL, 0);
    AX_REQUIRE(p != NULL && ax_text_paragraph_shape(p));
    l = ax_text_paragraph_layout(p, 90.0f, 90.0f);
    AX_REQUIRE(l != NULL && l->line_count > 1);
    for (i = 0; i < l->line_count; ++i) {
        const uint32_t n = chars_of(p, l, i, c, 64);
        AX_REQUIRE(n > 0);
        AX_CHECK_EQ_U(c[0].start, at); /* lines cover the text in order */
        at = c[n - 1].start + c[n - 1].length;
        total += n;
        AX_CHECK(c[n - 1].x + c[n - 1].width <= l->lines[i].x + l->lines[i].width + 1e-3f);
    }
    AX_CHECK_EQ_U(at, (uint32_t)strlen(TEXT));
    AX_CHECK_EQ_U(total, (uint32_t)strlen(TEXT)); /* ASCII: a byte per character */
    AX_CHECK_EQ_U(ax_text_line_chars(p, l, l->line_count, c, 64), 0);
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}
