# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Wayland integration tests. They need a compositor: run them through tools/ci/with-weston.sh,
# which starts a headless weston; without one they are skipped (or fail, in CI).

set(_ax_wayland_tests
    test_hit_regions.c
    test_present.c
    test_window.c)

foreach(_t IN LISTS _ax_wayland_tests)
    ax_add_unit_test(wayland "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_sources(test_wayland_${_n} PRIVATE wl_test_util.c)
    target_link_libraries(test_wayland_${_n} PRIVATE ax_platform)
    target_include_directories(test_wayland_${_n} PRIVATE
        "${PROJECT_SOURCE_DIR}/platform/src" "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
