# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# The headless host's tests: the PNG codec (decision 82) and the host (decision 85).

ax_add_unit_test(headless test_png.c)
target_link_libraries(test_headless_png PRIVATE ax_headless)

ax_add_unit_test(headless test_headless.c)
target_link_libraries(test_headless_headless PRIVATE ax_headless ax_test_golden)
if(AX_BUILD_GPU)
    # The GPU test keeps the drivers loaded until exit, so LeakSanitizer can name them.
    target_sources(test_headless_headless PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/gpu/gpu_test_util.c")
    target_include_directories(test_headless_headless PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/gpu")
    target_compile_definitions(test_headless_headless PRIVATE AXT_HAVE_GPU=1)
    target_link_libraries(test_headless_headless PRIVATE ${CMAKE_DL_LIBS})
endif()
foreach(_case a_tree_is_drawn_after_settle_and_matches_its_golden
              instances_on_threads_in_parallel_draw_the_same_pixels)
    set_property(TEST "headless.headless.${_case}" APPEND PROPERTY LABELS golden)
endforeach()

# The GPU renderer's test loads GPU drivers, which leak (decision 44): their suppressions.
if("address" IN_LIST AX_SANITIZE)
    get_property(_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_tests} APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/lsan-gpu-drivers.supp")
endif()

# Goldens of animation frames (decision 94), on m1_nodes' cards.
ax_add_unit_test(headless test_anim_goldens.c)
target_sources(test_headless_anim_goldens PRIVATE "${PROJECT_SOURCE_DIR}/examples/m1_nodes/m1_cards.c")
target_include_directories(test_headless_anim_goldens PRIVATE "${PROJECT_SOURCE_DIR}/examples/m1_nodes")
target_link_libraries(test_headless_anim_goldens PRIVATE ax_headless ax_test_golden)
set_property(TEST headless.anim_goldens.cards_reorder_grow_and_change_color_frame_by_frame
             APPEND PROPERTY LABELS golden)

# Goldens of text (decisions 99 to 101): paragraphs at several sizes, widths and scales.
ax_add_unit_test(headless test_text_goldens.c)
target_link_libraries(test_headless_text_goldens PRIVATE ax_headless ax_test_golden)
if(AX_BUILD_GPU)
    target_sources(test_headless_text_goldens PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/unit/gpu/gpu_test_util.c")
    target_include_directories(test_headless_text_goldens PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/gpu")
    target_compile_definitions(test_headless_text_goldens PRIVATE AXT_HAVE_GPU=1)
    target_link_libraries(test_headless_text_goldens PRIVATE ${CMAKE_DL_LIBS})
endif()
foreach(_case paragraphs_at_small_sizes paragraphs_wrap_at_several_widths_and_large_sizes)
    set_property(TEST "headless.text_goldens.${_case}" APPEND PROPERTY LABELS golden)
endforeach()
if("address" IN_LIST AX_SANITIZE)
    set_property(TEST headless.text_goldens.the_gpu_renderer_draws_text_like_the_software_one
                 APPEND PROPERTY ENVIRONMENT
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/lsan-gpu-drivers.supp")
endif()

# The semantic tree (decision 108): queries and its golden on m1_nodes' cards.
ax_add_unit_test(headless test_semantics.c)
target_sources(test_headless_semantics PRIVATE "${PROJECT_SOURCE_DIR}/examples/m1_nodes/m1_cards.c")
target_include_directories(test_headless_semantics PRIVATE "${PROJECT_SOURCE_DIR}/examples/m1_nodes")
target_link_libraries(test_headless_semantics PRIVATE ax_headless ax_test_golden)
set_property(TEST headless.semantics.the_semantic_tree_matches_its_golden APPEND PROPERTY LABELS golden)
