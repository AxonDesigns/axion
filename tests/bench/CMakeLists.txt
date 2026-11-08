# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Reference benchmarks. Built with the tests so they keep compiling, never registered with ctest
# and never a CI gate: run them by hand on a Release preset (decisions.md records the numbers).

add_executable(bench_core bench_core.c)
target_link_libraries(bench_core PRIVATE ax_core)
ax_target_defaults(bench_core)

# The layout engine: 10 001 flex nodes, full and incremental, boundaries in parallel (decision 92).
add_executable(bench_layout bench_layout.c)
target_link_libraries(bench_layout PRIVATE ax_core)
ax_target_defaults(bench_layout)

# ax_render_soft on m1_nodes' scene (decision 87), recorded headless.
add_executable(bench_soft bench_soft.c bench_soft_scene.c)
target_link_libraries(bench_soft PRIVATE ax_headless ax_render_soft ax_core)
ax_target_defaults(bench_soft)
