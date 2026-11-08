/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flex conformance against Yoga (decision 91): the fixtures of Yoga's generated tests
 * (yoga_fixtures.inc, ported by tools/yoga/port_fixtures.py; MIT, see NOTICE), laid out left to
 * right with ax_layout_tree at pixel scale 1, every node's frame compared with Yoga's expectation.
 *
 * Each fixture is one of:
 *   - skipped upstream (GTEST_SKIP in Yoga itself);
 *   - unsupported: it uses a feature outside M1's scope (baseline alignment, display: contents,
 *     overflow: scroll); reported, not required;
 *   - an accepted difference: listed below with its reason, and required to still differ, so the
 *     list never goes stale;
 *   - required to pass.
 * Run the executable to print the report (counts per file and every difference).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/core/ax_layout.h"
#include "axion/core/ax_scene.h"

enum { YFX_DIM = 1, YFX_F32, YFX_U32, YFX_INSERT, YFX_MEASURE };

#define YFX_FEATURE_BASELINE         0x1u
#define YFX_FEATURE_DISPLAY_CONTENTS 0x2u
#define YFX_FEATURE_MEASURE          0x4u
#define YFX_FEATURE_OVERFLOW_SCROLL  0x8u
#define YFX_FEATURE_START_END        0x10u
#define YFX_UNSUPPORTED \
    (YFX_FEATURE_BASELINE | YFX_FEATURE_DISPLAY_CONTENTS | YFX_FEATURE_OVERFLOW_SCROLL)

typedef struct yfx_op {
    uint8_t kind;
    uint8_t node;
    uint32_t prop; /* or the child, for YFX_INSERT */
    float value;
    uint32_t arg; /* unit, enumeration value, or text index */
} yfx_op;

typedef struct yfx_frame {
    float left, top, width, height;
} yfx_frame;

typedef struct yfx_test {
    const char *file;
    const char *name;
    int skipped;
    float owner_w, owner_h;
    uint32_t op_start, op_count;
    uint32_t expect_start, node_count;
    uint32_t features;
} yfx_test;

#include "yoga_fixtures.inc"

/* Fixtures that differ from Yoga on purpose, with the reason (decision 91). */
typedef struct accepted {
    const char *name;
    const char *reason;
} accepted;

static const accepted ACCEPTED[] = {
    {NULL, NULL},
};

static const char *accepted_reason(const char *name)
{
    size_t i;
    for (i = 0; ACCEPTED[i].name != NULL; ++i) {
        if (strcmp(ACCEPTED[i].name, name) == 0) {
            return ACCEPTED[i].reason;
        }
    }
    return NULL;
}

/* ---- Yoga's IntrinsicSizeMeasure (tests/util/TestUtil.cpp), ported ----------------------------
 */

static float longest_word_width(const char *text, float per_char)
{
    int max_len = 0, cur = 0;
    for (; *text != '\0'; ++text) {
        if (*text == ' ') {
            max_len = cur > max_len ? cur : max_len;
            cur = 0;
        } else {
            cur++;
        }
    }
    return (float)(cur > max_len ? cur : max_len) * per_char;
}

static float text_height(const char *text, float width, float per_char, float line_h)
{
    float lines = 1.0f;
    float line_len = 0.0f;
    const char *word = text;
    if ((float)strlen(text) * per_char <= width) {
        return line_h;
    }
    for (;;) {
        const char *end = strchr(word, ' ');
        size_t len = end != NULL ? (size_t)(end - word) : strlen(word);
        float ww = (float)len * per_char;
        if (ww > width) {
            if (line_len > 0.0f) {
                lines += 1.0f;
            }
            lines += 1.0f;
            line_len = 0.0f;
        } else if (line_len + ww <= width) {
            line_len += ww + per_char;
        } else {
            lines += 1.0f;
            line_len = ww + per_char;
        }
        if (end == NULL) {
            break;
        }
        word = end + 1;
    }
    return (line_len == 0.0f ? lines - 1.0f : lines) * line_h;
}

static void intrinsic_measure(void *user, ax_node n, float width, ax_layout_mode wm, float height,
                              ax_layout_mode hm, float *out_w, float *out_h)
{
    const char *text = (const char *)user;
    float len = (float)strlen(text) * 10.0f;
    float w, h;
    (void)n;
    if (wm == AX_LAYOUT_EXACT) {
        w = width;
    } else if (wm == AX_LAYOUT_AT_MOST) {
        w = len < width ? len : width;
    } else {
        w = len;
    }
    if (hm == AX_LAYOUT_EXACT) {
        h = height;
    } else {
        float lw = longest_word_width(text, 10.0f);
        float th = text_height(text, lw > w ? lw : w, 10.0f, 10.0f);
        h = hm == AX_LAYOUT_AT_MOST ? (th < height ? th : height) : th;
    }
    *out_w = w;
    *out_h = h;
}

/* ---- Running a fixture -------------------------------------------------------------------------
 */

static bool close_to(float actual, float expected)
{
    if (expected != expected) {
        return actual != actual;
    }
    return fabsf(actual - expected) <= 1e-3f * (1.0f + fabsf(expected));
}

/* Lays the fixture out; returns the number of nodes whose frame differs, and describes the first.
 */
static uint32_t run_fixture(const yfx_test *t, char *why, size_t why_size)
{
    ax_scene scene;
    ax_node_store_desc desc;
    ax_node nodes[256];
    uint32_t i, bad = 0;
    memset(&desc, 0, sizeof desc);
    ax_scene_init(&scene, &desc);
    why[0] = '\0';
    for (i = 0; i < t->node_count && i < 256u; ++i) {
        (void)ax_scene_create(&scene, AX_NODE_VIEW, &nodes[i]);
    }
    for (i = 0; i < t->op_count; ++i) {
        const yfx_op *op = &YFX_OPS[t->op_start + i];
        ax_value v;
        ax_result r = AX_OK;
        memset(&v, 0, sizeof v);
        switch (op->kind) {
        case YFX_DIM:
            v.dimension.value = op->value;
            v.dimension.unit = op->arg;
            r = ax_scene_set(&scene, nodes[op->node], op->prop, AX_VALUE_DIMENSION, &v);
            break;
        case YFX_F32:
            v.f32 = op->value;
            r = ax_scene_set(&scene, nodes[op->node], op->prop, AX_VALUE_F32, &v);
            break;
        case YFX_U32:
            v.u32 = op->arg;
            r = ax_scene_set(&scene, nodes[op->node], op->prop, AX_VALUE_U32, &v);
            break;
        case YFX_INSERT:
            r = ax_scene_insert_before(&scene, nodes[op->node], nodes[op->prop],
                                       (ax_node)AX_HANDLE_NULL_INIT);
            break;
        case YFX_MEASURE:
            r = ax_layout_set_measure(&scene, nodes[op->node], intrinsic_measure,
                                      (void *)YFX_TEXTS[op->arg]);
            break;
        default:
            break;
        }
        if (r != AX_OK && why[0] == '\0') {
            /* A value AXION rejects (a negative padding...): the fixture cannot be expressed. */
            (void)snprintf(why, why_size, "op %u rejected (%d)", (unsigned)i, (int)r);
            bad++;
        }
    }
    ax_layout_tree(&scene, nodes[0], t->owner_w, t->owner_h, 1.0f);
    for (i = 0; i < t->node_count; ++i) {
        const yfx_frame *e = &YFX_EXPECT[t->expect_start + i];
        ax_rect f = scene.store.layout[ax_node_store_row(&scene.store, nodes[i])].frame;
        if (!close_to(f.x, e->left) || !close_to(f.y, e->top) || !close_to(f.w, e->width) ||
            !close_to(f.h, e->height)) {
            if (bad == 0 || why[0] == '\0') {
                (void)snprintf(why, why_size, "node %u: got (%g %g %g %g), Yoga (%g %g %g %g)",
                               (unsigned)i, (double)f.x, (double)f.y, (double)f.w, (double)f.h,
                               (double)e->left, (double)e->top, (double)e->width,
                               (double)e->height);
            }
            bad++;
        }
    }
    ax_scene_deinit(&scene);
    return bad;
}

AX_TEST(flex_layout_matches_yoga_on_its_conformance_fixtures)
{
    uint32_t n = (uint32_t)AX_COUNTOF(YFX_TESTS);
    uint32_t i;
    uint32_t passed = 0, failed = 0, skipped = 0, unsupported = 0, unsupported_passing = 0;
    uint32_t accepted_count = 0, stale_accepted = 0;
    const char *file = NULL;
    uint32_t file_pass = 0, file_total = 0;
    char why[256];

    printf("Yoga conformance (%u fixtures)\n", (unsigned)n);
    for (i = 0; i <= n; ++i) {
        const yfx_test *t = i < n ? &YFX_TESTS[i] : NULL;
        const char *reason;
        uint32_t bad;
        if (file != NULL && (t == NULL || strcmp(t->file, file) != 0)) {
            printf("  %-28s %3u / %3u\n", file, (unsigned)file_pass, (unsigned)file_total);
            file_pass = file_total = 0;
        }
        if (t == NULL) {
            break;
        }
        file = t->file;
        if (t->skipped) {
            skipped++;
            continue;
        }
        file_total++;
        bad = run_fixture(t, why, sizeof why);
        reason = accepted_reason(t->name);
        if (t->features & YFX_UNSUPPORTED) {
            unsupported++;
            if (bad == 0) {
                unsupported_passing++;
                file_pass++;
            }
            continue;
        }
        if (reason != NULL) {
            accepted_count++;
            if (bad == 0) {
                stale_accepted++;
                printf("    accepted difference now passes: %s\n", t->name);
            }
            continue;
        }
        if (bad == 0) {
            passed++;
            file_pass++;
        } else {
            failed++;
            printf("    FAIL %s: %s\n", t->name, why);
        }
    }
    printf("passed %u, failed %u, accepted differences %u, unsupported %u (%u of them pass), "
           "skipped upstream %u\n",
           (unsigned)passed, (unsigned)failed, (unsigned)accepted_count, (unsigned)unsupported,
           (unsigned)unsupported_passing, (unsigned)skipped);
    AX_CHECK_EQ_U(failed, 0);
    AX_CHECK_EQ_U(stale_accepted, 0);
}
