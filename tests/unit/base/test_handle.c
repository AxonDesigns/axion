/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "axion/ax_handle.h"

AX_DECLARE_HANDLE(test_widget);
AX_DECLARE_HANDLE(test_gadget);

AX_TEST(layout_is_slot_low_generation_high)
{
    uint64_t h = ax_handle_make(0x12345678u, 0x9ABCDEF0u);
    AX_CHECK_EQ_U(h, 0x9ABCDEF012345678ull);
    AX_CHECK_EQ_U(ax_handle_slot(h), 0x12345678u);
    AX_CHECK_EQ_U(ax_handle_generation(h), 0x9ABCDEF0u);
}

AX_TEST(null_handle)
{
    test_widget w = AX_HANDLE_NULL_INIT;
    test_widget v;
    v.h = ax_handle_make(0, 1);
    AX_CHECK(AX_HANDLE_IS_NULL(w));
    AX_CHECK(!AX_HANDLE_IS_NULL(v)); /* slot 0 is valid; generations start at 1 */
    AX_CHECK(!AX_HANDLE_EQ(w, v));
}

AX_TEST(handle_types_are_distinct_and_64_bit)
{
    AX_CHECK_EQ_U(sizeof(test_widget), 8);
    AX_CHECK_EQ_U(sizeof(test_gadget), 8);
    /* Passing a test_gadget where a test_widget is expected does not compile; nothing to run. */
}
