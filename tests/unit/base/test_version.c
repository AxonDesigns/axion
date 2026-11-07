/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "axion/ax_version.h"

AX_TEST(runtime_matches_header)
{
    AX_CHECK_EQ_U(ax_version(), AX_VERSION);
}

AX_TEST(pack_and_unpack)
{
    uint32_t v = AX_MAKE_VERSION(3, 1023, 4095);
    AX_CHECK_EQ_U(AX_VERSION_GET_MAJOR(v), 3);
    AX_CHECK_EQ_U(AX_VERSION_GET_MINOR(v), 1023);
    AX_CHECK_EQ_U(AX_VERSION_GET_PATCH(v), 4095);

    AX_CHECK_EQ_U(AX_VERSION_GET_MAJOR(ax_version()), AX_VERSION_MAJOR);
    AX_CHECK_EQ_U(AX_VERSION_GET_MINOR(ax_version()), AX_VERSION_MINOR);
    AX_CHECK_EQ_U(AX_VERSION_GET_PATCH(ax_version()), AX_VERSION_PATCH);
}

AX_TEST(packed_versions_order)
{
    AX_CHECK(AX_MAKE_VERSION(1, 0, 0) > AX_MAKE_VERSION(0, 1023, 4095));
    AX_CHECK(AX_MAKE_VERSION(1, 2, 0) > AX_MAKE_VERSION(1, 1, 4095));
    AX_CHECK(AX_MAKE_VERSION(1, 2, 3) > AX_MAKE_VERSION(1, 2, 2));
}
