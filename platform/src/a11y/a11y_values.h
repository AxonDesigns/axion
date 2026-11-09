/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The numbers of core's ax_role, ax_toggled, ax_expanded and AX_ACTION_* (ax_prop.h, fixed
 * forever), for the adapters: platform depends only on base, so it keeps its own copy, and the
 * runtime, which sees both, checks they are equal (runtime/src/a11y_bridge.c).
 */
#ifndef AX_PLATFORM_A11Y_VALUES_H
#define AX_PLATFORM_A11Y_VALUES_H

enum {
    PA_ROLE_NONE = 0,
    PA_ROLE_GENERIC = 1,
    PA_ROLE_WINDOW = 2,
    PA_ROLE_GROUP = 3,
    PA_ROLE_LABEL = 4,
    PA_ROLE_HEADING = 5,
    PA_ROLE_PARAGRAPH = 6,
    PA_ROLE_BUTTON = 7,
    PA_ROLE_LINK = 8,
    PA_ROLE_IMAGE = 9,
    PA_ROLE_CHECKBOX = 10,
    PA_ROLE_SWITCH = 11,
    PA_ROLE_RADIO = 12,
    PA_ROLE_SLIDER = 13,
    PA_ROLE_PROGRESS = 14,
    PA_ROLE_TEXT_INPUT = 15,
    PA_ROLE_LIST = 16,
    PA_ROLE_LIST_ITEM = 17,
    PA_ROLE_ARTICLE = 18,
    PA_ROLE_DIALOG = 19,
    PA_ROLE_TOOLBAR = 20,
    PA_ROLE_TAB_LIST = 21,
    PA_ROLE_TAB = 22,
    PA_ROLE_TAB_PANEL = 23,
    PA_ROLE_MENU = 24,
    PA_ROLE_MENU_ITEM = 25,
    PA_ROLE_SCROLL_VIEW = 26,
    PA_ROLE_SEPARATOR = 27,
    PA_ROLE_TEXT_RUN = 28
};

enum { PA_TOGGLED_NONE = 0, PA_TOGGLED_FALSE = 1, PA_TOGGLED_TRUE = 2, PA_TOGGLED_MIXED = 3 };
enum { PA_EXPANDED_NONE = 0, PA_EXPANDED_COLLAPSED = 1, PA_EXPANDED_EXPANDED = 2 };

#define PA_ACTION_FOCUS     0x01u
#define PA_ACTION_DEFAULT   0x02u
#define PA_ACTION_SET_VALUE 0x04u
#define PA_ACTION_INCREMENT 0x08u
#define PA_ACTION_DECREMENT 0x10u

#endif
