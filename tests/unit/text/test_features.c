/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * CSS font-feature-settings, as FONT_FEATURES takes them (decision 99).
 */
#include <string.h>

#include <axion/text/ax_text.h>

#include "ax_test.h"

#define TAG(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t parse(const char *s, ax_text_feature *out, ax_result *r)
{
    ax_str v;
    v.ptr = s;
    v.len = strlen(s);
    return ax_text_parse_features(v, out, AX_TEXT_MAX_FEATURES, r);
}

AX_TEST(css_feature_lists_parse)
{
    ax_text_feature f[AX_TEXT_MAX_FEATURES];
    ax_result r;
    AX_CHECK_EQ_U(parse("", f, &r), 0u);
    AX_CHECK_RESULT(r, AX_OK);
    AX_CHECK_EQ_U(parse("\"liga\" 0, tnum, 'smcp' on, \"salt\" 2, zero off", f, &r), 5u);
    AX_CHECK_RESULT(r, AX_OK);
    AX_CHECK_EQ_U(f[0].tag, TAG('l', 'i', 'g', 'a'));
    AX_CHECK_EQ_U(f[0].value, 0u);
    AX_CHECK_EQ_U(f[1].tag, TAG('t', 'n', 'u', 'm'));
    AX_CHECK_EQ_U(f[1].value, 1u);
    AX_CHECK_EQ_U(f[2].tag, TAG('s', 'm', 'c', 'p'));
    AX_CHECK_EQ_U(f[2].value, 1u);
    AX_CHECK_EQ_U(f[3].value, 2u);
    AX_CHECK_EQ_U(f[4].value, 0u);
}

AX_TEST(malformed_feature_lists_are_rejected)
{
    static const char *const bad[] = {
        "lig",        /* three characters */
        "ligatures",  /* more than four */
        "\"liga",     /* unclosed quote */
        "liga maybe", /* not a value */
        "liga 0 1",   /* two values */
        "liga,",      /* trailing comma */
        ", liga",     /* empty item */
        "li ga",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        ax_result r = AX_OK;
        AX_CHECK_EQ_U(parse(bad[i], NULL, &r), 0u);
        AX_CHECK_RESULT(r, AX_ERR_INVALID_ARGUMENT);
    }
    {
        /* More than the limit. */
        char many[256];
        size_t at = 0;
        ax_result r = AX_OK;
        for (i = 0; i <= AX_TEXT_MAX_FEATURES; ++i) {
            const char *item = i == 0 ? "liga" : ", liga";
            memcpy(many + at, item, strlen(item));
            at += strlen(item);
        }
        many[at] = '\0';
        AX_CHECK_EQ_U(parse(many, NULL, &r), 0u);
        AX_CHECK_RESULT(r, AX_ERR_INVALID_ARGUMENT);
    }
}
