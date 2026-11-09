/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Builds without a system font source (not Linux, or AX_TEXT_FONTCONFIG off): only the bundled
 * set. DirectWrite and Core Text sources arrive with their platforms.
 */
#include <axion/base/ax_error_set.h>
#include <axion/text/ax_font_source.h>

ax_result ax_font_source_fontconfig_create(const ax_allocator *allocator, ax_font_source **out)
{
    (void)allocator;
    *out = NULL;
    return ax_set_error(AX_ERR_UNSUPPORTED, "fontconfig: not in this build");
}
