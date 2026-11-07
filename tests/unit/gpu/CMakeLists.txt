# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# gpu/ tests that need no window. Tests that need an adapter skip themselves when none exists.

set(_ax_gpu_unit_tests
    test_device.c
    test_device_lost.c
    test_webgpu_link.c)

foreach(_t IN LISTS _ax_gpu_unit_tests)
    ax_add_unit_test(gpu "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_sources(test_gpu_${_n} PRIVATE gpu_test_util.c)
    target_include_directories(test_gpu_${_n} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/gpu/src")
    target_link_libraries(test_gpu_${_n} PRIVATE ax_gpu ${CMAKE_DL_LIBS})
endforeach()

# GPU drivers leak process-lifetime allocations; LeakSanitizer ignores the ones in driver modules
# (the file explains why full stacks are needed). Nothing in AXION, Dawn or wgpu-native is
# suppressed.
if("address" IN_LIST AX_SANITIZE)
    get_property(_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_tests} APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/lsan-gpu-drivers.supp")
endif()
