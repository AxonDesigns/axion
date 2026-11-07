# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0

add_executable(m0_window main.c)
target_link_libraries(m0_window PRIVATE ax_platform ax_base m)
ax_target_defaults(m0_window)
