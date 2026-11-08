/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Four float lanes for ax_render_soft's SIMD path: SSE2 (every x86-64), NEON on 64-bit ARM and
 * WebAssembly SIMD128. Only operations that IEEE defines exactly (add, subtract, multiply, divide,
 * square root, comparisons, bitwise selection, truncating conversion) are used, so each lane gives
 * the bits the scalar path gives for the same operations. min, max and abs are written as compare
 * and select, the scalar path's ternaries, because the instructions differ on signed zeros
 * between ISAs.
 */
#ifndef AX_RENDER_SOFT_SIMD4_H
#define AX_RENDER_SOFT_SIMD4_H

#include <stdint.h>
#include <string.h>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    define SOFT_SIMD      1
#    define SOFT_SIMD_NAME "SSE2"
#    include <emmintrin.h>
typedef __m128 v4f;
typedef __m128 v4m; /* all ones or all zeros per lane */
static inline v4f v4_set1(float f)
{
    return _mm_set1_ps(f);
}
static inline v4f v4_set(float a, float b, float c, float d)
{
    return _mm_setr_ps(a, b, c, d);
}
static inline v4f v4_load(const float *p)
{
    return _mm_loadu_ps(p);
}
static inline void v4_store(float *p, v4f v)
{
    _mm_storeu_ps(p, v);
}
static inline v4f v4_add(v4f a, v4f b)
{
    return _mm_add_ps(a, b);
}
static inline v4f v4_sub(v4f a, v4f b)
{
    return _mm_sub_ps(a, b);
}
static inline v4f v4_mul(v4f a, v4f b)
{
    return _mm_mul_ps(a, b);
}
static inline v4f v4_div(v4f a, v4f b)
{
    return _mm_div_ps(a, b);
}
static inline v4f v4_sqrt(v4f a)
{
    return _mm_sqrt_ps(a);
}
static inline v4m v4_lt(v4f a, v4f b)
{
    return _mm_cmplt_ps(a, b);
}
static inline v4m v4_le(v4f a, v4f b)
{
    return _mm_cmple_ps(a, b);
}
static inline v4m v4_gt(v4f a, v4f b)
{
    return _mm_cmpgt_ps(a, b);
}
static inline v4m v4_ge(v4f a, v4f b)
{
    return _mm_cmpge_ps(a, b);
}
static inline v4m m4_and(v4m a, v4m b)
{
    return _mm_and_ps(a, b);
}
static inline v4m m4_or(v4m a, v4m b)
{
    return _mm_or_ps(a, b);
}
/* m ? a : b, per lane */
static inline v4f v4_sel(v4m m, v4f a, v4f b)
{
    return _mm_or_ps(_mm_and_ps(m, a), _mm_andnot_ps(m, b));
}
static inline v4f v4_neg(v4f a)
{
    return _mm_xor_ps(a, _mm_set1_ps(-0.0f));
}
static inline int m4_any(v4m m)
{
    return _mm_movemask_ps(m) != 0;
}
/* Lanes start + 0..3 (start < 2^24), exactly. */
static inline v4f v4_iota(int32_t start)
{
    return _mm_cvtepi32_ps(_mm_add_epi32(_mm_set1_epi32(start), _mm_setr_epi32(0, 1, 2, 3)));
}
/* Lanes whose index start + 0..3 lies in [lo, hi). */
static inline v4m m4_range(int32_t start, int32_t lo, int32_t hi)
{
    const __m128i i = _mm_add_epi32(_mm_set1_epi32(start), _mm_setr_epi32(0, 1, 2, 3));
    const __m128i in = _mm_and_si128(_mm_cmpgt_epi32(i, _mm_set1_epi32(lo - 1)),
                                     _mm_cmplt_epi32(i, _mm_set1_epi32(hi)));
    return _mm_castsi128_ps(in);
}
/* Truncation toward zero of each lane (values in [0, 256)). */
static inline void v4_trunc_u32(v4f v, uint32_t out[4])
{
    _mm_storeu_si128((__m128i *)(void *)out, _mm_cvttps_epi32(v));
}

#elif defined(__aarch64__) || defined(_M_ARM64)
#    define SOFT_SIMD      1
#    define SOFT_SIMD_NAME "NEON"
#    include <arm_neon.h>
typedef float32x4_t v4f;
typedef uint32x4_t v4m;
static inline v4f v4_set1(float f)
{
    return vdupq_n_f32(f);
}
static inline v4f v4_set(float a, float b, float c, float d)
{
    const float v[4] = {a, b, c, d};
    return vld1q_f32(v);
}
static inline v4f v4_load(const float *p)
{
    return vld1q_f32(p);
}
static inline void v4_store(float *p, v4f v)
{
    vst1q_f32(p, v);
}
static inline v4f v4_add(v4f a, v4f b)
{
    return vaddq_f32(a, b);
}
static inline v4f v4_sub(v4f a, v4f b)
{
    return vsubq_f32(a, b);
}
static inline v4f v4_mul(v4f a, v4f b)
{
    return vmulq_f32(a, b);
}
static inline v4f v4_div(v4f a, v4f b)
{
    return vdivq_f32(a, b);
}
static inline v4f v4_sqrt(v4f a)
{
    return vsqrtq_f32(a);
}
static inline v4m v4_lt(v4f a, v4f b)
{
    return vcltq_f32(a, b);
}
static inline v4m v4_le(v4f a, v4f b)
{
    return vcleq_f32(a, b);
}
static inline v4m v4_gt(v4f a, v4f b)
{
    return vcgtq_f32(a, b);
}
static inline v4m v4_ge(v4f a, v4f b)
{
    return vcgeq_f32(a, b);
}
static inline v4m m4_and(v4m a, v4m b)
{
    return vandq_u32(a, b);
}
static inline v4m m4_or(v4m a, v4m b)
{
    return vorrq_u32(a, b);
}
static inline v4f v4_sel(v4m m, v4f a, v4f b)
{
    return vbslq_f32(m, a, b);
}
static inline v4f v4_neg(v4f a)
{
    return vnegq_f32(a); /* flips the sign bit, as the scalar unary minus */
}
static inline int m4_any(v4m m)
{
    return vmaxvq_u32(m) != 0;
}
static inline v4f v4_iota(int32_t start)
{
    const int32_t v[4] = {0, 1, 2, 3};
    return vcvtq_f32_s32(vaddq_s32(vdupq_n_s32(start), vld1q_s32(v)));
}
static inline v4m m4_range(int32_t start, int32_t lo, int32_t hi)
{
    const int32_t v[4] = {0, 1, 2, 3};
    const int32x4_t i = vaddq_s32(vdupq_n_s32(start), vld1q_s32(v));
    return vandq_u32(vcgeq_s32(i, vdupq_n_s32(lo)), vcltq_s32(i, vdupq_n_s32(hi)));
}
static inline void v4_trunc_u32(v4f v, uint32_t out[4])
{
    vst1q_u32(out, vreinterpretq_u32_s32(vcvtq_s32_f32(v)));
}

#elif defined(__wasm_simd128__)
#    define SOFT_SIMD      1
#    define SOFT_SIMD_NAME "SIMD128"
#    include <wasm_simd128.h>
typedef v128_t v4f;
typedef v128_t v4m;
static inline v4f v4_set1(float f)
{
    return wasm_f32x4_splat(f);
}
static inline v4f v4_set(float a, float b, float c, float d)
{
    return wasm_f32x4_make(a, b, c, d);
}
static inline v4f v4_load(const float *p)
{
    return wasm_v128_load(p);
}
static inline void v4_store(float *p, v4f v)
{
    wasm_v128_store(p, v);
}
static inline v4f v4_add(v4f a, v4f b)
{
    return wasm_f32x4_add(a, b);
}
static inline v4f v4_sub(v4f a, v4f b)
{
    return wasm_f32x4_sub(a, b);
}
static inline v4f v4_mul(v4f a, v4f b)
{
    return wasm_f32x4_mul(a, b);
}
static inline v4f v4_div(v4f a, v4f b)
{
    return wasm_f32x4_div(a, b);
}
static inline v4f v4_sqrt(v4f a)
{
    return wasm_f32x4_sqrt(a);
}
static inline v4m v4_lt(v4f a, v4f b)
{
    return wasm_f32x4_lt(a, b);
}
static inline v4m v4_le(v4f a, v4f b)
{
    return wasm_f32x4_le(a, b);
}
static inline v4m v4_gt(v4f a, v4f b)
{
    return wasm_f32x4_gt(a, b);
}
static inline v4m v4_ge(v4f a, v4f b)
{
    return wasm_f32x4_ge(a, b);
}
static inline v4m m4_and(v4m a, v4m b)
{
    return wasm_v128_and(a, b);
}
static inline v4m m4_or(v4m a, v4m b)
{
    return wasm_v128_or(a, b);
}
static inline v4f v4_sel(v4m m, v4f a, v4f b)
{
    return wasm_v128_bitselect(a, b, m);
}
static inline v4f v4_neg(v4f a)
{
    return wasm_f32x4_neg(a); /* flips the sign bit */
}
static inline int m4_any(v4m m)
{
    return wasm_v128_any_true(m);
}
static inline v4f v4_iota(int32_t start)
{
    return wasm_f32x4_convert_i32x4(
        wasm_i32x4_add(wasm_i32x4_splat(start), wasm_i32x4_make(0, 1, 2, 3)));
}
static inline v4m m4_range(int32_t start, int32_t lo, int32_t hi)
{
    const v128_t i = wasm_i32x4_add(wasm_i32x4_splat(start), wasm_i32x4_make(0, 1, 2, 3));
    return wasm_v128_and(wasm_i32x4_ge(i, wasm_i32x4_splat(lo)),
                         wasm_i32x4_lt(i, wasm_i32x4_splat(hi)));
}
static inline void v4_trunc_u32(v4f v, uint32_t out[4])
{
    wasm_v128_store(out, wasm_i32x4_trunc_sat_f32x4(v));
}

#else
#    define SOFT_SIMD      0
#    define SOFT_SIMD_NAME "none"
#endif

#endif
