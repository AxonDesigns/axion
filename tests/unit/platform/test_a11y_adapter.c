/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The accessibility adapter (decision 108): created, fed updates while no assistive technology
 * reads it (dropped: nothing is built), and destroyed; with or without a session bus (CI has
 * none), it must neither fail nor abort. Without AccessKit in the build it reports
 * AX_ERR_UNSUPPORTED.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/platform/ax_a11y_adapter.h"

static void on_activated(void *user, bool active)
{
    (void)user;
    (void)active;
}

static void on_action(void *user, uint64_t target, uint32_t action, float value)
{
    (void)user;
    (void)target;
    (void)action;
    (void)value;
}

AX_TEST(an_adapter_lives_and_drops_updates_while_nothing_reads_it)
{
    ax_a11y_adapter_desc d;
    ax_a11y_adapter *a = NULL;
    ax_window w = AX_HANDLE_NULL_INIT;
    ax_a11y_tree_node nodes[2];
    ax_a11y_tree_update u;
    uint64_t kids[1] = {2};
    ax_result r;
    int i;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.root_id = 1;
    d.root_name = "test";
    d.activated = on_activated;
    d.action = on_action;
    r = ax_a11y_adapter_create(w, &d, &a);
    if (!ax_a11y_adapter_available()) {
        AX_CHECK_RESULT(r, AX_ERR_UNSUPPORTED);
        AX_CHECK(a == NULL);
        AX_SKIP("built without AccessKit");
    }
    AX_REQUIRE(r == AX_OK && a != NULL);
    memset(nodes, 0, sizeof nodes);
    nodes[0].id = 1;
    nodes[0].role = 2; /* window */
    nodes[0].children = kids;
    nodes[0].child_count = 1;
    nodes[0].transform[0] = nodes[0].transform[3] = 1.0f;
    nodes[1].id = 2;
    nodes[1].role = 7; /* button */
    nodes[1].name.ptr = "OK";
    nodes[1].name.len = 2;
    nodes[1].transform[0] = nodes[1].transform[3] = 1.0f;
    memset(&u, 0, sizeof u);
    u.root = 1;
    u.focus = 1;
    u.full = true;
    u.nodes = nodes;
    u.count = 2;
    for (i = 0; i < 100; ++i) {
        ax_a11y_adapter_update(a, &u);
        u.full = false;
    }
    ax_a11y_adapter_set_window_focused(a, true);
    ax_a11y_adapter_destroy(a);
    /* A second one, as a second window would have. */
    AX_REQUIRE(ax_a11y_adapter_create(w, &d, &a) == AX_OK);
    ax_a11y_adapter_destroy(a);
    d.struct_size = 4;
    AX_CHECK_RESULT(ax_a11y_adapter_create(w, &d, &a), AX_ERR_ABI_MISMATCH);
}
