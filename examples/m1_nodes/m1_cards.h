/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m1_nodes' scene: a list of cards in flex that reorder, grow and change color through
 * transactions, every change animated implicitly by the scene (decisions 90 and 94). Shared by the
 * example and by the headless goldens of animation frames (tests/unit/headless).
 */
#ifndef M1_CARDS_H
#define M1_CARDS_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_txn.h"

#define M1_CARDS          6
#define M1_CAPTION_HEIGHT 32.0f
#define M1_BORDER         8.0f
#define M1_CORNER         16.0f

typedef struct m1_card {
    ax_node card; /* a row: avatar, two lines, a progress bar clipped by the card's corners */
    ax_node avatar;
    ax_node detail; /* shown when the card grows */
    ax_node progress;
    bool grown;
    uint32_t color;
} m1_card;

typedef struct m1_cards {
    bool csd; /* client-side decorations: a caption bar and a border */
    ax_node root, list;
    m1_card cards[M1_CARDS];
    ax_node order[M1_CARDS]; /* the cards' current order in the list */
} m1_cards;

/* The sRGB color 0xAARRGGBB as a node color. */
ax_rgba m1_c8(uint32_t argb);

/* Builds the scene under root in one transaction, not animated (a window's first content). App
 * thread. Returns the transaction's commit result. */
ax_result m1_cards_build(ax_world world, ax_node root, m1_cards *c, bool csd, uint64_t *out_seq);

/* The k-th change, deterministic: k % 3 == 0 moves the last card to the top, 1 grows or shrinks a
 * card, 2 gives a card the next color. One animated transaction. App thread. */
ax_result m1_cards_step(ax_world world, m1_cards *c, uint32_t k, uint64_t *out_seq);

#endif /* M1_CARDS_H */
