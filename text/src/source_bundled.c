/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The bundled font set (decision 98): Noto Sans, variable in width and weight, roman and italic,
 * cut down to Latin by tools/fonts/subset_noto.py and compiled into the library. It answers every
 * query: "Noto Sans" (and the generic and empty families) exactly, anything else as a fallback.
 */
#include <string.h>

#include <axion/text/ax_font_source.h>

extern const uint8_t ax__font_noto_sans[];
extern const size_t ax__font_noto_sans_size;
extern const uint8_t ax__font_noto_sans_italic[];
extern const size_t ax__font_noto_sans_italic_size;

static bool is_family(ax_str s, const char *name)
{
    size_t i, n = strlen(name);
    if (s.len != n) {
        return false;
    }
    for (i = 0; i < n; ++i) {
        char c = s.ptr[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

static bool bundled_match(ax_font_source *self, const ax_font_query *q, ax_font_file *out)
{
    (void)self;
    memset(out, 0, sizeof *out);
    if (q->style == AX_FONT_STYLE_ITALIC) {
        out->data = ax__font_noto_sans_italic;
        out->size = ax__font_noto_sans_italic_size;
    } else {
        out->data = ax__font_noto_sans;
        out->size = ax__font_noto_sans_size;
    }
    out->exact_family = q->family.len == 0 || is_family(q->family, "noto sans") ||
                        is_family(q->family, "sans-serif") || is_family(q->family, "system-ui");
    return true;
}

static const ax_font_source_vtbl BUNDLED_VTBL = {"bundled", bundled_match, NULL};
static ax_font_source BUNDLED = {&BUNDLED_VTBL};

ax_font_source *ax_font_source_bundled(void)
{
    return &BUNDLED;
}

bool ax_font_source_bundled_file(uint32_t index, const uint8_t **data, size_t *size,
                                 const char **name)
{
    switch (index) {
    case 0:
        *data = ax__font_noto_sans;
        *size = ax__font_noto_sans_size;
        *name = "NotoSans[wdth,wght].ttf";
        return true;
    case 1:
        *data = ax__font_noto_sans_italic;
        *size = ax__font_noto_sans_italic_size;
        *name = "NotoSans-Italic[wdth,wght].ttf";
        return true;
    default:
        return false;
    }
}

void ax_font_source_destroy(ax_font_source *source)
{
    if (source != NULL && source->vtbl->destroy != NULL) {
        source->vtbl->destroy(source);
    }
}
