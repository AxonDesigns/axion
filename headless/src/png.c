/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * PNG encoding and decoding (decision 82), without dependencies.
 *
 * Encoder: per row, the filter whose output has the smallest sum of absolute values (the usual
 * heuristic; ties go to the lower filter type), then one deflate block with fixed Huffman codes
 * over greedy LZ77 matches (hash chains of at most 64 links, window 32 KiB). Everything depends
 * only on the pixels, so the same image gives the same file on every platform.
 *
 * Decoder: inflate after Mark Adler's puff (zlib's contrib/puff, the reference decoder), with every
 * read and write checked against its buffer, the PNG chunks' CRCs, the zlib header and Adler-32,
 * and the image data's exact length.
 */
#if defined(_MSC_VER)
#    define _CRT_SECURE_NO_WARNINGS /* fopen: the portable C function, on purpose */
#endif

#include "axion/headless/ax_png.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"

/* ---- checksums ------------------------------------------------------------------------------- */

/* CRC-32 (PNG's, polynomial 0xEDB88320), four bits at a time from a constant table: no lazy
 * initialization to race on. */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static const uint32_t table[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
                                       0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                       0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                       0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    size_t i;
    crc = ~crc;
    for (i = 0; i < n; ++i) {
        crc ^= p[i];
        crc = table[crc & 15u] ^ (crc >> 4);
        crc = table[crc & 15u] ^ (crc >> 4);
    }
    return ~crc;
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/* ---- a growing byte buffer ------------------------------------------------------------------- */

typedef struct buf {
    const ax_allocator *a;
    uint8_t *p;
    size_t len, cap;
    bool oom;
} buf;

static bool buf_reserve(buf *b, size_t extra)
{
    size_t cap;
    uint8_t *p;
    if (b->oom) {
        return false;
    }
    if (b->len + extra <= b->cap) {
        return true;
    }
    cap = b->cap != 0 ? b->cap : 4096;
    while (cap < b->len + extra) {
        cap *= 2;
    }
    p = (uint8_t *)ax_mem_alloc(b->a, cap, 1);
    if (p == NULL) {
        b->oom = true;
        return false;
    }
    if (b->len != 0) {
        memcpy(p, b->p, b->len);
    }
    if (b->p != NULL) {
        ax_mem_free(b->a, b->p, b->cap);
    }
    b->p = p;
    b->cap = cap;
    return true;
}

static void buf_put(buf *b, const void *data, size_t n)
{
    if (n != 0 && buf_reserve(b, n)) {
        memcpy(b->p + b->len, data, n);
        b->len += n;
    }
}

static void buf_byte(buf *b, uint8_t v)
{
    buf_put(b, &v, 1);
}

static void buf_be32(buf *b, uint32_t v)
{
    const uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    buf_put(b, x, 4);
}

static void buf_free(buf *b)
{
    if (b->p != NULL) {
        ax_mem_free(b->a, b->p, b->cap);
    }
    b->p = NULL;
    b->len = b->cap = 0;
}

/* ---- deflate: LZ77 with fixed Huffman codes -------------------------------------------------- */

static const uint16_t k_len_base[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                        15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                        67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t k_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t k_dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t k_dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

typedef struct bitw {
    buf *b;
    uint32_t acc, n;
} bitw;

static void put_bits(bitw *w, uint32_t v, uint32_t n)
{
    w->acc |= v << w->n;
    w->n += n;
    while (w->n >= 8) {
        buf_byte(w->b, (uint8_t)w->acc);
        w->acc >>= 8;
        w->n -= 8;
    }
}

/* Huffman codes go most significant bit first. */
static void put_code(bitw *w, uint32_t code, uint32_t len)
{
    uint32_t r = 0, i;
    for (i = 0; i < len; ++i) {
        r = (r << 1) | ((code >> i) & 1u);
    }
    put_bits(w, r, len);
}

static void put_literal(bitw *w, uint32_t v)
{
    if (v < 144) {
        put_code(w, 0x30u + v, 8);
    } else if (v < 256) {
        put_code(w, 0x190u + (v - 144), 9);
    } else if (v < 280) {
        put_code(w, v - 256, 7);
    } else {
        put_code(w, 0xC0u + (v - 280), 8);
    }
}

static void put_match(bitw *w, uint32_t len, uint32_t dist)
{
    uint32_t i = 28, d = 29;
    while (k_len_base[i] > len) {
        i--;
    }
    put_literal(w, 257 + i);
    put_bits(w, len - k_len_base[i], k_len_extra[i]);
    while (k_dist_base[d] > dist) {
        d--;
    }
    put_code(w, d, 5);
    put_bits(w, dist - k_dist_base[d], k_dist_extra[d]);
}

#define HASH_BITS 16u
#define WINDOW    32768u
#define MAX_CHAIN 64u
#define MAX_MATCH 258u

static uint32_t hash3(const uint8_t *p)
{
    return (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) * 2654435761u >>
           (32 - HASH_BITS);
}

/* A zlib stream of `data` into out. */
static bool zlib_deflate(const ax_allocator *a, const uint8_t *data, size_t n, buf *out)
{
    int32_t *head = (int32_t *)ax_mem_alloc(a, sizeof(int32_t) << HASH_BITS, AX_ALIGNOF(int32_t));
    int32_t *prev = (int32_t *)ax_mem_alloc(a, sizeof(int32_t) * WINDOW, AX_ALIGNOF(int32_t));
    bitw w;
    size_t i = 0;
    if (head == NULL || prev == NULL) {
        if (head != NULL) {
            ax_mem_free(a, head, sizeof(int32_t) << HASH_BITS);
        }
        if (prev != NULL) {
            ax_mem_free(a, prev, sizeof(int32_t) * WINDOW);
        }
        return false;
    }
    memset(head, 0xFF, sizeof(int32_t) << HASH_BITS); /* -1: empty */
    buf_byte(out, 0x78);
    buf_byte(out, 0x01);
    w.b = out;
    w.acc = 0;
    w.n = 0;
    put_bits(&w, 1, 1); /* final block */
    put_bits(&w, 1, 2); /* fixed Huffman codes */
    while (i < n) {
        uint32_t best_len = 0, best_dist = 0;
        if (i + 3 <= n) {
            const uint32_t h = hash3(data + i);
            int32_t cand = head[h];
            uint32_t chain = 0;
            const size_t max_len = n - i < MAX_MATCH ? n - i : MAX_MATCH;
            while (cand >= 0 && chain++ < MAX_CHAIN && i - (size_t)cand <= WINDOW) {
                const uint8_t *p = data + cand, *q = data + i;
                uint32_t len = 0;
                while (len < max_len && p[len] == q[len]) {
                    len++;
                }
                if (len > best_len) {
                    best_len = len;
                    best_dist = (uint32_t)(i - (size_t)cand);
                    if (len == max_len) {
                        break;
                    }
                }
                cand = prev[(size_t)cand % WINDOW];
            }
        }
        if (best_len >= 3) {
            uint32_t k;
            put_match(&w, best_len, best_dist);
            for (k = 0; k < best_len; ++k, ++i) {
                if (i + 3 <= n) {
                    const uint32_t h = hash3(data + i);
                    prev[i % WINDOW] = head[h];
                    head[h] = (int32_t)i;
                }
            }
        } else {
            if (i + 3 <= n) {
                const uint32_t h = hash3(data + i);
                prev[i % WINDOW] = head[h];
                head[h] = (int32_t)i;
            }
            put_literal(&w, data[i]);
            i++;
        }
    }
    put_literal(&w, 256);
    if (w.n > 0) {
        buf_byte(out, (uint8_t)w.acc);
    }
    buf_be32(out, adler32(data, n));
    ax_mem_free(a, head, sizeof(int32_t) << HASH_BITS);
    ax_mem_free(a, prev, sizeof(int32_t) * WINDOW);
    return !out->oom;
}

/* ---- filters --------------------------------------------------------------------------------- */

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
    const int p = (int)a + (int)b - (int)c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

static uint8_t predict(int type, const uint8_t *row, const uint8_t *up, size_t x, size_t bpp)
{
    const uint8_t a = x >= bpp ? row[x - bpp] : 0;
    const uint8_t b = up != NULL ? up[x] : 0;
    const uint8_t c = (x >= bpp && up != NULL) ? up[x - bpp] : 0;
    switch (type) {
    case 1:
        return a;
    case 2:
        return b;
    case 3:
        return (uint8_t)(((unsigned)a + b) / 2u);
    case 4:
        return paeth(a, b, c);
    default:
        return 0;
    }
}

void ax_rgba_unpremultiply(uint8_t *rgba, uint32_t width, uint32_t height, size_t stride)
{
    uint32_t x, y;
    for (y = 0; y < height; ++y) {
        uint8_t *p = rgba + (size_t)y * stride;
        for (x = 0; x < width; ++x, p += 4) {
            const uint32_t a = p[3];
            int k;
            if (a == 255) {
                continue;
            }
            for (k = 0; k < 3; ++k) {
                const uint32_t v = a == 0 ? 0 : ((uint32_t)p[k] * 255u + a / 2u) / a;
                p[k] = (uint8_t)(v > 255u ? 255u : v);
            }
        }
    }
}

ax_result ax_png_encode(const ax_allocator *allocator, const uint8_t *rgba, uint32_t width,
                        uint32_t height, size_t stride, uint32_t flags, uint8_t **out,
                        size_t *out_size)
{
    const ax_allocator *a = allocator != NULL ? allocator : ax_default_allocator();
    size_t bpp = 3, row_bytes, raw_size, x, y;
    uint8_t *raw, *line;
    bool opaque = true;
    buf png, z;
    uint8_t ihdr[13];

    *out = NULL;
    *out_size = 0;
    if (rgba == NULL || width == 0 || height == 0 || width > AX_PNG_MAX_SIDE ||
        height > AX_PNG_MAX_SIDE || stride < (size_t)width * 4u) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_encode: invalid image");
    }
    for (y = 0; y < height && opaque; ++y) {
        for (x = 0; x < width; ++x) {
            if (rgba[y * stride + x * 4 + 3] != 255) {
                opaque = false;
                break;
            }
        }
    }
    if (!opaque) {
        bpp = 4;
    }
    row_bytes = (size_t)width * bpp;
    raw_size = (row_bytes + 1u) * height;
    raw = (uint8_t *)ax_mem_alloc(a, raw_size + 2u * row_bytes, 1);
    if (raw == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_encode: out of memory");
    }
    line = raw + raw_size; /* two scratch rows: this one and the previous, unfiltered */
    for (y = 0; y < height; ++y) {
        uint8_t *cur = line + (y % 2) * row_bytes;
        const uint8_t *up = y > 0 ? line + ((y + 1) % 2) * row_bytes : NULL;
        uint8_t *dst = raw + y * (row_bytes + 1u);
        uint64_t best_sum = UINT64_MAX;
        int best = 0, type;
        for (x = 0; x < width; ++x) {
            const uint8_t *p = rgba + y * stride + x * 4;
            memcpy(cur + x * bpp, p, bpp);
            if (bpp == 4 && (flags & AX_PNG_PREMULTIPLIED) != 0) {
                ax_rgba_unpremultiply(cur + x * 4, 1, 1, 4);
            }
        }
        for (type = 0; type < 5; ++type) {
            uint64_t sum = 0;
            for (x = 0; x < row_bytes; ++x) {
                const uint8_t v = (uint8_t)(cur[x] - predict(type, cur, up, x, bpp));
                sum += v < 128 ? v : 256u - v;
            }
            if (sum < best_sum) {
                best_sum = sum;
                best = type;
            }
        }
        dst[0] = (uint8_t)best;
        for (x = 0; x < row_bytes; ++x) {
            dst[1 + x] = (uint8_t)(cur[x] - predict(best, cur, up, x, bpp));
        }
    }

    memset(&z, 0, sizeof z);
    z.a = a;
    if (!zlib_deflate(a, raw, raw_size, &z)) {
        buf_free(&z);
        ax_mem_free(a, raw, raw_size + 2u * row_bytes);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_encode: out of memory");
    }
    ax_mem_free(a, raw, raw_size + 2u * row_bytes);

    memset(&png, 0, sizeof png);
    png.a = a;
    buf_put(&png, "\x89PNG\r\n\x1a\n", 8);
    ihdr[0] = (uint8_t)(width >> 24);
    ihdr[1] = (uint8_t)(width >> 16);
    ihdr[2] = (uint8_t)(width >> 8);
    ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24);
    ihdr[5] = (uint8_t)(height >> 16);
    ihdr[6] = (uint8_t)(height >> 8);
    ihdr[7] = (uint8_t)height;
    ihdr[8] = 8;                           /* bit depth */
    ihdr[9] = (uint8_t)(bpp == 4 ? 6 : 2); /* RGBA or RGB */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    {
        const struct {
            const char *type;
            const uint8_t *data;
            size_t len;
        } chunks[4] = {{"IHDR", ihdr, 13},
                       {"sRGB", (const uint8_t *)"\0", 1},
                       {"IDAT", z.p, z.len},
                       {"IEND", NULL, 0}};
        int c;
        for (c = 0; c < 4; ++c) {
            uint32_t crc = crc32_update(0, (const uint8_t *)chunks[c].type, 4);
            if (chunks[c].len != 0) {
                crc = crc32_update(crc, chunks[c].data, chunks[c].len);
            }
            buf_be32(&png, (uint32_t)chunks[c].len);
            buf_put(&png, chunks[c].type, 4);
            buf_put(&png, chunks[c].data, chunks[c].len);
            buf_be32(&png, crc);
        }
    }
    buf_free(&z);
    if (png.oom) {
        buf_free(&png);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_encode: out of memory");
    }
    /* Hand over exactly len bytes, so ax_png_free knows the size. */
    *out = (uint8_t *)ax_mem_alloc(a, png.len, 1);
    if (*out == NULL) {
        buf_free(&png);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_encode: out of memory");
    }
    memcpy(*out, png.p, png.len);
    *out_size = png.len;
    buf_free(&png);
    return AX_OK;
}

void ax_png_free(const ax_allocator *allocator, uint8_t *data, size_t size)
{
    if (data != NULL) {
        ax_mem_free(allocator != NULL ? allocator : ax_default_allocator(), data, size);
    }
}

/* ---- inflate (after puff) -------------------------------------------------------------------- */

typedef struct inflate_state {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint32_t bitbuf, bitcnt;
    uint8_t *out;
    size_t out_len, out_pos;
    bool error;
} inflate_state;

static uint32_t get_bits(inflate_state *s, uint32_t need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) {
            s->error = true;
            return 0;
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return val & ((1u << need) - 1u);
}

#define MAXBITS   15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct huffman {
    uint16_t count[MAXBITS + 1];
    uint16_t symbol[FIXLCODES];
} huffman;

static int decode_sym(inflate_state *s, const huffman *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= MAXBITS; ++len) {
        int count;
        code |= (int)get_bits(s, 1);
        if (s->error) {
            return -1;
        }
        count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->error = true; /* ran out of codes */
    return -1;
}

/* Builds the decoding table from code lengths. Returns 0 for a complete code, a positive number
 * for an incomplete one and a negative one for an over-subscribed one. */
static int construct(huffman *h, const uint16_t *length, int n)
{
    uint16_t offs[MAXBITS + 1];
    int symbol, len, left;
    for (len = 0; len <= MAXBITS; ++len) {
        h->count[len] = 0;
    }
    for (symbol = 0; symbol < n; ++symbol) {
        h->count[length[symbol]]++;
    }
    if (h->count[0] == n) {
        return 0; /* no codes: complete, but decoding will fail */
    }
    left = 1;
    for (len = 1; len <= MAXBITS; ++len) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return left;
        }
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; ++len) {
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    }
    for (symbol = 0; symbol < n; ++symbol) {
        if (length[symbol] != 0) {
            h->symbol[offs[length[symbol]]++] = (uint16_t)symbol;
        }
    }
    return left;
}

static bool codes(inflate_state *s, const huffman *lencode, const huffman *distcode)
{
    for (;;) {
        int symbol = decode_sym(s, lencode);
        if (s->error || symbol < 0) {
            return false;
        }
        if (symbol < 256) {
            if (s->out_pos >= s->out_len) {
                return false;
            }
            s->out[s->out_pos++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            return true;
        } else {
            uint32_t len, dist;
            symbol -= 257;
            if (symbol >= 29) {
                return false;
            }
            len = k_len_base[symbol] + get_bits(s, k_len_extra[symbol]);
            symbol = decode_sym(s, distcode);
            if (s->error || symbol < 0 || symbol >= 30) {
                return false;
            }
            dist = k_dist_base[symbol] + get_bits(s, k_dist_extra[symbol]);
            if (s->error || dist > s->out_pos || len > s->out_len - s->out_pos) {
                return false;
            }
            while (len-- > 0) {
                s->out[s->out_pos] = s->out[s->out_pos - dist];
                s->out_pos++;
            }
        }
    }
}

static bool stored(inflate_state *s)
{
    uint32_t len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->in_pos + 4 > s->in_len) {
        return false;
    }
    len = s->in[s->in_pos] | ((uint32_t)s->in[s->in_pos + 1] << 8);
    if (s->in[s->in_pos + 2] != (uint8_t)~len || s->in[s->in_pos + 3] != (uint8_t)(~len >> 8)) {
        return false;
    }
    s->in_pos += 4;
    if (len > s->in_len - s->in_pos || len > s->out_len - s->out_pos) {
        return false;
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->out_pos += len;
    s->in_pos += len;
    return true;
}

static bool fixed(inflate_state *s)
{
    huffman lencode, distcode;
    uint16_t lengths[FIXLCODES];
    int symbol;
    for (symbol = 0; symbol < 144; ++symbol) {
        lengths[symbol] = 8;
    }
    for (; symbol < 256; ++symbol) {
        lengths[symbol] = 9;
    }
    for (; symbol < 280; ++symbol) {
        lengths[symbol] = 7;
    }
    for (; symbol < FIXLCODES; ++symbol) {
        lengths[symbol] = 8;
    }
    construct(&lencode, lengths, FIXLCODES);
    for (symbol = 0; symbol < MAXDCODES; ++symbol) {
        lengths[symbol] = 5;
    }
    construct(&distcode, lengths, MAXDCODES);
    return codes(s, &lencode, &distcode);
}

static bool dynamic(inflate_state *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};
    uint16_t lengths[MAXLCODES + MAXDCODES];
    huffman lencode, distcode;
    int nlen, ndist, ncode, index, err;
    nlen = (int)get_bits(s, 5) + 257;
    ndist = (int)get_bits(s, 5) + 1;
    ncode = (int)get_bits(s, 4) + 4;
    if (s->error || nlen > MAXLCODES || ndist > MAXDCODES) {
        return false;
    }
    for (index = 0; index < ncode; ++index) {
        lengths[order[index]] = (uint16_t)get_bits(s, 3);
    }
    for (; index < 19; ++index) {
        lengths[order[index]] = 0;
    }
    if (s->error || construct(&lencode, lengths, 19) != 0) {
        return false; /* the code-length code must be complete */
    }
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode_sym(s, &lencode);
        if (s->error || symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            lengths[index++] = (uint16_t)symbol;
        } else {
            uint16_t len = 0;
            int rep;
            if (symbol == 16) {
                if (index == 0) {
                    return false;
                }
                len = lengths[index - 1];
                rep = 3 + (int)get_bits(s, 2);
            } else if (symbol == 17) {
                rep = 3 + (int)get_bits(s, 3);
            } else {
                rep = 11 + (int)get_bits(s, 7);
            }
            if (s->error || index + rep > nlen + ndist) {
                return false;
            }
            while (rep-- > 0) {
                lengths[index++] = len;
            }
        }
    }
    if (lengths[256] == 0) {
        return false; /* no end-of-block code */
    }
    err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) {
        return false; /* incomplete only with a single length code */
    }
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) {
        return false;
    }
    return codes(s, &lencode, &distcode);
}

/* Inflates a zlib stream into exactly out_len bytes. */
static bool zlib_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    inflate_state s;
    uint32_t last;
    uint32_t adler;
    if (in_len < 6 || (in[0] & 0x0Fu) != 8 || (in[0] >> 4) > 7 ||
        (((uint32_t)in[0] << 8) | in[1]) % 31u != 0 || (in[1] & 0x20u) != 0) {
        return false;
    }
    memset(&s, 0, sizeof s);
    s.in = in;
    s.in_len = in_len - 4; /* the Adler-32 trailer */
    s.in_pos = 2;
    s.out = out;
    s.out_len = out_len;
    do {
        uint32_t type;
        last = get_bits(&s, 1);
        type = get_bits(&s, 2);
        if (s.error) {
            return false;
        }
        if (type == 0) {
            if (!stored(&s)) {
                return false;
            }
        } else if (type == 1) {
            if (!fixed(&s)) {
                return false;
            }
        } else if (type == 2) {
            if (!dynamic(&s)) {
                return false;
            }
        } else {
            return false;
        }
    } while (!last);
    if (s.out_pos != out_len) {
        return false;
    }
    adler = ((uint32_t)in[in_len - 4] << 24) | ((uint32_t)in[in_len - 3] << 16) |
            ((uint32_t)in[in_len - 2] << 8) | in[in_len - 1];
    return adler == adler32(out, out_len);
}

/* ---- decoding -------------------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static ax_result corrupt(const char *why)
{
    return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_decode: %s", why);
}

ax_result ax_png_decode(const ax_allocator *allocator, const void *data, size_t size, ax_image *out)
{
    const ax_allocator *a = allocator != NULL ? allocator : ax_default_allocator();
    const uint8_t *p = (const uint8_t *)data;
    size_t pos = 8;
    uint32_t width = 0, height = 0, color = 0, channels = 0;
    bool have_ihdr = false, have_iend = false;
    buf idat;
    uint8_t *raw = NULL, *rgba = NULL;
    size_t row_bytes = 0, raw_size = 0, x, y;
    ax_result res = AX_OK;

    memset(out, 0, sizeof *out);
    if (p == NULL || size < 8 || memcmp(p, "\x89PNG\r\n\x1a\n", 8) != 0) {
        return corrupt("not a PNG file");
    }
    memset(&idat, 0, sizeof idat);
    idat.a = a;
    while (!have_iend) {
        uint32_t len;
        const uint8_t *type, *body;
        if (size - pos < 12) {
            res = corrupt("truncated chunk");
            goto done;
        }
        len = be32(p + pos);
        if (len > size - pos - 12) {
            res = corrupt("chunk longer than the file");
            goto done;
        }
        type = p + pos + 4;
        body = p + pos + 8;
        if (crc32_update(crc32_update(0, type, 4), body, len) != be32(body + len)) {
            res = corrupt("chunk CRC mismatch");
            goto done;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (have_ihdr || len != 13) {
                res = corrupt("bad IHDR");
                goto done;
            }
            width = be32(body);
            height = be32(body + 4);
            color = body[9];
            if (width == 0 || height == 0 || width > AX_PNG_MAX_SIDE || height > AX_PNG_MAX_SIDE ||
                body[10] != 0 || body[11] != 0) {
                res = corrupt("bad IHDR values");
                goto done;
            }
            if (body[8] != 8 || color == 3 || body[12] != 0) {
                res = ax_set_error(AX_ERR_UNSUPPORTED,
                                   "ax_png_decode: only 8-bit, non-interlaced grey, grey and "
                                   "alpha, RGB and RGBA images are read");
                goto done;
            }
            channels = color == 0 ? 1 : color == 4 ? 2 : color == 2 ? 3 : color == 6 ? 4 : 0;
            if (channels == 0) {
                res = corrupt("unknown color type");
                goto done;
            }
            have_ihdr = true;
        } else if (!have_ihdr) {
            res = corrupt("IHDR is not first");
            goto done;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            buf_put(&idat, body, len);
            if (idat.oom) {
                res = ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_decode: out of memory");
                goto done;
            }
        } else if (memcmp(type, "IEND", 4) == 0) {
            have_iend = true;
        } else if ((type[0] & 0x20u) == 0) {
            res = ax_set_error(AX_ERR_UNSUPPORTED, "ax_png_decode: unknown critical chunk");
            goto done;
        }
        pos += 12u + len;
    }

    row_bytes = (size_t)width * channels;
    raw_size = (row_bytes + 1u) * height;
    raw = (uint8_t *)ax_mem_alloc(a, raw_size, 1);
    rgba = (uint8_t *)ax_mem_alloc(a, (size_t)width * height * 4u, 1);
    if (raw == NULL || rgba == NULL) {
        res = ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_decode: out of memory");
        goto done;
    }
    if (idat.len == 0 || !zlib_inflate(idat.p, idat.len, raw, raw_size)) {
        res = corrupt("corrupt image data");
        goto done;
    }
    for (y = 0; y < height; ++y) {
        uint8_t *row = raw + y * (row_bytes + 1u) + 1u;
        const uint8_t *up = y > 0 ? raw + (y - 1u) * (row_bytes + 1u) + 1u : NULL;
        const int type = row[-1];
        if (type > 4) {
            res = corrupt("unknown filter");
            goto done;
        }
        for (x = 0; x < row_bytes; ++x) {
            row[x] = (uint8_t)(row[x] + predict(type, row, up, x, channels));
        }
        for (x = 0; x < width; ++x) {
            const uint8_t *s = row + x * channels;
            uint8_t *d = rgba + (y * width + x) * 4u;
            switch (channels) {
            case 1:
                d[0] = d[1] = d[2] = s[0];
                d[3] = 255;
                break;
            case 2:
                d[0] = d[1] = d[2] = s[0];
                d[3] = s[1];
                break;
            case 3:
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
                break;
            default:
                memcpy(d, s, 4);
                break;
            }
        }
    }
    out->width = width;
    out->height = height;
    out->rgba = rgba;
    rgba = NULL;

done:
    buf_free(&idat);
    if (raw != NULL) {
        ax_mem_free(a, raw, raw_size);
    }
    if (rgba != NULL) {
        ax_mem_free(a, rgba, (size_t)width * height * 4u);
    }
    return res;
}

void ax_image_free(const ax_allocator *allocator, ax_image *image)
{
    if (image->rgba != NULL) {
        ax_mem_free(allocator != NULL ? allocator : ax_default_allocator(), image->rgba,
                    (size_t)image->width * image->height * 4u);
    }
    memset(image, 0, sizeof *image);
}

/* ---- files ----------------------------------------------------------------------------------- */

ax_result ax_png_write_file(const char *path, const uint8_t *rgba, uint32_t width, uint32_t height,
                            size_t stride, uint32_t flags)
{
    uint8_t *data;
    size_t size;
    FILE *f;
    ax_result r = ax_png_encode(NULL, rgba, width, height, stride, flags, &data, &size);
    if (r != AX_OK) {
        return r;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        ax_png_free(NULL, data, size);
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_write_file: cannot create %s", path);
    }
    if (fwrite(data, 1, size, f) != size) {
        r = ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_write_file: cannot write %s", path);
    }
    if (fclose(f) != 0 && r == AX_OK) {
        r = ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_write_file: cannot write %s", path);
    }
    ax_png_free(NULL, data, size);
    return r;
}

ax_result ax_png_read_file(const ax_allocator *allocator, const char *path, ax_image *out)
{
    const ax_allocator *a = allocator != NULL ? allocator : ax_default_allocator();
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *data;
    ax_result r;
    memset(out, 0, sizeof *out);
    if (f == NULL) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_read_file: cannot open %s", path);
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_read_file: cannot read %s", path);
    }
    data = (uint8_t *)ax_mem_alloc(a, (size_t)len + 1u, 1);
    if (data == NULL) {
        fclose(f);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_png_read_file: out of memory");
    }
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        ax_mem_free(a, data, (size_t)len + 1u);
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_png_read_file: cannot read %s", path);
    }
    fclose(f);
    r = ax_png_decode(a, data, (size_t)len, out);
    ax_mem_free(a, data, (size_t)len + 1u);
    return r;
}
