/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * FreeType's options for AXION (FT_CONFIG_OPTIONS_H, cmake/AxionTextDeps.cmake): FreeType's
 * defaults, without the parts that need code AXION does not build: gzip and LZW (WOFF and
 * compressed PCF fonts) and OT-SVG glyphs (they need an external SVG renderer).
 */
#ifndef AX_FTOPTION_H
#define AX_FTOPTION_H

#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_SVG

#endif /* AX_FTOPTION_H */
