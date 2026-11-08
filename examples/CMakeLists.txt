# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0

if(AX_BUILD_PLATFORM)
    add_subdirectory(m0_window)
    if(AX_BUILD_GPU)
        add_subdirectory(m1_nodes) # the M1 pipeline presents with ax_render_wgpu
    endif()
endif()
