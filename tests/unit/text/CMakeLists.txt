# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Text tests: the third-party libraries, fonts, shaping, line layout and glyphs.

ax_add_unit_test(text test_deps.c)
target_link_libraries(test_text_deps PRIVATE ax_harfbuzz ax_freetype ax_unibreak)
