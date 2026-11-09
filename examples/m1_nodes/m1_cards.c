/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m1_nodes' scene (m1_cards.h): every geometry comes from flex (decision 90), text is measured by
 * its paragraphs (decision 99), every change is a transaction, and the scene animates it
 * implicitly (decision 94): a reorder moves the cards with the default spring, a card that grows
 * lays its content out at each interpolated height (the resize preset) behind its rounded clip
 * (decision 93), a color eases, and a narrower list re-wraps the paragraphs at each interpolated
 * width.
 *
 * Semantics (decision 108): each card is an article named by its title, with a heading, its
 * paragraphs, and a progress bar; the avatar is decorative (hidden). A card's default action grows
 * or shrinks it (m1_nodes answers AX_WORLD_EVENT_A11Y_ACTION with m1_cards_toggle).
 */
#include "m1_cards.h"

#include <stdio.h>
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_math.h"

#define COLOR_BG      0xFF1E2129u
#define COLOR_CAPTION 0xFF2D3240u
#define COLOR_LINE    0xFFE8ECF4u
#define COLOR_DIM     0xFFB4BCCCu
#define COLOR_DETAIL  0x33FFFFFFu

/* Dark enough for light text. */
static const uint32_t PALETTE[] = {0xFF2F3B52u, 0xFF3B4FB8u, 0xFF2B7A3Eu,
                                   0xFF8A6200u, 0xFFA8401Cu, 0xFF6440C8u};

static const char *const TITLES[M1_CARDS] = {
    "Shaping", "Line breaking", "Glyph atlas", "Layout", "Animation", "Rendering"};
static const char *const PARAGRAPHS[M1_CARDS] = {
    "HarfBuzz shapes each run once, in font units, so one result serves every size.",
    "Lines break at UAX #14 opportunities and wrap again without shaping when the card narrows.",
    "Coverage glyphs at the exact size, four subpixel positions, rasterized on the workers.",
    "Flex measures every paragraph through its measure function, in parallel when it pays.",
    "Moves, sizes and colors spring or ease from wherever they are when they change.",
    "The same snapshot draws on the GPU and in software, within a perceptual tolerance."};
static const char *const DETAILS[M1_CARDS] = {
    "Ligatures (fi, ffl), kerning (AV, To) and features such as tnum: 0123456789.",
    "Spaces at the end of a line hang; a word longer than the line overflows it.",
    "A run waits for all of its glyphs: words never appear in part.",
    "Text leaves are measured at the width their parent gives them.",
    "The detail grows into view as the card's height animates.",
    "Text coverage is corrected for its luminance before blending."};
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

static ax_node label(ax_txn *t, ax_node parent, const char *s, float size, float weight,
                     uint32_t argb)
{
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_str(t, n, AX_PROP_TEXT, s, strlen(s));
    ax_txn_set_f32(t, n, AX_PROP_FONT_SIZE, size);
    if (weight != 400.0f) {
        ax_txn_set_f32(t, n, AX_PROP_FONT_WEIGHT, weight);
    }
    if (argb != 0) {
        ax_txn_set_color(t, n, AX_PROP_COLOR, m1_c8(argb));
    }
    ax_txn_append_child(t, parent, n);
    return n;
}

static void build_card(ax_txn *t, m1_cards *c, uint32_t i)
{
    m1_card *k = &c->cards[i];
    ax_node lines, letter;
    char initial[2] = {0, 0};
    k->color = (uint32_t)i % PALETTE_N;
    k->card = box(t, c->list, PALETTE[k->color], 14.0f);
    {
        char id[16];
        (void)snprintf(id, sizeof id, "card-%u", (unsigned)i);
        ax_txn_set_u32(t, k->card, AX_PROP_ROLE, AX_ROLE_ARTICLE);
        ax_txn_set_str(t, k->card, AX_PROP_LABEL, TITLES[i], strlen(TITLES[i]));
        ax_txn_set_str(t, k->card, AX_PROP_TEST_ID, id, strlen(id));
        ax_txn_set_u32(t, k->card, AX_PROP_EXPANDED, AX_EXPANDED_COLLAPSED);
        ax_txn_set_u32(t, k->card, AX_PROP_ACTIONS, AX_ACTION_FOCUS | AX_ACTION_DEFAULT);
    }
    ax_txn_set_u32(t, k->card, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    ax_txn_set_u32(t, k->card, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    ax_txn_set_u32(t, k->card, AX_PROP_OVERFLOW, AX_OVERFLOW_HIDDEN); /* its content clipped */
    ax_txn_set_dimension(t, k->card, AX_PROP_PADDING, pt(12));
    ax_txn_set_dimension(t, k->card, AX_PROP_PADDING_BOTTOM, pt(16));
    ax_txn_set_dimension(t, k->card, AX_PROP_GAP, pt(12));
    ax_txn_set_f32(t, k->card, AX_PROP_FLEX_SHRINK, 0.0f);

    /* An avatar with the title's initial at 28 px. */
    k->avatar = box(t, k->card, 0x40FFFFFFu, 12.0f);
    ax_txn_set_dimension(t, k->avatar, AX_PROP_WIDTH, pt(48));
    ax_txn_set_dimension(t, k->avatar, AX_PROP_HEIGHT, pt(48));
    ax_txn_set_f32(t, k->avatar, AX_PROP_FLEX_SHRINK, 0.0f);
    ax_txn_set_u32(t, k->avatar, AX_PROP_ALIGN_ITEMS, AX_ALIGN_CENTER);
    ax_txn_set_u32(t, k->avatar, AX_PROP_JUSTIFY_CONTENT, AX_JUSTIFY_CENTER);
    ax_txn_set_bool(t, k->avatar, AX_PROP_HIDDEN, true); /* decorative: the title names the card */
    initial[0] = TITLES[i][0];
    letter = label(t, k->avatar, initial, 28.0f, 600.0f, 0);

    lines = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, lines, AX_PROP_FLEX_GROW, 1.0f);
    ax_txn_set_f32(t, lines, AX_PROP_FLEX_SHRINK, 1.0f);
    ax_txn_set_dimension(t, lines, AX_PROP_GAP, pt(4));
    ax_txn_set_dimension(t, lines, AX_PROP_MIN_WIDTH, pt(0));
    ax_txn_append_child(t, k->card, lines);
    {
        ax_node title = label(t, lines, TITLES[i], 20.0f, 600.0f, 0);
        ax_txn_set_u32(t, title, AX_PROP_ROLE, AX_ROLE_HEADING);
        ax_txn_set_u32(t, title, AX_PROP_LEVEL, 2);
    }
    (void)label(t, lines, PARAGRAPHS[i], 14.0f, 400.0f, COLOR_DIM);
    /* Below the fold: shown when the card grows. */
    k->detail = label(t, lines, DETAILS[i], 13.0f, 400.0f, COLOR_DIM);
    ax_txn_set_u32(t, k->detail, AX_PROP_DISPLAY, AX_DISPLAY_NONE);
    (void)label(t, lines, "Updated by a transaction \xC2\xB7 animated by the scene", 11.0f, 400.0f,
                COLOR_DIM);
    (void)letter;

    /* A progress bar along the bottom edge, rounded off by the card's clip. */
    k->progress = box(t, k->card, COLOR_LINE, 0.0f);
    ax_txn_set_u32(t, k->progress, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_dimension(t, k->progress, AX_PROP_LEFT, pt(0));
    ax_txn_set_dimension(t, k->progress, AX_PROP_BOTTOM, pt(0));
    ax_txn_set_dimension(t, k->progress, AX_PROP_HEIGHT, pt(4));
    ax_txn_set_dimension(t, k->progress, AX_PROP_WIDTH, pct(20.0f + 13.0f * (float)i));
    ax_txn_set_u32(t, k->progress, AX_PROP_ROLE, AX_ROLE_PROGRESS);
    ax_txn_set_str(t, k->progress, AX_PROP_LABEL, "Progress", 8);
    ax_txn_set_f32(t, k->progress, AX_PROP_VALUE, 20.0f + 13.0f * (float)i);
    ax_txn_set_f32(t, k->progress, AX_PROP_VALUE_MIN, 0.0f);
    ax_txn_set_f32(t, k->progress, AX_PROP_VALUE_MAX, 100.0f);
    c->order[i] = k->card;
}

/* Grows or shrinks a card: its detail paragraph in or out of view. */
static void toggle(ax_txn *t, m1_card *card)
{
    card->grown = !card->grown;
    ax_txn_set_u32(t, card->detail, AX_PROP_DISPLAY, card->grown ? AX_DISPLAY_FLEX : AX_DISPLAY_NONE);
    ax_txn_set_u32(t, card->card, AX_PROP_EXPANDED,
                   card->grown ? AX_EXPANDED_EXPANDED : AX_EXPANDED_COLLAPSED);
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
    ax_txn_set_color(t, root, AX_PROP_COLOR, m1_c8(COLOR_LINE)); /* inherited by every text */
    ax_txn_set_str(t, root, AX_PROP_LABEL, "m1_nodes", 8);
    if (csd) {
        ax_node caption = box(t, root, COLOR_CAPTION, 0.0f);
        ax_txn_set_dimension(t, caption, AX_PROP_HEIGHT, pt(M1_CAPTION_HEIGHT));
        ax_txn_set_f32(t, caption, AX_PROP_FLEX_SHRINK, 0.0f);
    }
    c->list = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, c->list, AX_PROP_FLEX_GROW, 1.0f);
    /* The window's height, whatever its content (Yoga's default shrink is 0): the cards change
     * size inside it, so they animate their own frames. */
    ax_txn_set_f32(t, c->list, AX_PROP_FLEX_SHRINK, 1.0f);
    ax_txn_set_dimension(t, c->list, AX_PROP_MIN_HEIGHT, pt(0));
    ax_txn_set_dimension(t, c->list, AX_PROP_PADDING, pt(20));
    ax_txn_set_dimension(t, c->list, AX_PROP_GAP, pt(12));
    {
        /* The heading: the largest and the smallest text sizes of the page. */
        ax_node head = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_set_f32(t, head, AX_PROP_FLEX_SHRINK, 0.0f);
        ax_txn_append_child(t, c->list, head);
        ax_node title = label(t, head, "AXION text", 48.0f, 700.0f, 0);
        ax_txn_set_u32(t, title, AX_PROP_ROLE, AX_ROLE_HEADING);
        ax_txn_set_u32(t, title, AX_PROP_LEVEL, 1);
        (void)label(t, head,
                    "Noto Sans, shaped by HarfBuzz, from 11 to 48 px. Every change below is one "
                    "transaction; the scene animates it.",
                    12.0f, 400.0f, COLOR_DIM);
    }
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
    switch (k % 4u) {
    case 0: { /* the last card to the top: every card moves to its new place */
        ax_node last = c->order[M1_CARDS - 1];
        ax_txn_insert_before(t, c->list, last, c->order[0]);
        for (i = M1_CARDS - 1; i > 0; --i) {
            c->order[i] = c->order[i - 1];
        }
        c->order[0] = last;
        break;
    }
    case 1: { /* a card grows (or shrinks back): its detail paragraph comes into view as the
               * card's height animates */
        m1_card *card = &c->cards[(k / 4u) % M1_CARDS];
        toggle(t, card);
        break;
    }
    case 3: /* the list narrows (or widens back): every paragraph re-wraps as the cards' width
             * animates, without shaping again */
        c->narrow = !c->narrow;
        ax_txn_set_dimension(t, c->list, AX_PROP_PADDING_RIGHT, c->narrow ? pct(35.0f) : pt(20.0f));
        break;
    default: { /* a card takes the next color */
        m1_card *card = &c->cards[(k / 4u + 2u) % M1_CARDS];
        card->color = (card->color + 1u) % PALETTE_N;
        ax_txn_set_color(t, card->card, AX_PROP_BACKGROUND_COLOR, m1_c8(PALETTE[card->color]));
        break;
    }
    }
    return ax_txn_commit(t, out_seq);
}

ax_result m1_cards_toggle(ax_world world, m1_cards *c, ax_node card, uint64_t *out_seq)
{
    ax_txn *t;
    uint32_t i;
    for (i = 0; i < M1_CARDS && c->cards[i].card.h != card.h; ++i) {
    }
    if (i == M1_CARDS) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    t = ax_txn_begin(world);
    if (t == NULL) {
        return ax_last_error()->code;
    }
    toggle(t, &c->cards[i]);
    return ax_txn_commit(t, out_seq);
}
