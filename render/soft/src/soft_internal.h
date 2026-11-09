/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft internals: the command list a snapshot is flattened into, the shapes whose
 * coverage the rasterizer computes, and the per-thread tile context.
 *
 * Every function here computes with IEEE single precision in a fixed order. The library is built
 * with floating-point contraction off (render/soft/CMakeLists.txt), so no compiler fuses a
 * multiply and an add, and calls nothing transcendental from libm (soft_math.c has its own).
 */
#ifndef AX_RENDER_SOFT_INTERNAL_H
#define AX_RENDER_SOFT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/base/ax_arena.h>
#include <axion/render/ax_render_soft.h>

#define SOFT_TILE        AX_RENDER_SOFT_TILE
#define SOFT_TILE_PIXELS (SOFT_TILE * SOFT_TILE)

/* Coverage below this is zero, and accumulated channels below SOFT_FLUSH are zero: both keep the
 * arithmetic away from denormals, so flush-to-zero modes cannot change a result (decision 81). */
#define SOFT_MIN_COVERAGE (1.0f / 65536.0f)
#define SOFT_FLUSH        (1.0f / 16777216.0f)
#define SOFT_MIN_COLOR    (1.0f / 1048576.0f)

/* Transforms whose linear part (or its inverse) has an entry beyond this are degenerate for
 * drawing: their items count as invalid. It bounds every gradient the coverage uses. */
#define SOFT_MAX_SCALE 1.0e6f

/* ---- deterministic math (soft_math.c) -------------------------------------------------------- */

typedef struct soft_affine {
    float a, b, c, d, tx, ty; /* x' = a x + c y + tx; y' = b x + d y + ty */
} soft_affine;

soft_affine ax__soft_affine_mul(soft_affine m, soft_affine n); /* m after n */
bool ax__soft_affine_invert(soft_affine m, soft_affine *out);

/* sRGB transfer functions, clamped to [0, 1] (base's deterministic ones). */
float ax__soft_linear_to_srgb(float linear);
float ax__soft_srgb_to_linear(float encoded);

/* ---- shapes ---------------------------------------------------------------------------------- */

/* A slab's constants: the unit normal's components sorted (lo <= hi) and what the coverage of a
 * straight edge needs from them (decision 55's trapezoid). */
typedef struct soft_edge {
    float rlen;     /* 1 / |gradient of the local coordinate, in device pixels| */
    float lo, hi;   /* sorted |components| of the edge's unit normal */
    float half_sum; /* 0.5 * (lo + hi) */
    float sum;      /* lo + hi */
    float half_lo;  /* 0.5 * lo */
    float inv_hi;   /* 1 / hi */
    float k;        /* 1 / (2 lo hi), when not axis-aligned */
    uint32_t axis;  /* lo < 1e-4: the edge is axis-aligned on screen */
} soft_edge;

/* A rect or rounded rect in local space, with its device-to-local transform: an item's geometry,
 * or a clip's. */
typedef struct soft_shape {
    float ia, ib, ic, id, itx, ity; /* device -> local */
    float x0, y0, x1, y1;
    soft_edge ex, ey;
    uint32_t corners;                 /* bit k: corner k (tl, tr, br, bl) is rounded */
    float r[4];                       /* clamped radii */
    float cx[4], cy[4], ix[4], iy[4]; /* corner centres and inward signs */
} soft_shape;

/* Pixel bounds, exclusive at the end. */
typedef struct soft_box {
    int32_t x0, y0, x1, y1;
} soft_box;

typedef struct soft_clip {
    soft_shape shape;
    uint32_t parent; /* AX_IR_NO_INDEX for the outermost */
} soft_clip;

enum { SOFT_CMD_ITEM = 0, SOFT_CMD_PUSH = 1, SOFT_CMD_POP = 2, SOFT_CMD_GLYPH = 3 };

/* A glyph's bitmap as a command samples it (decision 101). */
typedef struct soft_glyph {
    const uint8_t *pixels;
    uint32_t stride;
    int32_t width, height;
    uint32_t exact;     /* raster pixels land on device pixels: copied as they are */
    int32_t x0, y0;     /* exact: the device pixel of the bitmap's top-left */
    soft_affine inv;    /* otherwise: device point -> bitmap coordinates (pixel units) */
    const uint8_t *lut; /* the coverage correction for the run's luminance (256 entries) */
} soft_glyph;

/* One step of the frame, in painting order. */
typedef struct soft_cmd {
    uint32_t kind;
    uint32_t clip;    /* innermost clip, or AX_IR_NO_INDEX */
    uint32_t linear;  /* blend in linear light */
    uint32_t end;     /* PUSH: index of the matching POP */
    soft_box box;     /* ITEM: pixels it may touch; PUSH/POP: pixels its group may touch */
    float color[4];   /* ITEM: premultiplied (sRGB-encoded, or linear when `linear`); PUSH: [3] is
                       * the group's opacity */
    soft_shape shape; /* ITEM */
    soft_glyph glyph; /* GLYPH */
    const uint8_t *shape_lut; /* ITEM: coverage correction (AX_RENDER_BLEND_SRGB_CORRECTED), or NULL */
} soft_cmd;

/* A prepared frame: everything tiles read, never written while they run. */
typedef struct soft_frame {
    const soft_cmd *cmds;
    uint32_t cmd_count;
    const soft_clip *clips;
    uint32_t clip_count;
    uint32_t group_depth; /* deepest nesting of groups */
    uint32_t width, height;
    float background[4]; /* sRGB-encoded, premultiplied */
    void *pixels;
    size_t stride;
    uint32_t format;
    bool simd;
} soft_frame;

/* How a frame blends (issue #4): the mode and the renderer's correction tables. */
typedef struct soft_blend {
    uint32_t mode; /* ax_render_blend */
    const uint8_t (*luts)[AX_IR_TEXT_LUT_BUCKETS][256]; /* AX_IR_LUT_TABLES tables */
} soft_blend;

/* Turns a snapshot into the frame's commands (prepare.c), in `arena`. */
ax_result ax__soft_prepare(const ax_render_ir *ir, uint32_t width, uint32_t height, float scale,
                           const soft_blend *blend, ax_arena *arena, soft_frame *out,
                           ax_render_counts *counts);

/* A coverage through a correction table (the same quantization as glyphs: wgpu's too). */
static inline float ax__soft_lut_coverage(const uint8_t *lut, float cov)
{
    const int32_t i = (int32_t)(cov * 255.0f + 0.5f);
    return (float)lut[i < 0 ? 0 : (i > 255 ? 255 : i)] * (1.0f / 255.0f);
}

/* State another part of the renderer (surface.c) keeps with it, freed by ax_render_soft_destroy.
 * One allocation per renderer. */
void *ax__render_soft_alloc_user(ax_render_soft *r, size_t size);
void *ax__render_soft_user(ax_render_soft *r);

/* ---- tiles (raster.c) ------------------------------------------------------------------------ */

/* One thread's tile buffers: the base and one per open group, each four planes of floats. */
typedef struct soft_tile_ctx {
    float *planes; /* (group_depth + 1) * 4 * SOFT_TILE_PIXELS */
    uint32_t depth_capacity;
} soft_tile_ctx;

/* Draws tile (tx, ty) of the frame into its pixels. */
void ax__soft_draw_tile(const soft_frame *f, soft_tile_ctx *ctx, uint32_t tx, uint32_t ty);

/* Coverage of a shape at device point (x, y): the scalar reference (raster.c). */
float ax__soft_shape_coverage(const soft_shape *s, float x, float y);

/* Source-over of color * cov onto pixel i of a tile's planes, in sRGB space or linear light. */
void ax__soft_blend_pixel(float *planes, size_t i, const float color[4], float cov, bool linear);

/* ---- the SIMD path (raster_simd.c) ----------------------------------------------------------- */

/* True when this build has one (SSE2, NEON on 64-bit ARM, WebAssembly SIMD128), and its name. */
bool ax__soft_simd_available(void);
const char *ax__soft_simd_name(void);

/* draw_item for the tile-relative pixels [x0, x1) x [y0, y1), four at a time. */
void ax__soft_draw_item_simd(const soft_frame *f, const soft_cmd *cmd, float *planes, int32_t ox,
                             int32_t oy, int32_t x0, int32_t y0, int32_t x1, int32_t y1);

/* Coverage at four points of one row (scalar when the build has no SIMD), for the tests. */
void ax__soft_shape_coverage4(const soft_shape *s, const float x[4], float y, float out[4]);

#endif
