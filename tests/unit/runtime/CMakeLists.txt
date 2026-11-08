# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Runtime tests: the frame pipeline in single-threaded mode (deterministic, a manual clock and a
# fake presenter) and with its threads (fake presenter, under TSan). They need no platform, so
# they also run in the Emscripten build.

set(_ax_runtime_tests
    test_single_thread.c)

foreach(_t IN LISTS _ax_runtime_tests)
    ax_add_unit_test(runtime "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_runtime_${_n} PRIVATE ax_runtime)
endforeach()
