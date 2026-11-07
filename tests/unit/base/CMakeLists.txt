# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# ax_add_unit_test is defined in tests/CMakeLists.txt; sources are relative to this directory.

set(_ax_base_tests
    test_abi_headers.c
    test_allocator.c
    test_arena.c
    test_array.c
    test_atomic.c
    test_error.c
    test_handle.c
    test_pool.c
    test_math.c
    test_thread.c
    test_triple_buffer.c
    test_types.c
    test_version.c)

foreach(_t IN LISTS _ax_base_tests)
    ax_add_unit_test(base "${_t}")
endforeach()
