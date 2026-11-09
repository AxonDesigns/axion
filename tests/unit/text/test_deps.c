/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Text's libraries, built from source (cmake/AxionTextDeps.cmake): the pinned versions link and
 * work from C.
 */
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <graphemebreak.h>
#include <hb.h>
#include <linebreak.h>
#include <unibreakdef.h>

#include "ax_test.h"

AX_TEST(the_pinned_versions_are_linked)
{
    FT_Library lib;
    FT_Int major, minor, patch;
    unsigned hb_major, hb_minor, hb_micro;
    AX_CHECK_EQ_I(FT_Init_FreeType(&lib), 0);
    FT_Library_Version(lib, &major, &minor, &patch);
    AX_CHECK_EQ_I(major, 2);
    AX_CHECK_EQ_I(minor, 15);
    AX_CHECK_EQ_I(patch, 0);
    FT_Done_FreeType(lib);

    hb_version(&hb_major, &hb_minor, &hb_micro);
    AX_CHECK_EQ_U(hb_major, 15u);
    AX_CHECK_EQ_U(hb_minor, 0u);
    AX_CHECK_EQ_U(hb_micro, 0u);
    AX_CHECK(strcmp(hb_version_string(), "15.0.0") == 0);

    AX_CHECK_EQ_I(unibreak_version, 0x0700);
}

AX_TEST(harfbuzz_shapes_from_c)
{
    /* An empty face maps every character to glyph 0; the buffer still gets one glyph per
     * character, with clusters at their UTF-8 byte offsets. */
    hb_blob_t *blob = hb_blob_get_empty();
    hb_face_t *face = hb_face_create(blob, 0);
    hb_font_t *font = hb_font_create(face);
    hb_buffer_t *buf = hb_buffer_create();
    unsigned n = 0;
    hb_glyph_info_t *info;
    hb_buffer_add_utf8(buf, "a\xC3\xA9z", -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font, buf, NULL, 0);
    info = hb_buffer_get_glyph_infos(buf, &n);
    AX_CHECK_EQ_U(n, 3u);
    AX_CHECK_EQ_U(info[0].cluster, 0u);
    AX_CHECK_EQ_U(info[1].cluster, 1u);
    AX_CHECK_EQ_U(info[2].cluster, 3u);
    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    hb_face_destroy(face);
}

AX_TEST(libunibreak_finds_line_and_grapheme_breaks)
{
    static const char text[] = "Hello, world e\xCC\x81!"; /* e + combining acute */
    char lb[sizeof text];
    char gb[sizeof text];
    const size_t n = sizeof text - 1u;
    init_linebreak();
    set_linebreaks_utf8((const utf8_t *)text, n, "en", lb);
    /* A break is allowed after "Hello, " (before "world"), not inside a word. */
    AX_CHECK_EQ_I(lb[6], LINEBREAK_ALLOWBREAK);
    AX_CHECK_EQ_I(lb[2], LINEBREAK_NOBREAK);
    /* The last character: libunibreak leaves the end of text to the caller. */
    AX_CHECK_EQ_I(lb[n - 1u], LINEBREAK_INDETERMINATE);

    init_graphemebreak();
    set_graphemebreaks_utf8((const utf8_t *)text, n, "en", gb);
    /* "e" + U+0301 (bytes 13..15) is one grapheme: no break after the "e". */
    AX_CHECK_EQ_I(gb[13], GRAPHEMEBREAK_NOBREAK);
    AX_CHECK_EQ_I(gb[15], GRAPHEMEBREAK_BREAK);
}
