/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: where fonts come from (decision 98).
 *
 * A font source answers "which font file best matches this family, weight, style and width?".
 * The text system asks its sources in order and loads what they name. Two exist:
 *   - the bundled set: Noto Sans (variable, width and weight axes, roman and italic) cut down to
 *     Latin, compiled into the library, so headless runs, tests and the web need no file system.
 *     It is always available and answers every query: the last resort and the headless default;
 *   - fontconfig, Linux only and optional (AX_TEXT_FONTCONFIG): the system's fonts, as the
 *     desktop's own apps see them.
 * DirectWrite (Windows) and Core Text (Apple) implement the same table later.
 *
 * thread: a source's functions may be called from any thread, concurrently.
 */
#ifndef AX_TEXT_FONT_SOURCE_H
#define AX_TEXT_FONT_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>

AX_EXTERN_C_BEGIN

/* A font's style. Values are fixed. */
typedef enum ax_font_style {
    AX_FONT_STYLE_NORMAL = 0,
    AX_FONT_STYLE_ITALIC = 1
} ax_font_style;

/* What is asked for. An empty family means the system's interface font ("sans-serif"). */
typedef struct ax_font_query {
    ax_str family;  /* UTF-8, need not be NUL-terminated */
    float weight;   /* 1..1000; 400 regular, 700 bold */
    uint32_t style; /* ax_font_style */
    float width;    /* percent of normal: 50..200; 100 normal */
} ax_font_query;

/* What a source answers: a font file, in memory or on disk, and the face in it. */
typedef struct ax_font_file {
    const uint8_t *data; /* in memory (static, the source's), or NULL */
    size_t size;
    char path[512];      /* on disk when data is NULL: NUL-terminated UTF-8 */
    uint32_t face_index; /* the face in a collection; named instances are not used */
    bool exact_family;   /* the family is the one asked for (not a fallback) */
} ax_font_file;

typedef struct ax_font_source ax_font_source;

typedef struct ax_font_source_vtbl {
    const char *name; /* "bundled", "fontconfig" */
    /* The best match for the query; false when the source has nothing at all. */
    bool (*match)(ax_font_source *self, const ax_font_query *query, ax_font_file *out);
    void (*destroy)(ax_font_source *self); /* NULL for static sources */
} ax_font_source_vtbl;

/* Implementations start with this. */
struct ax_font_source {
    const ax_font_source_vtbl *vtbl;
};

/* The bundled set. Static: never destroyed. thread: any */
AX_API ax_font_source *ax_font_source_bundled(void);

/* The bundled files themselves (tests check their OpenType features). index 0: roman, 1: italic.
 * False past the last. */
AX_API bool ax_font_source_bundled_file(uint32_t index, const uint8_t **data, size_t *size,
                                        const char **name);

/* fontconfig's view of the system's fonts. AX_ERR_UNSUPPORTED when the build has no fontconfig
 * (not Linux, or AX_TEXT_FONTCONFIG off) or it cannot load its configuration;
 * AX_ERR_OUT_OF_MEMORY. thread: any */
AX_API ax_result ax_font_source_fontconfig_create(const ax_allocator *allocator,
                                                  ax_font_source **out);

/* Destroys a source made by a _create function (no-op for the bundled one). */
AX_API void ax_font_source_destroy(ax_font_source *source);

AX_EXTERN_C_END

#endif /* AX_TEXT_FONT_SOURCE_H */
