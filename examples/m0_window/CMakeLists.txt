# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0

add_executable(m0_window main.c shm.c)
target_link_libraries(m0_window PRIVATE ax_platform ax_base m)
if(AX_BUILD_GPU)
    target_sources(m0_window PRIVATE gpu.c)
    target_link_libraries(m0_window PRIVATE ax_gpu)
    target_compile_definitions(m0_window PRIVATE M0_HAVE_GPU=1)
endif()
ax_target_defaults(m0_window)
