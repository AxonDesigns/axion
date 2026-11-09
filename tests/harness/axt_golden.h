/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Golden images (decision 82). A golden is a PNG in the repository,
 * tests/goldens/<suite>/<name>.png, compared with what a test drew:
 *   - exactly by default (the software renderer is deterministic), or within a per-channel
 *     tolerance;
 *   - AX_UPDATE_GOLDENS=1 writes the golden instead of comparing. Only on purpose, locally:
 *     tools/update_goldens.sh, or the update_goldens build target. A test refuses to update when
 *     the CI variable is set, so CI never rewrites goldens;
 *   - on a mismatch, <name>.actual.png, .expected.png and .diff.png go to AX_TEST_FAILURE_DIR
 *     (default: golden-failures/ in the test's working directory), which CI uploads;
 *   - with AX_TEST_IMAGE_DIR set, every image checked is also written there as
 *     <suite>__<name>.png, for CI to compare platforms byte for byte.
 * Images are premultiplied RGBA8, converted to PNG's straight alpha before anything is compared,
 * so a golden always holds exactly what the comparison saw.
 *
 * Text goldens (decision 108: the semantic tree as text) work the same way with
 * tests/goldens/<suite>/<name>.txt, compared byte for byte; a mismatch writes <name>.actual.txt,
 * and AX_TEST_IMAGE_DIR gets <suite>__<name>.txt for the determinism job.
 */
#ifndef AXT_GOLDEN_H
#define AXT_GOLDEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool axt_golden_check(const char *file, int line, const char *suite, const char *name,
                      const uint8_t *rgba, uint32_t width, uint32_t height, size_t stride,
                      uint32_t tolerance);

bool axt_golden_check_text(const char *file, int line, const char *suite, const char *name,
                           const char *text, size_t length);

#define AXT_CHECK_GOLDEN_TEXT(suite, name, text, length) \
    axt_golden_check_text(__FILE__, __LINE__, (suite), (name), (text), (length))

#define AXT_CHECK_GOLDEN(suite, name, rgba, width, height, stride) \
    axt_golden_check(__FILE__, __LINE__, (suite), (name), (rgba), (width), (height), (stride), 0)

#endif
