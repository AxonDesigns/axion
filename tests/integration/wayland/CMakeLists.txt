# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Wayland integration tests. They need a compositor: run them through tools/ci/with-weston.sh,
# which starts a headless weston; without one they are skipped (or fail, in CI).

set(_ax_wayland_tests
    test_commit_failed.c
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

# The pipeline with ax_render_soft into wl_shm buffers (decision 84): needs no GPU.
ax_add_unit_test(wayland test_soft_pipeline.c)
target_sources(test_wayland_soft_pipeline PRIVATE wl_test_util.c)
target_link_libraries(test_wayland_soft_pipeline PRIVATE ax_runtime ax_render_soft ax_platform
    ${CMAKE_DL_LIBS})
target_include_directories(test_wayland_soft_pipeline PRIVATE
    "${PROJECT_SOURCE_DIR}/platform/src" "${CMAKE_CURRENT_SOURCE_DIR}")
# The fallback runs without a Vulkan driver, so the GPU renderer cannot be created; the pipeline
# tests time frames on a software renderer: run them alone.
set_property(TEST "wayland.soft_pipeline.without_a_usable_gpu_the_runtime_falls_back_to_the_software_renderer"
    APPEND PROPERTY ENVIRONMENT "VK_DRIVER_FILES=/nonexistent/axion.json"
                                "VK_ICD_FILENAMES=/nonexistent/axion.json")
get_property(_tests DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS _tests)
    if(_test MATCHES "^wayland\\.soft_pipeline\\.")
        set_property(TEST "${_test}" PROPERTY RUN_SERIAL TRUE)
    endif()
endforeach()
