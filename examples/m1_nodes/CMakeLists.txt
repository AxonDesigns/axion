# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# m1_nodes: the M1 pipeline in a window (m1_cards.c is shared with the headless animation goldens).

add_executable(m1_nodes main.c m1_cards.c m1_blend.c)
target_link_libraries(m1_nodes PRIVATE ax_runtime ax_render_soft ax_render_wgpu ax_platform
                                       ax_core ax_text ax_base m)
ax_target_defaults(m1_nodes)
