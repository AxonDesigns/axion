/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The system's fonts through fontconfig (Linux; decision 98). A query becomes a fontconfig
 * pattern (family, weight, slant, width) that goes through the system's configuration
 * (substitutions, aliases such as sans-serif), exactly as the desktop's toolkits match fonts, so
 * an empty family gives the desktop's interface font. Only the file and the face are used: the
 * rendering settings fontconfig also carries (hinting, antialiasing) are AXION's own.
 *
 * fontconfig is thread-safe for matching, but the configuration is shared: calls are serialized
 * by the source's mutex anyway, since matching happens once per distinct query (the text system
 * caches matches).
 */
#include <stdio.h>
#include <string.h>

#include <fontconfig/fontconfig.h>

#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/base/ax_thread.h>
#include <axion/text/ax_font_source.h>

typedef struct fc_source {
    ax_font_source base;
    ax_allocator allocator;
    ax_mutex mutex;
    FcConfig *config;
} fc_source;

static bool same_family(const char *a, ax_str b)
{
    size_t i;
    if (strlen(a) != b.len) {
        return false;
    }
    for (i = 0; i < b.len; ++i) {
        char x = a[i], y = b.ptr[i];
        if (x >= 'A' && x <= 'Z') {
            x = (char)(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (char)(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return true;
}

static bool fc_match(ax_font_source *self, const ax_font_query *q, ax_font_file *out)
{
    fc_source *s = (fc_source *)self;
    FcPattern *pat, *match;
    FcResult result;
    FcChar8 *file = NULL;
    FcChar8 *family = NULL;
    int index = 0;
    char name[256];
    bool generic;
    bool ok = false;

    memset(out, 0, sizeof *out);
    if (q->family.len >= sizeof name) {
        return false;
    }
    if (q->family.len == 0) {
        (void)snprintf(name, sizeof name, "sans-serif");
    } else {
        memcpy(name, q->family.ptr, q->family.len);
        name[q->family.len] = '\0';
    }
    generic = q->family.len == 0 || strcmp(name, "sans-serif") == 0 ||
              strcmp(name, "serif") == 0 || strcmp(name, "monospace") == 0 ||
              strcmp(name, "system-ui") == 0;

    ax_mutex_lock(&s->mutex);
    pat = FcPatternCreate();
    if (pat == NULL) {
        ax_mutex_unlock(&s->mutex);
        return false;
    }
    (void)FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)name);
    (void)FcPatternAddInteger(pat, FC_WEIGHT, FcWeightFromOpenTypeDouble((double)q->weight) > 0
                                                  ? (int)FcWeightFromOpenTypeDouble((double)q->weight)
                                                  : FC_WEIGHT_REGULAR);
    (void)FcPatternAddInteger(pat, FC_SLANT,
                              q->style == AX_FONT_STYLE_ITALIC ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    (void)FcPatternAddInteger(pat, FC_WIDTH, (int)(q->width + 0.5f));
    (void)FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    (void)FcConfigSubstitute(s->config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    match = FcFontMatch(s->config, pat, &result);
    if (match != NULL) {
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file != NULL &&
            strlen((const char *)file) < sizeof out->path) {
            (void)snprintf(out->path, sizeof out->path, "%s", (const char *)file);
            if (FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch) {
                index = 0;
            }
            /* The high 16 bits name a variable font's named instance: AXION applies the axes. */
            out->face_index = (uint32_t)index & 0xFFFFu;
            out->exact_family =
                generic || (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch &&
                            family != NULL && same_family((const char *)family, q->family));
            ok = true;
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    ax_mutex_unlock(&s->mutex);
    return ok;
}

static void fc_destroy(ax_font_source *self)
{
    fc_source *s = (fc_source *)self;
    ax_allocator a = s->allocator;
    FcConfigDestroy(s->config);
    ax_mutex_deinit(&s->mutex);
    ax_mem_free(&a, s, sizeof *s);
}

static const ax_font_source_vtbl FC_VTBL = {"fontconfig", fc_match, fc_destroy};

ax_result ax_font_source_fontconfig_create(const ax_allocator *allocator, ax_font_source **out)
{
    const ax_allocator *a = allocator != NULL ? allocator : ax_default_allocator();
    fc_source *s;
    FcConfig *config;
    *out = NULL;
    config = FcInitLoadConfigAndFonts();
    if (config == NULL) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "fontconfig: cannot load the configuration");
    }
    s = (fc_source *)ax_mem_alloc(a, sizeof *s, AX_ALIGNOF(fc_source));
    if (s == NULL) {
        FcConfigDestroy(config);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "fontconfig: out of memory");
    }
    memset(s, 0, sizeof *s);
    s->base.vtbl = &FC_VTBL;
    s->allocator = *a;
    s->config = config;
    ax_mutex_init(&s->mutex);
    *out = &s->base;
    return AX_OK;
}
