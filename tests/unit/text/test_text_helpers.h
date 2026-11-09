/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Helpers for text tests: a text system on the bundled fonts and one-style paragraphs.
 */
#ifndef AX_TEST_TEXT_HELPERS_H
#define AX_TEST_TEXT_HELPERS_H

#include <math.h>
#include <string.h>

#include <axion/text/ax_text.h>

static inline ax_text_system *tt_system(void)
{
    ax_text_system *ts = NULL;
    ax_text_system_desc d;
    memset(&d, 0, sizeof d);
    if (ax_text_system_create(&d, &ts) != AX_OK) {
        return NULL;
    }
    return ts;
}

static inline ax_font_id tt_font(ax_text_system *ts, float weight)
{
    ax_font_query q;
    ax_font_id f = 0;
    memset(&q, 0, sizeof q);
    q.weight = weight;
    q.width = 100.0f;
    (void)ax_text_match_font(ts, &q, &f);
    return f;
}

/* A paragraph of one style; `features` is CSS font-feature-settings (may be NULL). */
static inline ax_text_paragraph *tt_paragraph(ax_text_system *ts, const char *text, float size,
                                              const char *features, uint32_t align)
{
    ax_text_style st;
    ax_text_paragraph_desc d;
    ax_text_paragraph *p = NULL;
    memset(&st, 0, sizeof st);
    st.font = tt_font(ts, 400.0f);
    st.size = size;
    if (features != NULL) {
        ax_str f;
        f.ptr = features;
        f.len = strlen(features);
        st.feature_count = ax_text_parse_features(f, st.features, AX_TEXT_MAX_FEATURES, NULL);
    }
    memset(&d, 0, sizeof d);
    d.text = text;
    d.length = strlen(text);
    d.styles = &st;
    d.style_count = 1;
    d.align = align;
    if (ax_text_paragraph_create(ts, &d, &p) != AX_OK) {
        return NULL;
    }
    return p;
}

/* The unwrapped width of a text. */
static inline float tt_width(ax_text_system *ts, const char *text, float size, const char *features)
{
    ax_text_paragraph *p = tt_paragraph(ts, text, size, features, 0);
    float w = 0.0f, h = 0.0f;
    if (p == NULL) {
        return -1.0f;
    }
    ax_text_paragraph_measure(p, NAN, &w, &h);
    ax_text_paragraph_release(p);
    return w;
}

#endif
