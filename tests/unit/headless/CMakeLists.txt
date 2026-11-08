# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# The headless host's tests: the PNG codec (decision 82).

ax_add_unit_test(headless test_png.c)
target_link_libraries(test_headless_png PRIVATE ax_headless)
