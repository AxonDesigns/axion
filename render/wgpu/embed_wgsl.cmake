# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Embeds a WGSL file as a C array (bytes, not a string literal: MSVC limits string literal
# length). cmake -DINPUT=<file.wgsl> -DOUTPUT=<file.c> -DSYMBOL=<name> -P embed_wgsl.cmake

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _len)
math(EXPR _size "${_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " _bytes "${_bytes}")
file(WRITE "${OUTPUT}"
"/* Generated from ${INPUT} by embed_wgsl.cmake. Do not edit. */
#include <stddef.h>

extern const char ${SYMBOL}[];
extern const size_t ${SYMBOL}_size;

const char ${SYMBOL}[] = {
    ${_bytes}0x00};
const size_t ${SYMBOL}_size = ${_size};
")
