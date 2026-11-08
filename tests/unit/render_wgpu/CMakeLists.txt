# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_render_wgpu offscreen tests: coverage against the provisional CPU reference (decision 56).
# They need an adapter and skip without one, like gpu/'s tests.

ax_add_unit_test(render_wgpu test_coverage.c)
target_sources(test_render_wgpu_coverage PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/unit/render_soft/coverage_reference.c"
    "${PROJECT_SOURCE_DIR}/tests/unit/gpu/gpu_test_util.c")
target_include_directories(test_render_wgpu_coverage PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/tests/unit/gpu"
    "${PROJECT_SOURCE_DIR}/tests/unit/render_soft" "${PROJECT_SOURCE_DIR}/render/wgpu/src")
target_link_libraries(test_render_wgpu_coverage PRIVATE ax_render_wgpu ${CMAKE_DL_LIBS})

if("address" IN_LIST AX_SANITIZE)
    get_property(_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_tests} APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/lsan-gpu-drivers.supp")
endif()
