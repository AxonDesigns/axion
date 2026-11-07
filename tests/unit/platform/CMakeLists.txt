# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Unit tests of the platform module's backend-independent code. They test internals, so they see
# platform/src/ as well as the public headers.

set(_ax_platform_unit_tests
    test_event_queue.c
    test_frame_clock.c
    test_hit_test.c
    test_triple_buffer.c)

foreach(_t IN LISTS _ax_platform_unit_tests)
    ax_add_unit_test(platform "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_platform_${_n} PRIVATE ax_platform)
    target_include_directories(test_platform_${_n} PRIVATE "${PROJECT_SOURCE_DIR}/platform/src")
endforeach()
