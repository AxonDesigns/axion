# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_gpu_surface against a headless weston (run through tools/ci/with-weston.sh). These tests
# always present with Mesa's software Vulkan driver, lavapipe: the Vulkan loader is pointed at it
# with VK_DRIVER_FILES, so the machine's GPU driver (which may not support a headless compositor
# without dmabuf) never matters. Without lavapipe they skip, or fail in CI.

file(GLOB _ax_lavapipe_icd "/usr/share/vulkan/icd.d/lvp_icd*.json")
if(_ax_lavapipe_icd)
    list(GET _ax_lavapipe_icd 0 _ax_lavapipe_icd)
    message(STATUS "gpu_wayland tests: lavapipe at ${_ax_lavapipe_icd}")
else()
    message(STATUS "gpu_wayland tests: lavapipe not found; they will skip")
endif()

set(_ax_gpu_wayland_tests
    test_gpu_present.c
    test_ir_present.c)

foreach(_t IN LISTS _ax_gpu_wayland_tests)
    ax_add_unit_test(gpu_wayland "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_sources(test_gpu_wayland_${_n} PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/integration/wayland/wl_test_util.c"
        "${PROJECT_SOURCE_DIR}/tests/unit/gpu/gpu_test_util.c")
    target_link_libraries(test_gpu_wayland_${_n} PRIVATE ax_gpu ax_platform ax_render_wgpu
        ax_render_frame ${CMAKE_DL_LIBS})
    target_include_directories(test_gpu_wayland_${_n} PRIVATE
        "${PROJECT_SOURCE_DIR}/platform/src"
        "${PROJECT_SOURCE_DIR}/gpu/src"
        "${PROJECT_SOURCE_DIR}/tests/integration/wayland"
        "${PROJECT_SOURCE_DIR}/tests/unit/gpu")
endforeach()

get_property(_tests DIRECTORY PROPERTY TESTS)
# They time frames on a software renderer: under `ctest -j`, other tests competing for the CPU
# make them miss deadlines (lost frames, late acknowledgements). Run each alone; `RESOURCE_LOCK`
# would only keep them from overlapping one another.
set_property(TEST ${_tests} PROPERTY RUN_SERIAL TRUE)
if(_ax_lavapipe_icd)
    set_property(TEST ${_tests} APPEND PROPERTY ENVIRONMENT "VK_DRIVER_FILES=${_ax_lavapipe_icd}")
endif()
if("address" IN_LIST AX_SANITIZE)
    set_property(TEST ${_tests} APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/lsan-gpu-drivers.supp")
endif()
