# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# render: the Render IR contract (ir/) and the renderers that consume it.

add_subdirectory(ir)
if(AX_BUILD_GPU)
    add_subdirectory(wgpu)
endif()
if(AX_BUILD_PLATFORM)
    add_subdirectory(frame)
endif()
