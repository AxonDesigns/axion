# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Core tests: the node store, properties, transactions and the app thread's mirror.

set(_ax_core_tests
    test_abi_headers.c
    test_dirty.c
    test_node_store.c
    test_props.c
    test_txn.c)

foreach(_t IN LISTS _ax_core_tests)
    ax_add_unit_test(core "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_core_${_n} PRIVATE ax_core)
endforeach()
