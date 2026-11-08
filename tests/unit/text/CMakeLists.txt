# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Text tests: the third-party libraries, fonts, shaping, line layout and glyphs.

ax_add_unit_test(text test_deps.c)
target_link_libraries(test_text_deps PRIVATE ax_harfbuzz ax_freetype ax_unibreak)

ax_add_unit_test(text test_fonts.c)
target_link_libraries(test_text_fonts PRIVATE ax_text ax_harfbuzz)
target_compile_definitions(test_text_fonts PRIVATE
    "AX_TEXT_FONTS_DIR=\"${PROJECT_SOURCE_DIR}/text/fonts\"")

# text depends only on base (and its libraries and the system's font APIs).
add_test(NAME text.includes.only_base
         COMMAND "${CMAKE_COMMAND}" "-DTEXT_DIR=${PROJECT_SOURCE_DIR}/text"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_includes.cmake")
set_tests_properties(text.includes.only_base PROPERTIES LABELS text)

ax_add_unit_test(text test_features.c)
target_link_libraries(test_text_features PRIVATE ax_text)

ax_add_unit_test(text test_shaping.c)
target_link_libraries(test_text_shaping PRIVATE ax_text)

ax_add_unit_test(text test_lines.c)
target_link_libraries(test_text_lines PRIVATE ax_text)
