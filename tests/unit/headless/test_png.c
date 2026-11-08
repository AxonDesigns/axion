/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The PNG codec (decision 82): round trips, files from an independent encoder (Python's zlib,
 * with stored, fixed and dynamic Huffman blocks and every filter), the encoder's output pinned
 * byte for byte, real compression, and corrupt input (every truncation, thousands of flipped
 * bytes with their CRCs repaired so inflate sees them, random garbage): an error or a valid image,
 * never a crash or a read out of bounds (ASan in CI).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/headless/ax_png.h"
#include "png_samples.h"

static uint32_t g_seed = 0x12345678u;

static uint32_t next_u32(void)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

static uint8_t *pattern(uint32_t w, uint32_t h, bool translucent)
{
    uint8_t *p = (uint8_t *)malloc((size_t)w * h * 4u);
    uint32_t i;
    for (i = 0; p != NULL && i < w * h; ++i) {
        const uint32_t x = i % w, y = i / w;
        p[i * 4 + 0] = (uint8_t)(x * 3 + y);
        p[i * 4 + 1] = (uint8_t)((x ^ y) * 5);
        p[i * 4 + 2] = (uint8_t)((x / 7 + y / 5) * 40);
        p[i * 4 + 3] = translucent ? (uint8_t)(255 - (x + y) % 256) : 255;
        if (i % 97 == 3) {
            p[i * 4 + 0] = (uint8_t)next_u32(); /* some noise */
        }
    }
    return p;
}

static void round_trip(uint32_t w, uint32_t h, bool translucent)
{
    uint8_t *src = pattern(w, h, translucent), *data = NULL;
    size_t size = 0;
    ax_image img;
    AX_REQUIRE(src != NULL);
    AX_REQUIRE(ax_png_encode(NULL, src, w, h, (size_t)w * 4u, 0, &data, &size) == AX_OK);
    AX_REQUIRE(ax_png_decode(NULL, data, size, &img) == AX_OK);
    AX_CHECK_EQ_U(img.width, w);
    AX_CHECK_EQ_U(img.height, h);
    AX_CHECK(memcmp(img.rgba, src, (size_t)w * h * 4u) == 0);
    ax_image_free(NULL, &img);
    ax_png_free(NULL, data, size);
    free(src);
}

AX_TEST(images_round_trip)
{
    round_trip(1, 1, false);
    round_trip(17, 5, false);
    round_trip(17, 5, true);
    round_trip(300, 200, false);
    round_trip(129, 300, true);
}

static void check_sample(const uint8_t *png, size_t size, const uint8_t *pixels, uint32_t w,
                         uint32_t h)
{
    ax_image img;
    AX_REQUIRE(ax_png_decode(NULL, png, size, &img) == AX_OK);
    AX_CHECK_EQ_U(img.width, w);
    AX_CHECK_EQ_U(img.height, h);
    AX_CHECK(memcmp(img.rgba, pixels, (size_t)w * h * 4u) == 0);
    ax_image_free(NULL, &img);
}

AX_TEST(files_from_another_encoder_decode)
{
    check_sample(k_rgba_png, sizeof k_rgba_png, k_rgba_pixels, 9, 7);
    check_sample(k_grey_png, sizeof k_grey_png, k_grey_pixels, 5, 4);
    check_sample(k_grey_alpha_png, sizeof k_grey_alpha_png, k_grey_alpha_pixels, 5, 4);
    check_sample(k_rgb_png, sizeof k_rgb_png, k_rgb_pixels, 64, 48);
}

AX_TEST(kinds_it_does_not_read_are_unsupported)
{
    ax_image img;
    AX_CHECK_RESULT(ax_png_decode(NULL, k_palette_png, sizeof k_palette_png, &img),
                    AX_ERR_UNSUPPORTED);
    AX_CHECK_RESULT(ax_png_decode(NULL, k_16bit_png, sizeof k_16bit_png, &img), AX_ERR_UNSUPPORTED);
    AX_CHECK_RESULT(ax_png_decode(NULL, k_interlaced_png, sizeof k_interlaced_png, &img),
                    AX_ERR_UNSUPPORTED);
    AX_CHECK_RESULT(ax_png_decode(NULL, "GIF89a", 6, &img), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(img.rgba == NULL);
}

AX_TEST(premultiplied_input_is_stored_with_straight_alpha)
{
    /* 50% red premultiplied is (128, 0, 0, 128): straight (255, 0, 0, 128). Opaque pixels are
     * unchanged, and a transparent one is 0, 0, 0, 0. */
    const uint8_t src[12] = {128, 0, 0, 128, 10, 20, 30, 255, 0, 0, 0, 0};
    uint8_t *data;
    size_t size;
    ax_image img;
    AX_REQUIRE(ax_png_encode(NULL, src, 3, 1, 12, AX_PNG_PREMULTIPLIED, &data, &size) == AX_OK);
    AX_REQUIRE(ax_png_decode(NULL, data, size, &img) == AX_OK);
    AX_CHECK_EQ_U(img.rgba[0], 255);
    AX_CHECK_EQ_U(img.rgba[3], 128);
    AX_CHECK_EQ_U(img.rgba[4], 10);
    AX_CHECK_EQ_U(img.rgba[6], 30);
    AX_CHECK_EQ_U(img.rgba[11], 0);
    ax_image_free(NULL, &img);
    ax_png_free(NULL, data, size);
}

/* The encoder's output depends only on the pixels: the same bytes everywhere. A change of the
 * encoder changes this hash and every golden file's bytes (not their pixels). */
AX_TEST(encoding_is_pinned_byte_for_byte)
{
    uint8_t *src, *data;
    size_t size, i;
    uint32_t h = 2166136261u;
    g_seed = 0x12345678u;
    src = pattern(97, 61, true);
    AX_REQUIRE(src != NULL);
    AX_REQUIRE(ax_png_encode(NULL, src, 97, 61, 97u * 4u, 0, &data, &size) == AX_OK);
    for (i = 0; i < size; ++i) {
        h = (h ^ data[i]) * 16777619u;
    }
    printf("%u bytes, hash %08x\n", (unsigned)size, (unsigned)h);
    AX_CHECK_EQ_U(h, 0x46e4f4a2u);
    ax_png_free(NULL, data, size);
    free(src);
}

AX_TEST(it_really_compresses)
{
    /* A smooth 256 x 256 image: raw RGB is 196 KiB. */
    uint8_t *src = (uint8_t *)malloc(256u * 256u * 4u), *data;
    size_t size;
    uint32_t i;
    AX_REQUIRE(src != NULL);
    for (i = 0; i < 256u * 256u; ++i) {
        src[i * 4 + 0] = (uint8_t)(i % 256);
        src[i * 4 + 1] = (uint8_t)(i / 256);
        src[i * 4 + 2] = 128;
        src[i * 4 + 3] = 255;
    }
    AX_REQUIRE(ax_png_encode(NULL, src, 256, 256, 1024, 0, &data, &size) == AX_OK);
    printf("256 x 256 gradient: %u bytes\n", (unsigned)size);
    AX_CHECK(size < 256u * 256u * 3u / 20u);
    ax_png_free(NULL, data, size);
    free(src);
}

/* ---- corrupt input --------------------------------------------------------------------------- */

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8; ++k) {
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
    }
    return ~c;
}

/* Recomputes every chunk's CRC, so a flipped byte reaches the chunk's parser or inflate. */
static void repair_crcs(uint8_t *p, size_t size)
{
    size_t pos = 8;
    while (pos + 12 <= size) {
        const uint32_t len = ((uint32_t)p[pos] << 24) | ((uint32_t)p[pos + 1] << 16) |
                             ((uint32_t)p[pos + 2] << 8) | p[pos + 3];
        uint32_t crc;
        if (len > size - pos - 12) {
            return;
        }
        crc = crc32_ref(p + pos + 4, 4u + len);
        p[pos + 8 + len] = (uint8_t)(crc >> 24);
        p[pos + 9 + len] = (uint8_t)(crc >> 16);
        p[pos + 10 + len] = (uint8_t)(crc >> 8);
        p[pos + 11 + len] = (uint8_t)crc;
        pos += 12u + len;
    }
}

static void try_decode(const uint8_t *p, size_t size, uint32_t *ok, uint32_t *failed)
{
    /* A copy of exactly `size` bytes, so ASan sees any read past the end. */
    uint8_t *copy = (uint8_t *)malloc(size != 0 ? size : 1);
    ax_image img;
    ax_result r;
    memcpy(copy, p, size);
    r = ax_png_decode(NULL, copy, size, &img);
    if (r == AX_OK) {
        (*ok)++;
        AX_CHECK(img.rgba != NULL);
        ax_image_free(NULL, &img);
    } else {
        (*failed)++;
        AX_CHECK(r == AX_ERR_INVALID_ARGUMENT || r == AX_ERR_UNSUPPORTED);
        AX_CHECK(img.rgba == NULL);
    }
    free(copy);
}

AX_TEST(corrupt_input_fails_cleanly)
{
    const struct {
        const uint8_t *png;
        size_t size;
    } samples[3] = {{k_rgba_png, sizeof k_rgba_png},
                    {k_rgb_png, sizeof k_rgb_png},
                    {k_grey_png, sizeof k_grey_png}};
    uint32_t ok = 0, failed = 0, s, i;
    uint8_t *own = NULL, *buf;
    size_t own_size;
    uint8_t *src = pattern(40, 30, true);
    AX_REQUIRE(ax_png_encode(NULL, src, 40, 30, 160, 0, &own, &own_size) == AX_OK);
    free(src);
    buf = (uint8_t *)malloc(own_size + sizeof k_rgb_png);
    AX_REQUIRE(buf != NULL);
    for (s = 0; s < 4; ++s) {
        const uint8_t *png = s < 3 ? samples[s].png : own;
        const size_t size = s < 3 ? samples[s].size : own_size;
        size_t n;
        for (n = 0; n < size; ++n) {
            try_decode(png, n, &ok, &failed); /* every truncation */
        }
        for (i = 0; i < 1500; ++i) {
            const uint32_t flips = 1u + next_u32() % 3u;
            uint32_t f;
            memcpy(buf, png, size);
            for (f = 0; f < flips; ++f) {
                buf[8u + next_u32() % (uint32_t)(size - 8u)] ^= (uint8_t)(1u << (next_u32() % 8u));
            }
            repair_crcs(buf, size);
            try_decode(buf, size, &ok, &failed);
        }
    }
    for (i = 0; i < 500; ++i) {
        const size_t n = 8u + next_u32() % 300u;
        size_t k;
        memcpy(buf, "\x89PNG\r\n\x1a\n", 8);
        for (k = 8; k < n; ++k) {
            buf[k] = (uint8_t)next_u32();
        }
        try_decode(buf, n, &ok, &failed);
    }
    printf("corrupt inputs: %u decoded anyway, %u rejected\n", ok, failed);
    AX_CHECK(failed > 5000u);
    free(buf);
    ax_png_free(NULL, own, own_size);
}

AX_TEST(files_and_argument_errors)
{
    const uint8_t px[8] = {1, 2, 3, 255, 4, 5, 6, 255};
    ax_image img;
    uint8_t *data;
    size_t size;
    const char *path = "test_headless_png_file.png";
    AX_CHECK_RESULT(ax_png_write_file(path, px, 2, 1, 8, 0), AX_OK);
    AX_REQUIRE(ax_png_read_file(NULL, path, &img) == AX_OK);
    AX_CHECK(memcmp(img.rgba, px, 8) == 0);
    ax_image_free(NULL, &img);
    remove(path);
    AX_CHECK_RESULT(ax_png_read_file(NULL, "no/such/dir/x.png", &img), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_png_encode(NULL, px, 0, 1, 8, 0, &data, &size), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_png_encode(NULL, px, 2, 1, 4, 0, &data, &size), AX_ERR_INVALID_ARGUMENT);
}
