/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: PNG images for snapshots and goldens (decision 82). AXION's own codec, without
 * dependencies:
 *   - the encoder writes 8-bit RGB (every pixel opaque) or RGBA, non-interlaced, with an sRGB
 *     chunk, a filter chosen per row and a real deflate stream (LZ77 with fixed Huffman codes).
 *     Its output depends only on the pixels, so the same image is the same file everywhere;
 *   - the decoder reads 8-bit grey, grey with alpha, RGB and RGBA, non-interlaced, every filter,
 *     and any valid zlib stream (stored, fixed and dynamic Huffman blocks). It checks every length
 *     and code against its input, so corrupt files fail with AX_ERR_INVALID_ARGUMENT.
 *
 * PNG stores straight (unpremultiplied) alpha. Renderers produce premultiplied pixels: encode them
 * with AX_PNG_PREMULTIPLIED, which divides by alpha with integer rounding (lossless for opaque
 * pixels, which snapshots almost always are).
 *
 * thread: any
 */
#ifndef AX_HEADLESS_PNG_H
#define AX_HEADLESS_PNG_H

#include <stddef.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

#define AX_PNG_MAX_SIDE 32768u

/* An 8-bit RGBA image with straight alpha, rows packed (stride width * 4). */
typedef struct ax_image {
    uint32_t width, height;
    uint8_t *rgba;
} ax_image;

/* ax_png_encode flags */
#define AX_PNG_PREMULTIPLIED 0x1u /* the input is premultiplied: divide by alpha first */

/*
 * Encodes width x height pixels (RGBA8, rows `stride` bytes apart) into a new buffer from
 * `allocator` (NULL: the default), which the caller frees with ax_png_free.
 * Errors: AX_ERR_INVALID_ARGUMENT (size), AX_ERR_OUT_OF_MEMORY.
 */
AX_API ax_result ax_png_encode(const ax_allocator *allocator, const uint8_t *rgba, uint32_t width,
                               uint32_t height, size_t stride, uint32_t flags, uint8_t **out,
                               size_t *out_size);
AX_API void ax_png_free(const ax_allocator *allocator, uint8_t *data, size_t size);

/* Decodes a PNG into out (RGBA8, straight alpha); free it with ax_image_free.
 * Errors: AX_ERR_INVALID_ARGUMENT (not a PNG, corrupt, or a kind this decoder does not read:
 * ax_last_error says which), AX_ERR_UNSUPPORTED (16-bit, palette or interlaced images),
 * AX_ERR_OUT_OF_MEMORY. */
AX_API ax_result ax_png_decode(const ax_allocator *allocator, const void *data, size_t size,
                               ax_image *out);
AX_API void ax_image_free(const ax_allocator *allocator, ax_image *image);

/* Premultiplied RGBA8 to straight alpha, in place: c * 255 / a, rounded to nearest; a pixel with
 * alpha 0 becomes 0, 0, 0, 0. The conversion AX_PNG_PREMULTIPLIED applies. */
AX_API void ax_rgba_unpremultiply(uint8_t *rgba, uint32_t width, uint32_t height, size_t stride);

/* Files. Errors: those above; a file that cannot be opened, read or written is
 * AX_ERR_INVALID_ARGUMENT, with the path in ax_last_error. */
AX_API ax_result ax_png_write_file(const char *path, const uint8_t *rgba, uint32_t width,
                                   uint32_t height, size_t stride, uint32_t flags);
AX_API ax_result ax_png_read_file(const ax_allocator *allocator, const char *path, ax_image *out);

AX_EXTERN_C_END

#endif
