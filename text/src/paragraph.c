/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Paragraphs: itemization, breaks and shaping (decision 99).
 */
#include <string.h>

#include "text_internal.h"

/* A shaped run in the cache. */
typedef struct tx_shaped {
    tx_entry entry;
} tx_shaped;

bool tx_shape_equal(const tx_entry *e, const void *key);
void tx_shape_destroy(tx_entry *e, void *user);

bool tx_shape_equal(const tx_entry *e, const void *key)
{
    (void)e;
    (void)key;
    return false;
}

void tx_shape_destroy(tx_entry *e, void *user)
{
    ax_text_system *ts = (ax_text_system *)user;
    ax_mem_free(&ts->allocator, e, e->bytes);
}

/* ---- Features -------------------------------------------------------------------------------- */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

uint32_t ax_text_parse_features(ax_str css, ax_text_feature *out, uint32_t max,
                                ax_result *out_result)
{
    size_t i = 0;
    uint32_t n = 0;
    if (out_result != NULL) {
        *out_result = AX_OK;
    }
    for (;;) {
        char tag[4];
        uint32_t value = 1;
        size_t k;
        char quote = 0;
        while (i < css.len && is_space(css.ptr[i])) {
            i++;
        }
        if (i == css.len) {
            if (n > 0 && css.len > 0 && css.ptr[css.len - 1] == ',') {
                goto bad; /* a trailing comma */
            }
            return n;
        }
        if (css.ptr[i] == '"' || css.ptr[i] == '\'') {
            quote = css.ptr[i++];
        }
        for (k = 0; k < 4; ++k) {
            char c = i < css.len ? css.ptr[i] : '\0';
            if (c < 0x20 || c > 0x7E || c == '"' || c == '\'' || c == ',' || (!quote && is_space(c))) {
                goto bad;
            }
            tag[k] = c;
            i++;
        }
        if (quote) {
            if (i >= css.len || css.ptr[i] != quote) {
                goto bad;
            }
            i++;
        } else if (i < css.len && !is_space(css.ptr[i]) && css.ptr[i] != ',') {
            goto bad; /* a tag longer than four characters */
        }
        while (i < css.len && is_space(css.ptr[i])) {
            i++;
        }
        if (i < css.len && css.ptr[i] != ',') {
            if (css.len - i >= 2 && memcmp(css.ptr + i, "on", 2) == 0) {
                value = 1;
                i += 2;
            } else if (css.len - i >= 3 && memcmp(css.ptr + i, "off", 3) == 0) {
                value = 0;
                i += 3;
            } else if (css.ptr[i] >= '0' && css.ptr[i] <= '9') {
                value = 0;
                while (i < css.len && css.ptr[i] >= '0' && css.ptr[i] <= '9') {
                    if (value > 65535u) {
                        goto bad;
                    }
                    value = value * 10u + (uint32_t)(css.ptr[i] - '0');
                    i++;
                }
            } else {
                goto bad;
            }
            while (i < css.len && is_space(css.ptr[i])) {
                i++;
            }
        }
        if (n == max) {
            goto bad;
        }
        if (out != NULL) {
            out[n].tag = ((uint32_t)(unsigned char)tag[0] << 24) |
                         ((uint32_t)(unsigned char)tag[1] << 16) |
                         ((uint32_t)(unsigned char)tag[2] << 8) | (uint32_t)(unsigned char)tag[3];
            out[n].value = value;
        }
        n++;
        if (i < css.len) {
            if (css.ptr[i] != ',') {
                goto bad;
            }
            i++;
        }
    }
bad:
    if (out_result != NULL) {
        *out_result = AX_ERR_INVALID_ARGUMENT;
    }
    return 0;
}

/* ---- Paragraphs (filled in by the pipeline) -------------------------------------------------- */

struct ax_text_paragraph {
    ax_atomic_u32 refs;
    ax_text_system *ts;
};

void ax_text_paragraph_retain(ax_text_paragraph *p)
{
    (void)ax_atomic_fetch_add_u32(&p->refs, 1u, AX_MO_RELAXED);
}

void ax_text_paragraph_release(ax_text_paragraph *p)
{
    if (p != NULL && ax_atomic_fetch_add_u32(&p->refs, UINT32_MAX, AX_MO_ACQ_REL) == 1u) {
        ax_mem_free(&p->ts->allocator, p, sizeof *p);
    }
}
