/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the text system (the document's "Text rendering"; decisions 98 to 101).
 *
 *     ax_text_system *ts;                      // fonts, the shaping cache, the glyph cache
 *     ax_text_system_create(&desc, &ts);
 *     ax_font_id font;
 *     ax_text_match_font(ts, &query, &font);   // family, weight, style, width -> a font
 *     ax_text_paragraph *p;
 *     ax_text_paragraph_create(ts, &pdesc, &p); // itemized, breaks found; not shaped yet
 *     ax_text_paragraph_shape(p);              // HarfBuzz, on a worker (cached)
 *     ax_text_paragraph_measure(p, 240, &w, &h); // pure: greedy wrapping at 240 units
 *     ax_text_layout *l = ax_text_paragraph_layout(p, 240);  // lines and positioned glyphs
 *
 * Pipeline. A paragraph is UTF-8 text with style spans.
 *   1. Itemize: the text is split into runs by a chain of stages, each of which may only split
 *      runs further: style span, then font. Bidi levels (UAX #9), script and per-cluster font
 *      fallback are stages to add in M3; a run already carries its level, script and direction,
 *      so they slot in without changing what follows.
 *   2. Breaks: libunibreak's line-break opportunities (UAX #14) and grapheme boundaries (UAX #29),
 *      once per paragraph.
 *   3. Shape: each run through HarfBuzz, in font units, cached by (text, font, features, variation
 *      axes): positions are unhinted and in font units, so a run's shaping does not depend on the
 *      size, and the cache serves every size of a font (a font whose optical-size axis follows the
 *      size gets a font id per size, so the size is then part of the key).
 *   4. Lines: greedy wrapping at the break opportunities. A line ends inside a run without
 *      shaping it again when HarfBuzz marks the glyph at the break safe to break; where it does
 *      not (a ligature or kerning across the break), only the fragment from the nearest safe glyph
 *      to the break is shaped again (cached too). Resizing a paragraph is therefore cheap; the
 *      counters show it.
 *   5. Position: alignment, line metrics (the font's ascent, descent and line gap, or a line
 *      height), and the paragraph's size for layout.
 *
 * Threads. Every function here may be called from any thread, concurrently, unless it says
 * otherwise: fonts, the caches and the counters are shared under locks; a paragraph is immutable
 * once shaped, so measuring and laying it out are pure and can run on layout's workers
 * (decision 90's contract for measure functions).
 *
 * Glyphs (decision 100). Glyphs are rasterized as 8-bit coverage at the exact device size, by
 * FreeType with light hinting (vertical only, as KDE's "slight") up to 48 device pixels at whole
 * device scales and none above or at fractional scales (as Qt does), with 4 horizontal subpixel positions up to 48 pixels and whole pixels above. A font at a
 * raster size is a font instance. Rasterization runs on workers; the glyph cache keeps the
 * bitmaps, which renderers read (copying them into their atlases) through the Render IR's glyph
 * source. A glyph used by a snapshot that may still be drawn is never evicted: each use is
 * stamped with the snapshot's epoch, and only glyphs last used before the oldest live epoch
 * (ax_text_glyphs_retire) can go.
 */
#ifndef AX_TEXT_H
#define AX_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_jobs.h>

#include "ax_font_source.h"

AX_EXTERN_C_BEGIN

typedef struct ax_text_system ax_text_system;

typedef struct ax_text_system_desc {
    const ax_allocator *allocator; /* NULL: ax_default_allocator(); must be thread-safe */
    /* Asked in order; the bundled set is always asked last. Not owned; they must outlive the
     * system. */
    ax_font_source *const *sources;
    uint32_t source_count;
    bool own_sources;         /* destroy the sources with the system */
    size_t shape_cache_bytes; /* 0: 8 MiB */
    size_t glyph_cache_bytes; /* 0: 16 MiB (evicted only below the oldest live epoch) */
} ax_text_system_desc;

/* Errors: AX_ERR_OUT_OF_MEMORY, AX_ERR_UNSUPPORTED (FreeType could not start). */
AX_API ax_result ax_text_system_create(const ax_text_system_desc *desc, ax_text_system **out);

/* Waits for its background rasterization to finish (running jobs from the calling thread), then
 * frees everything. Paragraphs and layouts must have been released. */
AX_API void ax_text_system_destroy(ax_text_system *ts);

/* ---- Fonts ----------------------------------------------------------------------------------- */

/* A font: a face with its variation coordinates. 0 is none. Ids are never reused. */
typedef uint32_t ax_font_id;

/* Matches a query against the sources (the first with the exact family, else the first answer:
 * the system's or the bundled default) and returns the font, with the weight and width applied
 * to the variable axes the face has. Matches are cached. Errors: AX_ERR_OUT_OF_MEMORY,
 * AX_ERR_IO (a file a source named could not be read or parsed; the next source is tried
 * first). */
AX_API ax_result ax_text_match_font(ax_text_system *ts, const ax_font_query *query,
                                    ax_font_id *out);

/* Vertical metrics in ems (multiply by the size): ascent and descent are positive, as are the
 * line gap and the em's height in font units. */
typedef struct ax_font_metrics {
    float ascent, descent, line_gap;
    float units_per_em;
} ax_font_metrics;

AX_API void ax_text_font_metrics(ax_text_system *ts, ax_font_id font, ax_font_metrics *out);

/* The face's family name (for tests and devtools), "" when unknown. Valid while ts lives. */
AX_API const char *ax_text_font_family(ax_text_system *ts, ax_font_id font);

/* ---- Paragraphs ------------------------------------------------------------------------------ */

#define AX_TEXT_MAX_FEATURES 16u

/* An OpenType feature setting: the tag (HB_TAG order: 'l'<<24 | 'i'<<16 ...) and its value (0 off,
 * 1 on, n an alternate). */
typedef struct ax_text_feature {
    uint32_t tag;
    uint32_t value;
} ax_text_feature;

/*
 * Parses CSS font-feature-settings: comma-separated items, each a tag of four characters (quoted
 * or not) with an optional value: on, off or an integer ("liga" 0, "tnum", smcp 1). Returns the
 * number written; AX_ERR_INVALID_ARGUMENT through *out_result (may be NULL) on a malformed list
 * or more than `max` items. Pure.
 */
AX_API uint32_t ax_text_parse_features(ax_str css, ax_text_feature *out, uint32_t max,
                                       ax_result *out_result);

typedef enum ax_text_align {
    AX_TEXT_ALIGN_START = 0, /* left, until right-to-left text exists (M3) */
    AX_TEXT_ALIGN_CENTER = 1,
    AX_TEXT_ALIGN_END = 2
} ax_text_align;

typedef struct ax_text_style {
    ax_font_id font;
    float size;        /* the em, in logical units */
    float line_height; /* a multiple of size; 0: the font's ascent + descent + line gap */
    uint32_t feature_count;
    ax_text_feature features[AX_TEXT_MAX_FEATURES];
} ax_text_style;

/* A byte range of the text with one style. */
typedef struct ax_text_span {
    uint32_t start, end;
    uint32_t style; /* index in ax_text_paragraph_desc.styles */
} ax_text_span;

typedef struct ax_text_paragraph_desc {
    const char *text; /* UTF-8, copied; invalid sequences shape as U+FFFD */
    size_t length;
    const ax_text_style *styles; /* copied */
    uint32_t style_count;        /* at least 1 */
    /* In order, covering [0, length) without gaps; NULL: the whole text has styles[0]. */
    const ax_text_span *spans;
    uint32_t span_count;
    uint32_t align; /* ax_text_align */
} ax_text_paragraph_desc;

typedef struct ax_text_paragraph ax_text_paragraph;

/* Itemizes the text and finds its breaks. The paragraph starts with one reference.
 * Errors: AX_ERR_INVALID_ARGUMENT (no style, a style without a font or with a size not above 0,
 * spans out of order or out of range), AX_ERR_OUT_OF_MEMORY. */
AX_API ax_result ax_text_paragraph_create(ax_text_system *ts, const ax_text_paragraph_desc *desc,
                                          ax_text_paragraph **out);
AX_API void ax_text_paragraph_retain(ax_text_paragraph *p);
AX_API void ax_text_paragraph_release(ax_text_paragraph *p);

/* Shapes every run, through the cache. Idempotent and safe to call concurrently (one caller
 * shapes, the others wait). Returns false when it ran out of memory: the paragraph then lays out
 * as empty. */
AX_API bool ax_text_paragraph_shape(ax_text_paragraph *p);
AX_API bool ax_text_paragraph_is_shaped(const ax_text_paragraph *p);

/* The text's bytes (for tests and the mirror). */
AX_API ax_str ax_text_paragraph_text(const ax_text_paragraph *p);

/*
 * The size the paragraph takes when wrapped at max_width (NaN: never wrapped; only hard breaks
 * end lines): the widest line, and the lines' total height. Shapes first if needed. Pure, cached
 * by width.
 */
AX_API void ax_text_paragraph_measure(ax_text_paragraph *p, float max_width, float *out_width,
                                      float *out_height);

/* ---- Laid-out paragraphs --------------------------------------------------------------------- */

typedef struct ax_text_glyph {
    uint32_t id;     /* glyph id in its font */
    ax_font_id font; /* its run's font */
    float size;      /* its run's size */
    float x, y;      /* pen position: x from the paragraph's left, y its line's baseline */
    uint32_t cluster; /* byte offset of its cluster in the text */
    uint32_t style;   /* index of its style */
} ax_text_glyph;

typedef struct ax_text_line {
    float x;        /* left edge after alignment */
    float top;      /* top of the line box */
    float baseline; /* from the paragraph's top */
    float width;    /* without trailing spaces */
    float height;   /* the line box */
    uint32_t first_glyph, glyph_count;
    uint32_t start, end; /* bytes */
} ax_text_line;

/* A paragraph laid out at one width: immutable, reference-counted. */
typedef struct ax_text_layout {
    float max_width; /* what it was wrapped at (NaN: not wrapped) */
    float box_width; /* what it was aligned in (NaN: the widest line) */
    float width, height;
    uint32_t line_count, glyph_count;
    const ax_text_line *lines;
    const ax_text_glyph *glyphs;
} ax_text_layout;

/*
 * Lays the paragraph out wrapped at max_width (NaN: unwrapped) and aligned in a box of
 * box_width (NaN: the widest line). Pure; cached by both widths. NULL when out of memory.
 */
AX_API const ax_text_layout *ax_text_paragraph_layout(ax_text_paragraph *p, float max_width,
                                                      float box_width);
AX_API void ax_text_layout_retain(const ax_text_layout *l);
AX_API void ax_text_layout_release(const ax_text_layout *l);

/* ---- Characters and words (accessibility's text ranges, decision 108) ------------------------ */

/* A character of a laid-out line: a grapheme cluster (UAX #29). */
typedef struct ax_text_char {
    uint32_t start;  /* byte offset in the paragraph's text */
    uint32_t length; /* UTF-8 bytes */
    float x;         /* from the paragraph's left, as the line's glyphs */
    float width;
} ax_text_char;

/*
 * The characters of line `line` of a layout of paragraph p (shaped), in order, into chars (at
 * most capacity); returns how many the line has. A character inside a ligature gets an equal
 * share of its glyph's advance; the last one ends at the line's width (trailing spaces, which
 * hang, get none).
 */
AX_API uint32_t ax_text_line_chars(const ax_text_paragraph *p, const ax_text_layout *l,
                                   uint32_t line, ax_text_char *chars, uint32_t capacity);

/*
 * Splits a line's characters (as ax_text_line_chars gave them) into words (UAX #29 word
 * boundaries; spaces and punctuation after a word belong to it): word_lengths[i] is the i-th
 * word's length in characters, at most capacity of them; returns how many words there are.
 * Thread: any. Out of memory: 1 word with every character.
 */
AX_API uint32_t ax_text_line_words(const ax_text_paragraph *p, const ax_text_char *chars,
                                   uint32_t char_count, uint32_t *word_lengths, uint32_t capacity);

/* ---- Glyphs ---------------------------------------------------------------------------------- */

#define AX_TEXT_SUBPIXEL_MAX_SIZE 48.0f /* device pixels: subpixel positions and light hinting */
#define AX_TEXT_SUBPIXEL_BINS     4u
#define AX_TEXT_RASTER_MAX_SIZE   256.0f /* larger is rasterized at this size and scaled */

/* A font at a raster size (device pixels per em, to 1/64). 0 is none. */
typedef uint32_t ax_font_instance;

/* ax_text_font_instance flags. */
#define AX_TEXT_INSTANCE_NO_HINTING 0x1u /* never hint (a fractional device scale, as Qt does) */

/* The instance of `font` at `pixel_size` (clamped to AX_TEXT_RASTER_MAX_SIZE; *out_scale, may be
 * NULL, gets the factor the caller must scale it by: 1 unless clamped). Light hinting applies up to
 * AX_TEXT_SUBPIXEL_MAX_SIZE unless `flags` has AX_TEXT_INSTANCE_NO_HINTING. 0 when out of memory or
 * when the instance table is full (4096 instances). */
AX_API ax_font_instance ax_text_font_instance(ax_text_system *ts, ax_font_id font,
                                              float pixel_size, uint32_t flags, float *out_scale);

/* Pixel size of an instance (0 for none). */
AX_API float ax_text_instance_size(ax_text_system *ts, ax_font_instance instance);

typedef struct ax_glyph_key {
    ax_font_instance instance;
    uint32_t glyph;
    uint32_t bin; /* subpixel position: bin / AX_TEXT_SUBPIXEL_BINS of a pixel to the right */
} ax_glyph_key;

/* A rasterized glyph: 8-bit coverage, rows top-down. Its top-left pixel is at (pen x + left,
 * baseline - top). An empty glyph (a space) has width or height 0. */
typedef struct ax_glyph_bitmap {
    uint16_t width, height;
    int16_t left, top;
    uint32_t stride;
    const uint8_t *pixels; /* valid while a live snapshot uses the glyph (see above) */
} ax_glyph_bitmap;

/* The bitmap of a rasterized glyph; false when it is not in the cache or not ready. thread: any
 * (the render thread) */
AX_API bool ax_text_glyph_lookup(ax_text_system *ts, const ax_glyph_key *key, ax_glyph_bitmap *out);

/*
 * Makes the glyphs available for a snapshot of `epoch` (increasing), stamping each use. Missing
 * glyphs are rasterized at once while *sync_budget (may be NULL: no budget) lasts, in frame jobs
 * on `jobs` (NULL: on the calling thread), and the rest in background jobs, whose completion calls
 * the ready callback. Returns how many glyphs are not ready yet. A caller that gets a nonzero
 * answer draws its run later as a whole, never in part (decision 100).
 */
AX_API uint32_t ax_text_glyphs_ensure(ax_text_system *ts, const ax_glyph_key *keys, uint32_t count,
                                      uint64_t epoch, ax_job_system *jobs, uint32_t *sync_budget);

/* Called from a background job each time a batch of background glyphs is ready. */
AX_API void ax_text_set_glyphs_ready_callback(ax_text_system *ts, void (*fn)(void *user),
                                              void *user);

/* Evicts least recently used glyphs while the cache is above its budget, among those last used
 * before `oldest_live_epoch` (the oldest snapshot that may still be drawn). Returns how many were
 * evicted. */
AX_API uint32_t ax_text_glyphs_retire(ax_text_system *ts, uint64_t oldest_live_epoch);

/* ---- Counters -------------------------------------------------------------------------------- */

typedef struct ax_text_counters {
    uint64_t paragraphs;       /* created */
    uint64_t runs_shaped;      /* runs shaped by HarfBuzz (cache misses) */
    uint64_t runs_cached;      /* runs answered by the shaping cache */
    uint64_t fragments_shaped; /* line-end fragments shaped again at unsafe breaks */
    uint64_t layouts;          /* line layouts computed (cache misses) */
    uint64_t layouts_cached;
    uint64_t glyphs_rasterized;
    uint64_t glyphs_background;  /* of those, in background jobs */
    uint64_t glyphs_evicted;
    uint64_t glyph_bytes;        /* in the cache now */
    uint64_t glyph_pinned_skips; /* evictions refused: used by a live snapshot */
} ax_text_counters;

AX_API void ax_text_get_counters(ax_text_system *ts, ax_text_counters *out, bool reset);

AX_EXTERN_C_END

#endif /* AX_TEXT_H */
