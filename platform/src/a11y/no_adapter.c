/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * No accessibility adapter in this build (AX_ACCESSKIT off, or no AccessKit binary for the target;
 * decision 108). The accessibility tree and the headless queries work the same without it.
 */
#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/platform/ax_a11y_adapter.h>

bool ax_a11y_adapter_available(void)
{
    return false;
}

ax_result ax_a11y_adapter_create(ax_window window, const ax_a11y_adapter_desc *desc,
                                 ax_a11y_adapter **out)
{
    (void)window;
    (void)desc;
    *out = NULL;
    return ax_set_error(AX_ERR_UNSUPPORTED,
                        "ax_a11y_adapter_create: built without an accessibility adapter");
}

void ax_a11y_adapter_destroy(ax_a11y_adapter *adapter)
{
    (void)adapter;
}

void ax_a11y_adapter_update(ax_a11y_adapter *adapter, const ax_a11y_tree_update *update)
{
    (void)adapter;
    (void)update;
}

void ax_a11y_adapter_set_window_focused(ax_a11y_adapter *adapter, bool focused)
{
    (void)adapter;
    (void)focused;
}
