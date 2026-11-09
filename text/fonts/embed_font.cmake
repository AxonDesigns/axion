# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Embeds a font file as a C array of bytes (aligned for HarfBuzz and FreeType, which read tables
# in place). cmake -DINPUT=<file> -DOUTPUT=<file.c> -DSYMBOL=<name> -P embed_font.cmake

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _len)
math(EXPR _size "${_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n" _bytes "${_bytes}")
get_filename_component(_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
"/* Generated from text/fonts/${_name} by embed_font.cmake. Do not edit. */
#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#    define AX_FONT_ALIGN __declspec(align(16))
#else
#    define AX_FONT_ALIGN _Alignas(16)
#endif

extern const uint8_t ${SYMBOL}[];
extern const size_t ${SYMBOL}_size;

AX_FONT_ALIGN const uint8_t ${SYMBOL}[] = {
${_bytes}0x00};
const size_t ${SYMBOL}_size = ${_size};
")
