/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Line layout (decision 99): greedy wrapping at UAX #14 opportunities, hard breaks, hanging
 * spaces, alignment, line metrics, re-wrapping without re-shaping (counted), fragments at unsafe
 * breaks, and measuring from several threads at once.
 */
#include <stdio.h>

#include <axion/base/ax_thread.h>

#include "ax_test.h"
#include "test_text_helpers.h"

static const char PANGRAM[] = "The quick brown fox jumps over the lazy dog";

/* The text of line i. */
static void line_text(const ax_text_paragraph *p, const ax_text_layout *l, uint32_t i, char *out,
                      size_t cap)
{
    ax_str t = ax_text_paragraph_text(p);
    size_t n = l->lines[i].end - l->lines[i].start;
    n = n < cap - 1u ? n : cap - 1u;
    memcpy(out, t.ptr + l->lines[i].start, n);
    out[n] = '\0';
}

AX_TEST(lines_break_at_spaces_and_never_exceed_the_width)
{
    ax_text_system *ts = tt_system();
    ax_text_paragraph *p;
    const ax_text_layout *l;
    float full, h;
    uint32_t i;
    char buf[64];
    AX_REQUIRE(ts != NULL);
    p = tt_paragraph(ts, PANGRAM, 16.0f, NULL, AX_TEXT_ALIGN_START);
    AX_REQUIRE(p != NULL);
    ax_text_paragraph_measure(p, NAN, &full, &h);
    AX_CHECK(full > 200.0f && full < 400.0f);

    l = ax_text_paragraph_layout(p, 120.0f, NAN);
    AX_REQUIRE(l != NULL);
    AX_CHECK(l->line_count >= 3u);
    for (i = 0; i < l->line_count; ++i) {
        AX_CHECK(l->lines[i].width <= 120.0f);
        line_text(p, l, i, buf, sizeof buf);
        /* Lines start at word starts; trailing spaces hang on the line they end. */
        AX_CHECK(buf[0] != ' ');
        if (i + 1u < l->line_count) {
            AX_CHECK(buf[strlen(buf) - 1u] == ' ');
        }
    }
    line_text(p, l, 0, buf, sizeof buf);
    AX_CHECK(strcmp(buf, "The quick ") == 0); /* "The quick brown" is 124 units wide */
    /* Line boxes stack: Noto Sans at 16 is 16 * (1.069 + 0.293) tall. */
    AX_CHECK_NEAR_F(l->lines[0].height, 16.0f * 1.362f, 0.01f);
    AX_CHECK_NEAR_F(l->lines[1].top, l->lines[0].height, 0.0001f);
    AX_CHECK_NEAR_F(l->lines[0].baseline, 16.0f * 1.069f, 0.01f);
    AX_CHECK_NEAR_F(l->height, l->lines[0].height * (float)l->line_count, 0.001f);
    ax_text_layout_release(l);

    /* A width narrower than a word: the word overflows, alone on its line. */
    l = ax_text_paragraph_layout(p, 5.0f, NAN);
    AX_REQUIRE(l != NULL);
    AX_CHECK_EQ_U(l->line_count, 9u);
    AX_CHECK(l->width > 5.0f);
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}

AX_TEST(newlines_are_hard_breaks_and_alignment_places_lines)
{
    ax_text_system *ts = tt_system();
    ax_text_paragraph *p;
    const ax_text_layout *l;
    float w, h;
    AX_REQUIRE(ts != NULL);
    p = tt_paragraph(ts, "one\ntwo two\n", 20.0f, NULL, AX_TEXT_ALIGN_CENTER);
    AX_REQUIRE(p != NULL);
    l = ax_text_paragraph_layout(p, NAN, 200.0f);
    AX_REQUIRE(l != NULL);
    AX_CHECK_EQ_U(l->line_count, 3u); /* the final newline leaves an empty line */
    AX_CHECK_EQ_U(l->lines[2].glyph_count, 0u);
    AX_CHECK_NEAR_F(l->lines[0].x, (200.0f - l->lines[0].width) * 0.5f, 0.0001f);
    AX_CHECK_NEAR_F(l->lines[1].x, (200.0f - l->lines[1].width) * 0.5f, 0.0001f);
    AX_CHECK(l->lines[1].width > l->lines[0].width);
    /* A glyph's x is its pen position from the paragraph's left. */
    AX_CHECK_NEAR_F(l->glyphs[l->lines[1].first_glyph].x, l->lines[1].x, 0.0001f);
    ax_text_layout_release(l);
    /* Unwrapped, measured: as wide as the widest line, three lines tall. */
    ax_text_paragraph_measure(p, NAN, &w, &h);
    AX_CHECK(h > 3.0f * 20.0f);
    ax_text_paragraph_release(p);

    p = tt_paragraph(ts, "", 20.0f, NULL, 0);
    AX_REQUIRE(p != NULL);
    ax_text_paragraph_measure(p, 100.0f, &w, &h);
    AX_CHECK_NEAR_F(w, 0.0f, 0.0f);
    AX_CHECK_NEAR_F(h, 0.0f, 0.0f);
    ax_text_paragraph_release(p);

    /* END alignment against the widest line when no box is given. */
    p = tt_paragraph(ts, "a\nwide line", 20.0f, NULL, AX_TEXT_ALIGN_END);
    l = ax_text_paragraph_layout(p, NAN, NAN);
    AX_REQUIRE(l != NULL);
    AX_CHECK_NEAR_F(l->lines[0].x + l->lines[0].width, l->lines[1].width, 0.0001f);
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}

/* The owner's check: re-wrapping does not shape again, verified with counters. */
AX_TEST(rewrapping_does_not_shape_again)
{
    ax_text_system *ts = tt_system();
    ax_text_paragraph *p;
    ax_text_counters c;
    uint32_t i;
    AX_REQUIRE(ts != NULL);
    p = tt_paragraph(ts, PANGRAM, 16.0f, NULL, 0);
    AX_REQUIRE(p != NULL);
    ax_text_get_counters(ts, &c, true);
    AX_CHECK(ax_text_paragraph_shape(p));
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.runs_shaped, 1u);
    for (i = 0; i < 40; ++i) {
        const ax_text_layout *l = ax_text_paragraph_layout(p, 60.0f + 7.5f * (float)i, NAN);
        AX_REQUIRE(l != NULL);
        ax_text_layout_release(l);
    }
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.runs_shaped, 1u);      /* shaped once */
    AX_CHECK_EQ_U(c.fragments_shaped, 0u); /* every break was safe */
    AX_CHECK_EQ_U(c.layouts, 40u);
    /* The same widths again come from the layout cache (its last four). */
    for (i = 36; i < 40; ++i) {
        const ax_text_layout *l = ax_text_paragraph_layout(p, 60.0f + 7.5f * (float)i, NAN);
        ax_text_layout_release(l);
    }
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.layouts, 40u);
    AX_CHECK_EQ_U(c.layouts_cached, 4u);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}

/* "A-Tom": the hyphen kerns with the T, so a break after the hyphen is unsafe and each side is
 * shaped again on its own. The wrapped lines have the widths of the parts shaped alone. */
AX_TEST(an_unsafe_break_shapes_its_fragments_again)
{
    ax_text_system *ts = tt_system();
    ax_text_paragraph *p;
    const ax_text_layout *l;
    ax_text_counters c;
    float full, h, a, b, wrapped_a, wrapped_b;
    AX_REQUIRE(ts != NULL);
    a = tt_width(ts, "A-", 40.0f, NULL);
    b = tt_width(ts, "Tom", 40.0f, NULL);
    p = tt_paragraph(ts, "A-Tom", 40.0f, NULL, 0);
    ax_text_paragraph_measure(p, NAN, &full, &h);
    AX_CHECK(full < a + b - 0.5f); /* kerned across the hyphen */
    ax_text_get_counters(ts, &c, true);
    l = ax_text_paragraph_layout(p, full * 0.7f, NAN);
    AX_REQUIRE(l != NULL);
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(l->line_count, 2u);
    AX_CHECK(c.fragments_shaped >= 1u);
    wrapped_a = l->lines[0].width;
    wrapped_b = l->lines[1].width;
    AX_CHECK_NEAR_F(wrapped_a, a, 0.001f);
    AX_CHECK_NEAR_F(wrapped_b, b, 0.001f);
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}

/* ---- Concurrency: decision 90's contract for measure functions -------------------------------- */

typedef struct worker_arg {
    ax_text_system *ts;
    ax_text_paragraph *shared;
    uint32_t index;
    bool ok;
} worker_arg;

static int measure_worker(void *arg)
{
    worker_arg *w = (worker_arg *)arg;
    uint32_t i;
    char text[64];
    w->ok = true;
    for (i = 0; i < 200; ++i) {
        float width = 0.0f, height = 0.0f;
        ax_text_paragraph *own;
        ax_text_paragraph_measure(w->shared, 50.0f + (float)((i * 7u + w->index * 13u) % 300u),
                                  &width, &height);
        if (!(height > 0.0f)) {
            w->ok = false;
        }
        /* Other paragraphs through the shared caches at the same time. */
        (void)snprintf(text, sizeof text, "worker %u item %u", (unsigned)w->index, (unsigned)(i % 17u));
        own = tt_paragraph(w->ts, text, 12.0f + (float)(i % 5u), NULL, 0);
        if (own == NULL) {
            w->ok = false;
            continue;
        }
        ax_text_paragraph_measure(own, 80.0f, &width, &height);
        ax_text_paragraph_release(own);
    }
    return 0;
}

AX_TEST(paragraphs_measure_from_several_threads_at_once)
{
    ax_text_system *ts = tt_system();
    ax_text_paragraph *p;
    ax_thread threads[4];
    worker_arg args[4];
    uint32_t i;
    AX_REQUIRE(ts != NULL);
    if (!ax_threads_supported()) {
        ax_text_system_destroy(ts);
        AX_SKIP("no threads in this build");
    }
    p = tt_paragraph(ts, PANGRAM, 16.0f, NULL, 0); /* not shaped yet: the first caller shapes */
    AX_REQUIRE(p != NULL);
    for (i = 0; i < 4; ++i) {
        args[i].ts = ts;
        args[i].shared = p;
        args[i].index = i;
        args[i].ok = false;
        AX_REQUIRE(ax_thread_create(&threads[i], measure_worker, &args[i]) == AX_OK);
    }
    for (i = 0; i < 4; ++i) {
        (void)ax_thread_join(&threads[i]);
        AX_CHECK(args[i].ok);
    }
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
}
