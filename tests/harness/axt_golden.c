/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(_MSC_VER)
#    define _CRT_SECURE_NO_WARNINGS /* getenv */
#endif
#include "axt_golden.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/headless/ax_png.h"

#if defined(_WIN32)
#    include <direct.h>
#    define axt_mkdir(path) _mkdir(path)
#else
#    include <sys/stat.h>
#    define axt_mkdir(path) mkdir((path), 0777)
#endif

#ifndef AX_TEST_GOLDEN_DIR
#    error "AX_TEST_GOLDEN_DIR must name tests/goldens"
#endif

static bool env_set(const char *name)
{
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0';
}

/* Grey from the expected image where both agree; red, brighter for bigger differences, where
 * they do not. */
static uint8_t *diff_image(const uint8_t *got, const uint8_t *want, uint32_t n)
{
    uint8_t *d = (uint8_t *)malloc((size_t)n * 4u);
    uint32_t i;
    for (i = 0; d != NULL && i < n; ++i) {
        int k, worst = 0;
        for (k = 0; k < 4; ++k) {
            const int e = abs((int)got[i * 4 + k] - (int)want[i * 4 + k]);
            worst = e > worst ? e : worst;
        }
        if (worst == 0) {
            const uint8_t g =
                (uint8_t)((want[i * 4] * 3u + want[i * 4 + 1] * 6u + want[i * 4 + 2]) /
                          30u); /* a third of the luma */
            d[i * 4 + 0] = d[i * 4 + 1] = d[i * 4 + 2] = g;
        } else {
            d[i * 4 + 0] = (uint8_t)(worst * 4 > 127 ? 255 : 128 + worst * 4);
            d[i * 4 + 1] = 0;
            d[i * 4 + 2] = 0;
        }
        d[i * 4 + 3] = 255;
    }
    return d;
}

bool axt_golden_check(const char *file, int line, const char *suite, const char *name,
                      const uint8_t *rgba, uint32_t width, uint32_t height, size_t stride,
                      uint32_t tolerance)
{
    char path[1024], out[1024];
    const char *fail_dir = getenv("AX_TEST_FAILURE_DIR");
    uint8_t *got = (uint8_t *)malloc((size_t)width * height * 4u);
    ax_image want;
    uint32_t y, i, differ = 0, worst = 0;
    bool ok = true;

    if (got == NULL) {
        ax_test_fail(file, line, "golden %s/%s: out of memory", suite, name);
        return false;
    }
    for (y = 0; y < height; ++y) {
        memcpy(got + (size_t)y * width * 4u, rgba + (size_t)y * stride, (size_t)width * 4u);
    }
    ax_rgba_unpremultiply(got, width, height, (size_t)width * 4u);
    snprintf(path, sizeof path, "%s/%s/%s.png", AX_TEST_GOLDEN_DIR, suite, name);

    if (env_set("AX_TEST_IMAGE_DIR")) {
        snprintf(out, sizeof out, "%s/%s__%s.png", getenv("AX_TEST_IMAGE_DIR"), suite, name);
        if (ax_png_write_file(out, got, width, height, (size_t)width * 4u, 0) != AX_OK) {
            ax_test_fail(file, line, "cannot write %s", out);
        }
    }

    if (env_set("AX_UPDATE_GOLDENS")) {
        if (env_set("CI")) {
            ax_test_fail(file, line,
                         "golden %s/%s: AX_UPDATE_GOLDENS is refused in CI; goldens are updated "
                         "only on purpose, locally (tools/update_goldens.sh)",
                         suite, name);
            free(got);
            return false;
        }
        (void)axt_mkdir(AX_TEST_GOLDEN_DIR);
        snprintf(out, sizeof out, "%s/%s", AX_TEST_GOLDEN_DIR, suite);
        (void)axt_mkdir(out);
        if (ax_png_write_file(path, got, width, height, (size_t)width * 4u, 0) != AX_OK) {
            ax_test_fail(file, line, "cannot write the golden %s", path);
            ok = false;
        } else {
            printf("golden updated: %s\n", path);
        }
        free(got);
        return ok;
    }

    if (ax_png_read_file(NULL, path, &want) != AX_OK) {
        ax_test_fail(file, line,
                     "golden %s/%s: cannot read %s; create it with tools/update_goldens.sh", suite,
                     name, path);
        free(got);
        return false;
    }
    if (want.width != width || want.height != height) {
        ax_test_fail(file, line, "golden %s/%s: %ux%u, expected %ux%u", suite, name, width, height,
                     want.width, want.height);
        ok = false;
    } else {
        for (i = 0; i < width * height; ++i) {
            uint32_t k, e = 0;
            for (k = 0; k < 4; ++k) {
                const uint32_t d = (uint32_t)abs((int)got[i * 4 + k] - (int)want.rgba[i * 4 + k]);
                e = d > e ? d : e;
            }
            worst = e > worst ? e : worst;
            differ += e > tolerance ? 1u : 0u;
        }
        ok = differ == 0;
    }
    if (!ok) {
        const char *dir = fail_dir != NULL && fail_dir[0] != '\0' ? fail_dir : "golden-failures";
        (void)axt_mkdir(dir);
        snprintf(out, sizeof out, "%s/%s__%s.actual.png", dir, suite, name);
        (void)ax_png_write_file(out, got, width, height, (size_t)width * 4u, 0);
        if (want.width == width && want.height == height) {
            uint8_t *d = diff_image(got, want.rgba, width * height);
            snprintf(out, sizeof out, "%s/%s__%s.expected.png", dir, suite, name);
            (void)ax_png_write_file(out, want.rgba, width, height, (size_t)width * 4u, 0);
            snprintf(out, sizeof out, "%s/%s__%s.diff.png", dir, suite, name);
            if (d != NULL) {
                (void)ax_png_write_file(out, d, width, height, (size_t)width * 4u, 0);
                free(d);
            }
            ax_test_fail(file, line,
                         "golden %s/%s: %u pixels differ by more than %u (worst %u/255); images "
                         "in %s/%s__%s.*.png",
                         suite, name, differ, tolerance, worst, dir, suite, name);
        }
    }
    ax_image_free(NULL, &want);
    free(got);
    return ok;
}
