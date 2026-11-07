/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Helpers shared by the Render IR tests.
 */
#ifndef AXT_IR_TEST_UTIL_H
#define AXT_IR_TEST_UTIL_H

#include <string.h>

#include "ax_test.h"
#include "axion/render/ax_render_ir.h"

typedef struct axt_text {
    char buf[8192];
    size_t len;
} axt_text;

static void axt_text_write(void *ctx, const char *s, size_t n)
{
    axt_text *t = (axt_text *)ctx;
    if (t->len + n < sizeof t->buf) {
        memcpy(t->buf + t->len, s, n);
        t->len += n;
        t->buf[t->len] = '\0';
    }
}

static AX_MAYBE_UNUSED void axt_dump(const ax_render_ir *ir, axt_text *out)
{
    out->len = 0;
    out->buf[0] = '\0';
    ax_ir_dump(ir, axt_text_write, out);
}

#define axt_check_dump(ir, expected)                                                       \
    do {                                                                                   \
        static axt_text axt__t;                                                            \
        axt_dump((ir), &axt__t);                                                           \
        if (strcmp(axt__t.buf, (expected)) != 0) {                                         \
            ax_test_fail(__FILE__, __LINE__, "dump differs:\n--- got\n%s--- expected\n%s", \
                         axt__t.buf, (expected));                                          \
        }                                                                                  \
    } while (0)

#endif
