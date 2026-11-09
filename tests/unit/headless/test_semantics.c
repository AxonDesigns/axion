/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The headless host's semantic tree (decision 108) on m1_nodes' cards: queries by role and name,
 * by text and by test id, the tree as text against its golden (in the determinism job), an
 * assistive technology's action reaching the app, a semantics-only change that presents nothing,
 * and the same queries with the host's threads.
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_a11y.h"
#include "axion/core/ax_frame.h"
#include "axion/headless/ax_headless.h"
#include "axt_golden.h"
#include "m1_cards.h"

typedef struct text_buf {
    char data[65536];
    size_t len;
} text_buf;

static void write_text(void *ctx, const char *s, size_t n)
{
    text_buf *t = (text_buf *)ctx;
    if (t->len + n < sizeof t->data) {
        memcpy(t->data + t->len, s, n);
        t->len += n;
        t->data[t->len] = '\0';
    }
}

static ax_headless *start(uint32_t flags, ax_node *root, m1_cards *c)
{
    ax_headless *h = NULL;
    ax_headless_desc d = ax_headless_default_desc();
    ax_headless_surface_desc s = ax_headless_default_surface_desc();
    d.flags = flags;
    s.width = 320.0f;
    s.height = 420.0f;
    s.scale = 1.0f;
    if (ax_headless_create(&d, &h) != AX_OK || ax_headless_add_surface(h, &s, root) != AX_OK ||
        m1_cards_build(ax_headless_world(h), *root, c, false, NULL) != AX_OK ||
        ax_headless_settle(h, 200, NULL) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "headless setup: %s", ax_last_error()->message.ptr);
        if (h != NULL) {
            ax_headless_destroy(h);
        }
        return NULL;
    }
    return h;
}

static void check_queries(ax_headless *h, ax_node root, const m1_cards *c)
{
    ax_node found[16];
    ax_headless_element e;
    char name[64];
    size_t len = 0;

    /* By role and name: six articles, one called "Layout". */
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, NULL, 0, found, 16), M1_CARDS);
    AX_REQUIRE(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, "Layout", 0, found, 16) == 1);
    AX_CHECK_EQ_U(found[0].h, c->cards[3].card.h);
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, "layout", 0, found, 16), 0);
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, "layout",
                                          AX_HEADLESS_MATCH_IGNORE_CASE, found, 16),
                  1);
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, "ing",
                                          AX_HEADLESS_MATCH_SUBSTRING, found, 16),
                  3); /* Shaping, Line breaking, Rendering */
    /* Headings: the page's (level 1) and one per card (level 2). */
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_HEADING, NULL, 0, found, 16),
                  M1_CARDS + 1u);
    AX_REQUIRE(ax_headless_get_by_role(h, root, AX_ROLE_HEADING, "AXION text", 0, found, 16) == 1);
    e.struct_size = sizeof e;
    AX_REQUIRE(ax_headless_get_element(h, root, found[0], &e) == AX_OK);
    AX_CHECK_EQ_U(e.level, 1);
    /* The avatars are hidden; the progress bars carry their value. */
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_PROGRESS, "Progress", 0, found, 16),
                  M1_CARDS);
    AX_REQUIRE(ax_headless_get_element(h, root, found[0], &e) == AX_OK);
    AX_CHECK((e.flags & AX_A11Y_HAS_VALUE) != 0 && e.value == 20.0f && e.value_max == 100.0f);

    /* By text: a text leaf, wherever its lines wrap. */
    AX_REQUIRE(ax_headless_get_by_text(h, root,
                                       "Moves, sizes and colors spring or ease from wherever "
                                       "they are when they change.",
                                       0, found, 16) == 1);
    AX_CHECK_EQ_U(
        ax_headless_get_by_text(h, root, "spring or ease", AX_HEADLESS_MATCH_SUBSTRING, found, 16),
        1);
    AX_CHECK_EQ_U(ax_headless_get_by_text(h, root, "not on the page", 0, found, 16), 0);

    /* By test id; and an element's name and bounds. */
    AX_REQUIRE(ax_headless_get_by_test_id(h, root, "card-0", found, 16) == 1);
    AX_CHECK_EQ_U(found[0].h, c->cards[0].card.h);
    AX_REQUIRE(ax_headless_get_name(h, root, found[0], name, sizeof name, &len) == AX_OK);
    AX_CHECK(strcmp(name, "Shaping") == 0 && len == 7);
    AX_REQUIRE(ax_headless_get_element(h, root, found[0], &e) == AX_OK);
    AX_CHECK_EQ_U(e.role, AX_ROLE_ARTICLE);
    AX_CHECK_EQ_U(e.actions, AX_ACTION_FOCUS | AX_ACTION_DEFAULT);
    AX_CHECK_EQ_U(e.expanded, AX_EXPANDED_COLLAPSED);
    AX_CHECK(e.bounds.x >= 20.0f && e.bounds.w > 200.0f && e.bounds.h > 40.0f);
    AX_CHECK_EQ_U(ax_headless_get_by_test_id(h, root, "card-9", found, 16), 0);
}

AX_TEST(m1_nodes_cards_are_found_by_role_name_text_and_test_id)
{
    static m1_cards c;
    ax_node root;
    ax_headless *h = start(0, &root, &c);
    if (h == NULL) {
        return;
    }
    check_queries(h, root, &c);
    ax_headless_destroy(h);
}

AX_TEST(the_semantic_tree_matches_its_golden)
{
    static m1_cards c;
    static text_buf compact, full;
    ax_node root;
    ax_headless *h = start(0, &root, &c);
    if (h == NULL) {
        return;
    }
    AX_REQUIRE(ax_headless_dump_semantics(h, root, AX_HEADLESS_DUMP_BOUNDS, write_text, &compact) ==
               AX_OK);
    AXT_CHECK_GOLDEN_TEXT("a11y", "m1_nodes", compact.data, compact.len);
    /* Every node and text run, with its characters' line text. */
    AX_REQUIRE(ax_headless_dump_semantics(h, root, AX_HEADLESS_DUMP_ALL | AX_HEADLESS_DUMP_BOUNDS,
                                          write_text, &full) == AX_OK);
    AXT_CHECK_GOLDEN_TEXT("a11y", "m1_nodes_all", full.data, full.len);
    ax_headless_destroy(h);
}

AX_TEST(an_assistive_technology_action_reaches_the_app)
{
    static m1_cards c;
    ax_node root, card[4];
    ax_headless *h = start(0, &root, &c);
    ax_world_event ev;
    ax_headless_element e;
    uint32_t events = 0;
    if (h == NULL) {
        return;
    }
    AX_REQUIRE(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, "Glyph atlas", 0, card, 4) == 1);
    AX_REQUIRE(ax_headless_a11y_action(h, root, card[0], AX_ACTION_DEFAULT, 0.0f) == AX_OK);
    AX_REQUIRE(ax_headless_a11y_action(h, root, card[0], AX_ACTION_FOCUS, 0.0f) == AX_OK);
    AX_REQUIRE(ax_headless_step(h, 1) == AX_OK);
    /* The app answers its events: the default action grows the card. */
    for (;;) {
        memset(&ev, 0, sizeof ev);
        ev.struct_size = sizeof ev;
        AX_REQUIRE(ax_world_poll_event(ax_headless_world(h), &ev) == AX_OK);
        if (ev.type == AX_WORLD_EVENT_NONE) {
            break;
        }
        if (ev.type == AX_WORLD_EVENT_A11Y_ACTION) {
            ax_node n;
            n.h = ev.data.a11y_action.node;
            AX_CHECK_EQ_U(n.h, card[0].h);
            if (ev.data.a11y_action.action == AX_ACTION_DEFAULT) {
                AX_CHECK_RESULT(m1_cards_toggle(ax_headless_world(h), &c, n, NULL), AX_OK);
            }
            events++;
        }
    }
    AX_CHECK_EQ_U(events, 2);
    AX_REQUIRE(ax_headless_settle(h, 200, NULL) == AX_OK);
    e.struct_size = sizeof e;
    AX_REQUIRE(ax_headless_get_element(h, root, card[0], &e) == AX_OK);
    AX_CHECK_EQ_U(e.expanded, AX_EXPANDED_EXPANDED);
    AX_CHECK(e.focused);
    /* Its detail paragraph is in the tree now. */
    AX_CHECK_EQ_U(ax_headless_get_by_text(h, root, "A run waits for all of its glyphs",
                                          AX_HEADLESS_MATCH_SUBSTRING, card, 4),
                  1);
    ax_headless_destroy(h);
}

AX_TEST(a_semantics_only_change_records_and_presents_nothing)
{
    static m1_cards c;
    ax_node root, found[2];
    ax_headless *h = start(0, &root, &c);
    ax_headless_surface_info before, after;
    ax_frame_counters fc;
    ax_txn *t;
    if (h == NULL) {
        return;
    }
    before.struct_size = sizeof before;
    after.struct_size = sizeof after;
    AX_REQUIRE(ax_headless_get_surface_info(h, root, &before) == AX_OK);
    ax_world_get_frame_counters(ax_headless_world(h), &fc, true);
    t = ax_txn_begin(ax_headless_world(h));
    ax_txn_set_str(t, c.cards[2].card, AX_PROP_LABEL, "Atlas", 5);
    ax_txn_set_str(t, c.cards[2].card, AX_PROP_DESCRIPTION, "Glyphs", 6);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(ax_headless_settle(h, 10, NULL) == AX_OK);
    AX_REQUIRE(ax_headless_get_surface_info(h, root, &after) == AX_OK);
    ax_world_get_frame_counters(ax_headless_world(h), &fc, false);
    AX_CHECK_EQ_U(fc.items_recorded, 0);
    AX_CHECK_EQ_U(fc.snapshots, 0);
    AX_CHECK_EQ_U(after.shown_ir, before.shown_ir); /* no new snapshot drawn */
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, "Atlas", 0, found, 2), 1);
    ax_headless_destroy(h);
}

AX_TEST(queries_work_with_the_hosts_threads_and_not_without_semantics)
{
    static m1_cards c;
    ax_node root, found[8];
    ax_headless *h;
    if (ax_threads_supported()) { /* not in the single-threaded web build */
        h = start(AX_HEADLESS_THREADS, &root, &c);
        if (h == NULL) {
            return;
        }
        check_queries(h, root, &c);
        ax_headless_destroy(h);
    }
    h = start(AX_HEADLESS_NO_SEMANTICS, &root, &c);
    if (h == NULL) {
        return;
    }
    AX_CHECK_EQ_U(ax_headless_get_by_role(h, root, AX_ROLE_ARTICLE, NULL, 0, found, 8), 0);
    AX_CHECK(!ax_world_a11y_enabled(ax_headless_world(h)));
    ax_headless_destroy(h);
}
