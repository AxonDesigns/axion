# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_soft's tests (decision 81): the supersampled reference, the IR's rules, clips, group
# opacity, blending spaces and determinism. No GPU and no window, so they run everywhere.

set(_ax_render_soft_tests
    test_determinism.c
    test_reference.c
    test_simd.c
    test_soft.c)

foreach(_t IN LISTS _ax_render_soft_tests)
    ax_add_unit_test(render_soft "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_render_soft_${_n} PRIVATE ax_render_soft ax_test_scenes)
endforeach()
target_sources(test_render_soft_reference PRIVATE coverage_reference.c)
# test_simd compares the two paths through the renderer's internals.
target_include_directories(test_render_soft_simd PRIVATE "${PROJECT_SOURCE_DIR}/render/soft/src")
