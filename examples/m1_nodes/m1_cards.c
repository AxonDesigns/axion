/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m1_nodes' scene (m1_cards.h): every geometry comes from flex (decision 90), every change is a
 * transaction, and the scene animates it implicitly (decision 94): a reorder moves the cards with
 * the default spring, a card that grows lays its content out at each interpolated height (the
 * resize preset) behind its rounded clip (decision 93), a color eases.
 */
#include "m1_cards.h"

#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_math.h"

#define COLOR_BG      0xFF1E2129u
#define COLOR_CAPTION 0xFF2D3240u
#define COLOR_LINE    0xFFE8ECF4u
#define COLOR_DIM     0xFF8A93A6u
#define COLOR_DETAIL  0x33FFFFFFu

static const uint32_t PALETTE[] = {0xFF2F3B52u, 0xFF4F7CFFu, 0xFF5BD18Bu,
                                   0xFFF7C948u, 0xFFFF7A59u, 0xFFB57BFFu};
#define PALETTE_N (sizeof PALETTE / sizeof PALETTE[0])

ax_rgba m1_c8(uint32_t argb)
{
    return ax_rgba_from_srgb8((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                              (uint8_t)(argb >> 24));
}

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static ax_dimension pct(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_PERCENT;
    return d;
}

static ax_node box(ax_txn *t, ax_node parent, uint32_t argb, float radius)
{
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_color(t, n, AX_PROP_BACKGROUND_COLOR, m1_c8(argb));
    if (radius > 0.0f) {
        ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS, radius);
    }
    ax_txn_append_child(t, parent, n);
    return n;
}

static void build_card(ax_txn *t, m1_cards *c, uint32_t i)
{
    m1_card *k = &c->cards[i];
    ax_node lines, line;
    k->color = (uint32_t)i % PALETTE_N;
    k->card = box(t, c->list, PALETTE[k->color], 14.0f);
    ax_txn_set_u32(t, k->card, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    ax_txn_set_u32(t, k->card, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    ax_txn_set_u32(t, k->card, AX_PROP_OVERFLOW, AX_OVERFLOW_HIDDEN); /* its content clipped */
    ax_txn_set_dimension(t, k->card, AX_PROP_HEIGHT, pt(72));
    ax_txn_set_dimension(t, k->card, AX_PROP_PADDING, pt(12));
    ax_txn_set_dimension(t, k->card, AX_PROP_GAP, pt(12));
    ax_txn_set_f32(t, k->card, AX_PROP_FLEX_SHRINK, 0.0f);

    k->avatar = box(t, k->card, 0x40FFFFFFu, 12.0f);
    ax_txn_set_dimension(t, k->avatar, AX_PROP_WIDTH, pt(48));
    ax_txn_set_dimension(t, k->avatar, AX_PROP_HEIGHT, pt(48));

    lines = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, lines, AX_PROP_FLEX_GROW, 1.0f);
    ax_txn_set_dimension(t, lines, AX_PROP_GAP, pt(8));
    ax_txn_set_dimension(t, lines, AX_PROP_PADDING_TOP, pt(6));
    ax_txn_append_child(t, k->card, lines);
    line = box(t, lines, COLOR_LINE, 6.0f);
    ax_txn_set_dimension(t, line, AX_PROP_WIDTH, pct(40.0f + 7.0f * (float)i));
    ax_txn_set_dimension(t, line, AX_PROP_HEIGHT, pt(12));
    line = box(t, lines, COLOR_DIM, 5.0f);
    ax_txn_set_dimension(t, line, AX_PROP_WIDTH, pct(25.0f + 5.0f * (float)i));
    ax_txn_set_dimension(t, line, AX_PROP_HEIGHT, pt(10));
    /* Below the fold: seen when the card grows. */
    k->detail = box(t, lines, COLOR_DETAIL, 8.0f);
    ax_txn_set_dimension(t, k->detail, AX_PROP_HEIGHT, pt(48));
    ax_txn_set_dimension(t, k->detail, AX_PROP_MARGIN_TOP, pt(6));

    /* A progress bar along the bottom edge, rounded off by the card's clip. */
    k->progress = box(t, k->card, COLOR_LINE, 0.0f);
    ax_txn_set_u32(t, k->progress, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_dimension(t, k->progress, AX_PROP_LEFT, pt(0));
    ax_txn_set_dimension(t, k->progress, AX_PROP_BOTTOM, pt(0));
    ax_txn_set_dimension(t, k->progress, AX_PROP_HEIGHT, pt(4));
    ax_txn_set_dimension(t, k->progress, AX_PROP_WIDTH, pct(20.0f + 13.0f * (float)i));
    c->order[i] = k->card;
}

ax_result m1_cards_build(ax_world world, ax_node root, m1_cards *c, bool csd, uint64_t *out_seq)
{
    ax_txn *t = ax_txn_begin(world);
    uint32_t i;
    if (t == NULL) {
        return ax_last_error()->code;
    }
    memset(c, 0, sizeof *c);
    c->csd = csd;
    c->root = root;
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, m1_c8(COLOR_BG));
    if (csd) {
        ax_node caption = box(t, root, COLOR_CAPTION, 0.0f);
        ax_txn_set_dimension(t, caption, AX_PROP_HEIGHT, pt(M1_CAPTION_HEIGHT));
        ax_txn_set_f32(t, caption, AX_PROP_FLEX_SHRINK, 0.0f);
    }
    c->list = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, c->list, AX_PROP_FLEX_GROW, 1.0f);
    ax_txn_set_dimension(t, c->list, AX_PROP_PADDING, pt(20));
    ax_txn_set_dimension(t, c->list, AX_PROP_GAP, pt(12));
    ax_txn_set_u32(t, c->list, AX_PROP_OVERFLOW, AX_OVERFLOW_HIDDEN);
    ax_txn_append_child(t, root, c->list);
    for (i = 0; i < M1_CARDS; ++i) {
        build_card(t, c, i);
    }
    ax_txn_set_animated(t, false); /* the window's first content appears at once */
    return ax_txn_commit(t, out_seq);
}

ax_result m1_cards_step(ax_world world, m1_cards *c, uint32_t k, uint64_t *out_seq)
{
    ax_txn *t = ax_txn_begin(world);
    uint32_t i;
    if (t == NULL) {
        return ax_last_error()->code;
    }
    switch (k % 3u) {
    case 0: { /* the last card to the top: every card moves to its new place */
        ax_node last = c->order[M1_CARDS - 1];
        ax_txn_insert_before(t, c->list, last, c->order[0]);
        for (i = M1_CARDS - 1; i > 0; --i) {
            c->order[i] = c->order[i - 1];
        }
        c->order[0] = last;
        break;
    }
    case 1: { /* a card grows (or shrinks back): its content follows the interpolated height */
        m1_card *card = &c->cards[(k / 3u) % M1_CARDS];
        card->grown = !card->grown;
        ax_txn_set_dimension(t, card->card, AX_PROP_HEIGHT, pt(card->grown ? 132.0f : 72.0f));
        break;
    }
    default: { /* a card takes the next color */
        m1_card *card = &c->cards[(k / 3u + 2u) % M1_CARDS];
        card->color = (card->color + 1u) % PALETTE_N;
        ax_txn_set_color(t, card->card, AX_PROP_BACKGROUND_COLOR, m1_c8(PALETTE[card->color]));
        break;
    }
    }
    return ax_txn_commit(t, out_seq);
}
