# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Render IR tests: building, validation, serialization, the text dump and publication.

set(_ax_render_ir_tests
    test_builder.c
    test_mailbox.c
    test_serialize.c)

foreach(_t IN LISTS _ax_render_ir_tests)
    ax_add_unit_test(render_ir "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_render_ir_${_n} PRIVATE ax_render_ir)
endforeach()

# Glyph runs (minor 1): drawn by the software renderer too.
ax_add_unit_test(render_ir test_glyph_runs.c)
target_link_libraries(test_render_ir_glyph_runs PRIVATE ax_render_ir ax_render_soft)
