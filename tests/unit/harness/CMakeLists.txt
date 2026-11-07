# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Self-tests of the test harness. Death tests exist only in builds with assertions.

ax_add_unit_test(harness test_death.c)

if(AX_TESTS_HAVE_ASSERTS)
    # These two must fail as death tests; WILL_FAIL turns that into a pass.
    set_tests_properties(harness.death.exiting_without_abort_fails harness.death.returning_fails
                         PROPERTIES WILL_FAIL TRUE)
endif()
