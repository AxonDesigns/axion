# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0

add_executable(m0_window main.c shm.c)
target_link_libraries(m0_window PRIVATE ax_platform ax_base m)
if(AX_BUILD_GPU)
    target_sources(m0_window PRIVATE ir.c scene.c)
    target_link_libraries(m0_window PRIVATE ax_render_wgpu ax_render_frame)
    target_compile_definitions(m0_window PRIVATE M0_HAVE_IR=1)
endif()
ax_target_defaults(m0_window)
