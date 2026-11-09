/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Characters and words of a laid-out line, for accessibility's text ranges (decision 108):
 * grapheme clusters from the paragraph's UAX #29 breaks, placed by the line's glyphs, and words
 * from UAX #29 word boundaries.
 */
#include <string.h>

#include <graphemebreak.h>
#include <wordbreak.h>

#include "text_internal.h"

#include <axion/base/ax_memory.h>

/* The byte after which grapheme cluster i ends: the paragraph keeps a break status per byte,
 * GRAPHEMEBREAK_BREAK after the last byte of a cluster. */
static uint32_t cluster_end(const ax_text_paragraph *p, uint32_t start, uint32_t limit)
{
    uint32_t i = start;
    while (i < limit) {
        if (p->graphemes[i] == GRAPHEMEBREAK_BREAK) {
            return i + 1u;
        }
        i++;
    }
    return limit;
}

uint32_t ax_text_line_chars(const ax_text_paragraph *p, const ax_text_layout *l, uint32_t line,
                            ax_text_char *chars, uint32_t capacity)
{
    const ax_text_line *ln;
    uint32_t n = 0, at, g, gend, i, last_known;
    float line_end;
    if (p == NULL || l == NULL || line >= l->line_count) {
        return 0;
    }
    ln = &l->lines[line];
    g = ln->first_glyph;
    gend = ln->first_glyph + ln->glyph_count;
    line_end = ln->x + ln->width;
    at = ln->start;
    /* Pass 1: the clusters, and the x of each one a glyph starts. NaN marks "no glyph of its
     * own" (inside a ligature): filled in pass 2. */
    while (at < ln->end) {
        uint32_t end = cluster_end(p, at, ln->end);
        float x = -1.0f;
        bool found = false;
        while (g < gend && l->glyphs[g].cluster < at) {
            g++;
        }
        if (g < gend && l->glyphs[g].cluster < end) {
            x = l->glyphs[g].x;
            found = true;
        }
        if (n < capacity) {
            chars[n].start = at;
            chars[n].length = end - at;
            chars[n].x = found ? x : -1.0f;
            chars[n].width = found ? 0.0f : -1.0f; /* -1: no glyph of its own */
        }
        n++;
        at = end;
    }
    if (capacity == 0) {
        return n;
    }
    /* Pass 2: positions of characters without a glyph (an equal share of the ligature before
     * them), then each width up to the next character, the last one to the line's end. */
    last_known = 0;
    for (i = 0; i < n && i < capacity; ++i) {
        if (chars[i].width >= 0.0f) {
            last_known = i;
            continue;
        }
        if (i == 0) {
            chars[0].x = ln->x;
            chars[0].width = 0.0f;
            continue;
        }
        {
            uint32_t k = i;
            float from = chars[last_known].x, to;
            uint32_t span, j;
            while (k < n && k < capacity && chars[k].width < 0.0f) {
                k++;
            }
            to = (k < n && k < capacity) ? chars[k].x : line_end;
            if (to < from) {
                to = from;
            }
            span = k - last_known;
            for (j = last_known + 1u; j < k; ++j) {
                chars[j].x = from + (to - from) * (float)(j - last_known) / (float)span;
                chars[j].width = 0.0f;
            }
            i = k - 1u;
        }
    }
    for (i = 0; i < n && i < capacity; ++i) {
        float next = (i + 1u < n && i + 1u < capacity) ? chars[i + 1u].x : line_end;
        chars[i].width = next > chars[i].x ? next - chars[i].x : 0.0f;
    }
    return n;
}

/* Whether a character starts a word: letters, digits and any non-ASCII character; spaces and
 * ASCII punctuation stay with the word before them. */
static bool starts_word(unsigned char c)
{
    return c >= 0x80u || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

uint32_t ax_text_line_words(const ax_text_paragraph *p, const ax_text_char *chars,
                            uint32_t char_count, uint32_t *word_lengths, uint32_t capacity)
{
    const ax_allocator *a = ax_default_allocator();
    const char *text;
    uint32_t first, last, len, i, words = 0, current = 0;
    char stack[512];
    char *brks;
    if (p == NULL || chars == NULL || char_count == 0) {
        return 0;
    }
    text = p->text;
    first = chars[0].start;
    last = chars[char_count - 1u].start + chars[char_count - 1u].length;
    len = last - first;
    brks = len <= sizeof stack ? stack : (char *)ax_mem_alloc(a, len, 1);
    if (brks == NULL) {
        if (capacity > 0) {
            word_lengths[0] = char_count;
        }
        return 1;
    }
    set_wordbreaks_utf8((const utf8_t *)(text + first), len, "en", brks);
    for (i = 0; i < char_count; ++i) {
        const uint32_t s = chars[i].start - first;
        const bool boundary = i > 0 && brks[s - 1u] == WORDBREAK_BREAK;
        if (i > 0 && boundary && starts_word((unsigned char)text[chars[i].start]) && current > 0) {
            if (words < capacity) {
                word_lengths[words] = current;
            }
            words++;
            current = 0;
        }
        current++;
    }
    if (current > 0) {
        if (words < capacity) {
            word_lengths[words] = current;
        }
        words++;
    }
    if (brks != stack) {
        ax_mem_free(a, brks, len);
    }
    return words;
}
