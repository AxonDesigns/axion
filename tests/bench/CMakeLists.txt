# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Reference benchmarks. Built with the tests so they keep compiling, never registered with ctest
# and never a CI gate: run them by hand on a Release preset (decisions.md records the numbers).

add_executable(bench_core bench_core.c)
target_link_libraries(bench_core PRIVATE ax_core)
ax_target_defaults(bench_core)
