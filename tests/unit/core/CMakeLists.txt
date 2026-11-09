# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Core tests: the node store, properties, transactions and the app thread's mirror.

set(_ax_core_tests
    test_anim.c
    test_anim_resize.c
    test_anim_curves.c
    test_abi_headers.c
    test_a11y.c
    test_apply.c
    test_dirty.c
    test_events.c
    test_frame.c
    test_layout.c
    test_layout_yoga.c
    test_mirror.c
    test_node_store.c
    test_props.c
    test_semantics.c
    test_text_layout.c
    test_text_style.c
    test_txn.c)

foreach(_t IN LISTS _ax_core_tests)
    ax_add_unit_test(core "${_t}")
    get_filename_component(_n "${_t}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _n "${_n}")
    target_link_libraries(test_core_${_n} PRIVATE ax_core)
endforeach()

# Core meets the renderers only through the Render IR (decision 76).
add_test(NAME core.includes.only_the_render_ir
         COMMAND "${CMAKE_COMMAND}" "-DCORE_DIR=${PROJECT_SOURCE_DIR}/core"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_includes.cmake")
set_tests_properties(core.includes.only_the_render_ir PROPERTIES LABELS core)
