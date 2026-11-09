# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Runtime tests: the frame pipeline in single-threaded mode (deterministic, a manual clock and a
# fake presenter) and with its threads (fake presenter, under TSan). They need no platform, so
# they also run in the Emscripten build.

set(_ax_runtime_tests
    test_single_thread.c
    test_threads.c)

foreach(_t IN LISTS _ax_runtime_tests)
    ax_add_unit_test(runtime "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_runtime_${_n} PRIVATE ax_runtime)
endforeach()

# Platform's copy of core's semantic values (decision 108), where platform builds.
if(AX_BUILD_PLATFORM)
    ax_add_unit_test(runtime test_a11y_values.c)
    target_link_libraries(test_runtime_a11y_values PRIVATE ax_runtime)
    target_include_directories(test_runtime_a11y_values PRIVATE "${PROJECT_SOURCE_DIR}/platform/src")
endif()
