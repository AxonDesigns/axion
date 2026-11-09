/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_wgpu: pipelines, encoding a Render IR snapshot into instances, and drawing it into a
 * texture (decision 55).
 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/base/ax_thread.h>

#include "rw_internal.h"
#include "rw_testing.h"

AX_STATIC_ASSERT(sizeof(rw_instance) == 96, "rw_instance matches rect.wgsl's Instance");

/* ---- creation ---------------------------------------------------------------------------------
 */

ax_result ax_render_wgpu_create(const ax_allocator *allocator, const ax_render_wgpu_desc *desc,
                                ax_render_wgpu **out)
{
    ax_render_wgpu *r;
    ax_result res;
    if (allocator == NULL) {
        allocator = ax_default_allocator();
    }
    *out = NULL;
    r = AX_MEM_NEW(allocator, ax_render_wgpu);
    if (r == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_wgpu: out of memory");
    }
    memset(r, 0, sizeof *r);
    r->allocator = allocator;
    r->stats.struct_size = sizeof r->stats;
    r->initial_background = ax_color_make(0.0f, 0.0f, 0.0f, 1.0f);
    if (desc != NULL) {
        r->pacing = desc->pacing;
        r->present_mode = desc->present_mode;
        r->blend = desc->blend;
        if ((desc->diagnostic_flags & AX_RENDER_WGPU_DIAG_LINEAR_BLEND) != 0) {
            r->blend = AX_RENDER_BLEND_LINEAR;
        }
        r->linear_blend = r->blend == AX_RENDER_BLEND_LINEAR;
    }
    if (desc != NULL && desc->gpu != NULL) {
        r->gpu = desc->gpu;
    } else {
        ax_gpu_desc gd;
        memset(&gd, 0, sizeof gd);
        gd.struct_size = sizeof gd;
        gd.allocator = allocator;
        gd.adapter = desc != NULL ? desc->adapter : AX_GPU_ADAPTER_DEFAULT;
        res = ax_gpu_create(&gd, &r->gpu);
        if (res != AX_OK) {
            AX_MEM_DELETE(allocator, r);
            return res;
        }
        r->owns_gpu = true;
    }
    ax_arena_init(&r->scratch, allocator, 0);
    /* Instances (vertex) and clip records (storage) share the ring. */
    res = ax_gpu_upload_ring_create(r->gpu, WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage,
                                    64u * 1024u, &r->ring);
    if (res != AX_OK) {
        ax_arena_deinit(&r->scratch);
        if (r->owns_gpu) {
            ax_gpu_destroy(r->gpu);
        }
        AX_MEM_DELETE(allocator, r);
        return res;
    }
    *out = r;
    return AX_OK;
}

void ax_render_wgpu_destroy(ax_render_wgpu *r)
{
    uint32_t i;
    if (r == NULL) {
        return;
    }
    ax__render_wgpu_destroy_surfaces(r);
    /* A pending creation's callback points at this renderer: let it run first. */
    for (i = 0; i < r->pipeline_count; ++i) {
        rw_pipeline *p = &r->pipelines[i];
        if (p->pending && !p->done) {
            (void)ax_gpu_wait(r->gpu, p->future, &p->done, 10ull * 1000000000ull);
        }
        if (p->pipeline != NULL) {
            wgpuRenderPipelineRelease(p->pipeline);
        }
    }
    if (r->module != NULL) {
        wgpuShaderModuleRelease(r->module);
    }
    ax_gpu_upload_ring_destroy(r->ring);
    ax__rw_atlas_free(r);
    ax_arena_deinit(&r->scratch);
    if (r->owns_gpu) {
        ax_gpu_destroy(r->gpu);
    }
    AX_MEM_DELETE(r->allocator, r);
}

ax_gpu *ax_render_wgpu_gpu(ax_render_wgpu *r)
{
    return r->gpu;
}

/* ---- pipelines --------------------------------------------------------------------------------
 */

static void on_pipeline(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline,
                        WGPUStringView message, void *userdata1, void *userdata2)
{
    rw_pipeline *p = (rw_pipeline *)userdata1;
    ax_render_wgpu *r = p->owner;
    (void)userdata2;
    (void)message;
    p->done = true;
    p->pending = false;
    if (status != WGPUCreatePipelineAsyncStatus_Success || pipeline == NULL ||
        p->generation != ax_gpu_device_generation(r->gpu)) {
        /* Failed, or made for a device that was lost meanwhile: the next prepare retries. */
        if (pipeline != NULL) {
            wgpuRenderPipelineRelease(pipeline);
        }
        p->generation = 0;
        return;
    }
    if (p->pipeline != NULL) {
        wgpuRenderPipelineRelease(p->pipeline);
    }
    p->pipeline = pipeline;
    r->stats.pipelines_created++;
    if (r->stats.pipeline_ready_ns == 0 && r->first_request_ns != 0) {
        r->stats.pipeline_ready_ns = ax_time_monotonic_ns() - r->first_request_ns;
    }
}

static rw_pipeline *find_pipeline(ax_render_wgpu *r, WGPUTextureFormat format)
{
    uint32_t i;
    for (i = 0; i < r->pipeline_count; ++i) {
        if (r->pipelines[i].format == format) {
            return &r->pipelines[i];
        }
    }
    return NULL;
}

static bool ensure_module(ax_render_wgpu *r)
{
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    const uint32_t gen = ax_gpu_device_generation(r->gpu);
    if (r->module != NULL && r->module_generation == gen) {
        return true;
    }
    if (r->module != NULL) {
        wgpuShaderModuleRelease(r->module);
        r->module = NULL;
    }
    wgsl.code.data = ax__rw_rect_wgsl;
    wgsl.code.length = ax__rw_rect_wgsl_size;
    desc.nextInChain = &wgsl.chain;
    r->module = wgpuDeviceCreateShaderModule(ax_gpu_device(r->gpu), &desc);
    r->module_generation = gen;
    return r->module != NULL;
}

void ax_render_wgpu_prepare(ax_render_wgpu *r, WGPUTextureFormat format)
{
    static const char vs_name[] = "vs";
    static const char fs_name[] = "fs";
    const uint32_t gen = ax_gpu_device_generation(r->gpu);
    WGPUVertexAttribute attrs[6];
    WGPUVertexBufferLayout vb = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    WGPUCreateRenderPipelineAsyncCallbackInfo cb =
        WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
    rw_pipeline *p = find_pipeline(r, format);
    int i;

    if (p == NULL) {
        if (r->pipeline_count == RW_MAX_PIPELINES) {
            return;
        }
        p = &r->pipelines[r->pipeline_count++];
        memset(p, 0, sizeof *p);
        p->owner = r;
        p->format = format;
    }
    if (p->pending || (p->pipeline != NULL && p->generation == gen)) {
        return;
    }
    if (p->pipeline != NULL) { /* from a lost device */
        wgpuRenderPipelineRelease(p->pipeline);
        p->pipeline = NULL;
    }
    if (!ensure_module(r)) {
        return;
    }

    for (i = 0; i < 6; ++i) {
        attrs[i].nextInChain = NULL;
        attrs[i].format = WGPUVertexFormat_Float32x4;
        attrs[i].offset = (uint64_t)i * 16u;
        attrs[i].shaderLocation = (uint32_t)i;
    }
    vb.stepMode = WGPUVertexStepMode_Instance;
    vb.arrayStride = sizeof(rw_instance);
    vb.attributeCount = 6;
    vb.attributes = attrs;

    /* Premultiplied alpha, source over. */
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha = blend.color;
    target.format = format;
    target.blend = &blend;
    target.writeMask = WGPUColorWriteMask_All;
    fragment.module = r->module;
    fragment.entryPoint.data = fs_name;
    fragment.entryPoint.length = sizeof fs_name - 1;
    fragment.targetCount = 1;
    fragment.targets = &target;

    desc.vertex.module = r->module;
    desc.vertex.entryPoint.data = vs_name;
    desc.vertex.entryPoint.length = sizeof vs_name - 1;
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vb;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    desc.fragment = &fragment;

    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = on_pipeline;
    cb.userdata1 = p;
    p->pending = true;
    p->done = false;
    p->generation = gen;
    if (r->first_request_ns == 0) {
        r->first_request_ns = ax_time_monotonic_ns();
    }
    p->future = ax_gpu_create_render_pipeline_async(r->gpu, &desc, cb);
}

bool ax_render_wgpu_ready(ax_render_wgpu *r, WGPUTextureFormat format)
{
    const rw_pipeline *p = find_pipeline(r, format);
    if ((r->test_flags & RW_TEST_HOLD_PIPELINES) != 0) {
        return false;
    }
    return p != NULL && p->pipeline != NULL && p->generation == ax_gpu_device_generation(r->gpu);
}

/* ---- encoding ---------------------------------------------------------------------------------
 */

static bool finite4(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]) && isfinite(v[3]);
}

/* CSS: when the radii on one side add up to more than the side, all of them shrink by the same
 * factor. Negative radii are 0. */
static void clamp_radii(const float in[4], float w, float h, float out[4])
{
    float f = 1.0f;
    int i;
    for (i = 0; i < 4; ++i) {
        out[i] = in[i] > 0.0f ? in[i] : 0.0f;
    }
    if (out[0] + out[1] > w) {
        f = fminf(f, w / (out[0] + out[1]));
    }
    if (out[3] + out[2] > w) {
        f = fminf(f, w / (out[3] + out[2]));
    }
    if (out[0] + out[3] > h) {
        f = fminf(f, h / (out[0] + out[3]));
    }
    if (out[1] + out[2] > h) {
        f = fminf(f, h / (out[1] + out[2]));
    }
    for (i = 0; i < 4; ++i) {
        out[i] *= f;
    }
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

typedef struct rw_layer_state {
    ax_affine2 world; /* local -> device pixels */
    ax_affine2 clip;  /* local -> clip space */
    ax_affine2 inv;   /* device pixels -> local (linear part used) */
    float opacity;
    bool skip;
    bool invertible;
    /* Clip regions in force (decision 93): a chain of records evaluated in their own spaces, and
     * the intersection of the axis-aligned rect clips in device pixels. */
    uint32_t chain;     /* innermost record of the chain + 1; 0: none */
    uint32_t chain_len; /* records in the chain */
    bool has_box;
    float box[4];  /* device pixels: x0, y0, x1, y1 */
    uint32_t head; /* what the layer's items name: the box's record, else the chain */
    bool linear;   /* its content blends in linear light (decision 58, issue #4) */
} rw_layer_state;

/* Instances drawn into one view (issue #4's per-layer option): `alt` runs draw into the target's
 * sRGB view, where the blend happens in linear light. */
typedef struct rw_run {
    uint32_t first, count;
    bool alt;
} rw_run;

/* Applies a layer's clip to its state. False when the clip hides everything. */
static bool apply_clip(rw_layer_state *s, const ax_ir_clip *clip, rw_clip *records,
                       uint32_t *record_count, ax_render_counts *counts)
{
    const bool rounded_kind = clip->kind == AX_IR_CLIP_ROUNDED_RECT;
    float radii[4] = {0, 0, 0, 0};
    float rect[4];
    ax_affine2 inv;
    bool rounded;
    memcpy(rect, &clip->rect, sizeof rect);
    if (!isfinite(rect[0] + rect[1] + rect[2] + rect[3]) || !(rect[2] > 0.0f) ||
        !(rect[3] > 0.0f) || !ax_affine2_invert(s->world, &inv)) {
        return false; /* empty or degenerate: nothing shows through it */
    }
    if (rounded_kind) {
        clamp_radii(clip->radii, rect[2], rect[3], radii);
    }
    rounded = rounded_kind && radii[0] + radii[1] + radii[2] + radii[3] > 0.0f;
    if (!rounded && s->world.b == 0.0f && s->world.c == 0.0f) {
        /* An axis-aligned rect: intersected exactly into the device-space box. */
        float xa = s->world.a * rect[0] + s->world.tx;
        float xb = s->world.a * (rect[0] + rect[2]) + s->world.tx;
        float ya = s->world.d * rect[1] + s->world.ty;
        float yb = s->world.d * (rect[1] + rect[3]) + s->world.ty;
        float b[4] = {fminf(xa, xb), fminf(ya, yb), fmaxf(xa, xb), fmaxf(ya, yb)};
        if (s->has_box) {
            b[0] = fmaxf(b[0], s->box[0]);
            b[1] = fmaxf(b[1], s->box[1]);
            b[2] = fminf(b[2], s->box[2]);
            b[3] = fminf(b[3], s->box[3]);
        }
        memcpy(s->box, b, sizeof b);
        s->has_box = true;
    } else if (s->chain_len >= RW_MAX_CLIP_CHAIN) {
        /* Past the chain's limit: its bounding box in device pixels, intersected into the box. */
        float xs[4], ys[4], b[4];
        int k;
        for (k = 0; k < 4; ++k) {
            const float lx = rect[0] + ((k == 1 || k == 2) ? rect[2] : 0.0f);
            const float ly = rect[1] + ((k >= 2) ? rect[3] : 0.0f);
            xs[k] = s->world.a * lx + s->world.c * ly + s->world.tx;
            ys[k] = s->world.b * lx + s->world.d * ly + s->world.ty;
        }
        b[0] = fminf(fminf(xs[0], xs[1]), fminf(xs[2], xs[3]));
        b[1] = fminf(fminf(ys[0], ys[1]), fminf(ys[2], ys[3]));
        b[2] = fmaxf(fmaxf(xs[0], xs[1]), fmaxf(xs[2], xs[3]));
        b[3] = fmaxf(fmaxf(ys[0], ys[1]), fmaxf(ys[2], ys[3]));
        if (s->has_box) {
            b[0] = fmaxf(b[0], s->box[0]);
            b[1] = fmaxf(b[1], s->box[1]);
            b[2] = fminf(b[2], s->box[2]);
            b[3] = fminf(b[3], s->box[3]);
        }
        memcpy(s->box, b, sizeof b);
        s->has_box = true;
        counts->clips_approximated++;
    } else {
        rw_clip *c = &records[(*record_count)++];
        c->m[0] = inv.a;
        c->m[1] = inv.b;
        c->m[2] = inv.c;
        c->m[3] = inv.d;
        c->t[0] = inv.tx;
        c->t[1] = inv.ty;
        c->t[2] = rounded ? (float)RW_FLAG_ROUNDED : 0.0f;
        c->t[3] = (float)s->chain;
        memcpy(c->rect, rect, sizeof rect);
        memcpy(c->radii, radii, sizeof radii);
        s->chain = *record_count;
        s->chain_len++;
    }
    if (s->has_box) {
        rw_clip *c = &records[(*record_count)++];
        if (!(s->box[2] > s->box[0]) || !(s->box[3] > s->box[1])) {
            return false; /* the rects do not meet */
        }
        c->m[0] = 1.0f;
        c->m[1] = 0.0f;
        c->m[2] = 0.0f;
        c->m[3] = 1.0f;
        c->t[0] = c->t[1] = c->t[2] = 0.0f;
        c->t[3] = (float)s->chain;
        c->rect[0] = s->box[0];
        c->rect[1] = s->box[1];
        c->rect[2] = s->box[2] - s->box[0];
        c->rect[3] = s->box[3] - s->box[1];
        memset(c->radii, 0, sizeof c->radii);
        s->head = *record_count;
    } else {
        s->head = s->chain;
    }
    return true;
}

/* Turns the snapshot into instances in painting order. */
/* The instances of a glyph run (decision 101): one quad per glyph with a bitmap, its texels in
 * the atlas (uploaded on first use). Returns how many were written. */
static uint32_t encode_glyphs(ax_render_wgpu *r, const ax_render_ir *ir,
                              const ax_ir_item_header *h, const rw_layer_state *s, rw_instance *out,
                              ax_render_counts *counts)
{
    const ax_ir_glyph_source *src =
        ir->struct_size >= offsetof(ax_render_ir, glyphs) + sizeof(void *) ? ir->glyphs : NULL;
    ax_ir_glyph_run_item run;
    ax_color enc;
    uint32_t k, n = 0, bucket;
    float inv_scale;
    memcpy(&run, h, sizeof run);
    if (src == NULL || r->atlas.view == NULL || !(run.raster_scale > 0.0f) ||
        !isfinite(run.raster_scale) || !finite4(&run.color.r) || !s->invertible) {
        counts->skipped_items++;
        return 0;
    }
    enc = ax_color_scale(run.color, s->opacity);
    if (!s->linear) {
        enc = ax_color_to_srgb_premultiplied(enc);
    }
    enc.a = clampf(enc.a, 0.0f, 1.0f);
    enc.r = clampf(enc.r, 0.0f, enc.a);
    enc.g = clampf(enc.g, 0.0f, enc.a);
    enc.b = clampf(enc.b, 0.0f, enc.a);
    if (enc.a <= 0.0f) {
        return 0;
    }
    bucket = RW_LUT_ROW(s->linear ? AX_IR_LUT_TEXT_LINEAR : AX_IR_LUT_TEXT_SRGB,
                        ax_ir_text_lut_bucket(run.color));
    inv_scale = 1.0f / run.raster_scale;
    for (k = 0; k < run.glyph_count; ++k) {
        ax_ir_glyph gl;
        const rw_atlas_entry *e;
        rw_instance *o;
        memcpy(&gl, (const uint8_t *)h + sizeof run + sizeof gl * k, sizeof gl);
        e = ax__rw_atlas_glyph(r, src, run.font_instance, gl.id, gl.bin);
        if (e == NULL) {
            continue; /* no bitmap (a space), or no room in the atlas */
        }
        o = &out[n++];
        o->rect[0] = (float)(gl.x + e->left) * inv_scale;
        o->rect[1] = (float)(gl.y - e->top) * inv_scale;
        o->rect[2] = (float)e->w * inv_scale;
        o->rect[3] = (float)e->h * inv_scale;
        o->radii[0] = (float)e->x;
        o->radii[1] = (float)e->y;
        o->radii[2] = (float)(e->x + e->w);
        o->radii[3] = (float)(e->y + e->h);
        o->color[0] = enc.r;
        o->color[1] = enc.g;
        o->color[2] = enc.b;
        o->color[3] = enc.a;
        o->xform[0] = s->clip.a;
        o->xform[1] = s->clip.b;
        o->xform[2] = s->clip.c;
        o->xform[3] = s->clip.d;
        o->xlate[0] = s->clip.tx;
        o->xlate[1] = s->clip.ty;
        o->xlate[2] = (float)(RW_FLAG_GLYPH | ((uint32_t)e->page << 8) | (bucket << 16));
        o->xlate[3] = (float)s->head;
        o->inv[0] = s->inv.a;
        o->inv[1] = s->inv.b;
        o->inv[2] = s->inv.c;
        o->inv[3] = s->inv.d;
    }
    return n;
}

/* `alt_ok`: linear layers can draw into the target's sRGB view; `lut_ok`: the correction tables
 * exist (shapes are drawn uncorrected without them). */
static ax_result encode(ax_render_wgpu *r, const ax_render_ir *ir, uint32_t width, uint32_t height,
                        float scale, bool alt_ok, bool lut_ok, rw_instance **out,
                        uint32_t *out_count, rw_clip **out_clips, uint32_t *out_clip_count,
                        rw_run **out_runs, uint32_t *out_run_count, ax_render_counts *counts)
{
    const bool correct_shapes = lut_ok && r->blend == AX_RENDER_BLEND_SRGB_CORRECTED;
    rw_run *runs;
    uint32_t run_count = 0;
    const ax_affine2 to_device = ax_affine2_scale(scale, scale);
    const ax_affine2 to_clip =
        ax_affine2_make(2.0f / (float)width, 0.0f, 0.0f, -2.0f / (float)height, -1.0f, 1.0f);
    const uint32_t flags_extra = (r->test_flags & RW_TEST_FOOTPRINT_FROM_DERIVATIVES) != 0
                                     ? RW_FLAG_FOOTPRINT_FROM_DERIVATIVES
                                     : 0u;
    rw_layer_state *ls;
    rw_instance *inst;
    rw_clip *clips;
    uint32_t max_items, n = 0, i, clip_count = 0;

    ax_arena_reset(&r->scratch);
    max_items = ir->items_size / (uint32_t)sizeof(ax_ir_item_header) + 1u;
    ls = AX_ARENA_NEW_ARRAY(&r->scratch, rw_layer_state, ir->layer_count);
    inst = AX_ARENA_NEW_ARRAY(&r->scratch, rw_instance, max_items);
    /* At most two records per layer (its clip and the box), and one so the buffer is never
     * empty. */
    clips = AX_ARENA_NEW_ARRAY(&r->scratch, rw_clip, 2u * ir->layer_count + 1u);
    runs = AX_ARENA_NEW_ARRAY(&r->scratch, rw_run, ir->layer_count + 1u);
    if (ls == NULL || inst == NULL || clips == NULL || runs == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_wgpu: out of memory");
    }

    for (i = 0; i < ir->layer_count; ++i) {
        const ax_ir_layer *l = ax_ir_layer_at(ir, i);
        rw_layer_state *s = &ls[i];
        const rw_layer_state *parent = l->parent != AX_IR_NO_LAYER ? &ls[l->parent] : NULL;
        ax_ir_item_iter it;
        const ax_ir_item_header *h;
        uint32_t index = 0;

        s->world = ax_affine2_mul(parent != NULL ? parent->world : to_device, l->transform);
        s->opacity = (parent != NULL ? parent->opacity : 1.0f) * clampf(l->opacity, 0.0f, 1.0f);
        s->skip = parent != NULL && parent->skip;
        if (s->skip) {
            continue;
        }
        s->chain = parent != NULL ? parent->chain : 0u;
        s->chain_len = parent != NULL ? parent->chain_len : 0u;
        s->has_box = parent != NULL && parent->has_box;
        if (s->has_box) {
            memcpy(s->box, parent->box, sizeof s->box);
        }
        s->head = parent != NULL ? parent->head : 0u;
        if (l->clip.kind == AX_IR_CLIP_RECT || l->clip.kind == AX_IR_CLIP_ROUNDED_RECT) {
            if (l->clip.kind == AX_IR_CLIP_ROUNDED_RECT && !finite4(l->clip.radii)) {
                s->skip = true; /* a clip it cannot apply: never draw what it should hide */
                counts->skipped_layers++;
                continue;
            }
            if (!apply_clip(s, &l->clip, clips, &clip_count, counts)) {
                s->skip = true; /* the clip hides everything */
                continue;
            }
        } else if (l->clip.kind != AX_IR_CLIP_NONE) {
            s->skip = true; /* path clips and unknown kinds */
            counts->skipped_layers++;
            continue;
        }
        if (l->blend_mode != AX_IR_BLEND_NORMAL || l->effects != AX_IR_NO_INDEX) {
            counts->neutralized++;
        }
        if (l->opacity < 1.0f && l->item_count + (l->subtree_end - i - 1u) > 1u) {
            counts->approximated++; /* group opacity needs an offscreen layer (render graph) */
        }
        s->linear = r->linear_blend || (parent != NULL && parent->linear);
        if ((l->flags & AX_IR_LAYER_LINEAR_BLEND) != 0) {
            if (r->linear_blend || alt_ok) {
                s->linear = true;
                counts->linear_layers++;
            } else if (!s->linear) {
                counts->linear_layers_unsupported++; /* no sRGB view: blended in sRGB space */
            }
        }
        {
            /* A layer drawn into the other view starts a run. */
            const bool alt = s->linear && !r->linear_blend;
            if (run_count == 0 || runs[run_count - 1u].alt != alt) {
                if (run_count != 0 && runs[run_count - 1u].first == n) {
                    runs[run_count - 1u].alt = alt; /* still empty: reuse it */
                } else {
                    runs[run_count].first = n;
                    runs[run_count].count = 0;
                    runs[run_count].alt = alt;
                    run_count++;
                }
            }
        }
        s->clip = ax_affine2_mul(to_clip, s->world);
        s->invertible =
            ax_affine2_invert(s->world, &s->inv) &&
            isfinite(s->clip.a + s->clip.b + s->clip.c + s->clip.d + s->clip.tx + s->clip.ty);

        it = ax_ir_items(ir, l);
        while ((h = ax_ir_item_next(&it)) != NULL) {
            float rect[4], radii[4] = {0, 0, 0, 0};
            ax_color color, enc;
            rw_instance *o;
            uint32_t flags = flags_extra;
            ++index;
            if (h->type == AX_IR_ITEM_GLYPH_RUN) {
                n += encode_glyphs(r, ir, h, s, inst + n, counts);
                continue;
            }
            if (h->type == AX_IR_ITEM_RECT) {
                ax_ir_rect_item item;
                memcpy(&item, h, sizeof item);
                memcpy(rect, &item.rect, sizeof rect);
                color = item.color;
            } else if (h->type == AX_IR_ITEM_ROUNDED_RECT) {
                ax_ir_rounded_rect_item item;
                memcpy(&item, h, sizeof item);
                memcpy(rect, &item.rect, sizeof rect);
                memcpy(radii, item.radii, sizeof radii);
                color = item.color;
                if (!finite4(radii)) {
                    counts->invalid_items++;
                    continue;
                }
                flags |= RW_FLAG_ROUNDED;
            } else {
                counts->unknown_items++;
                if ((h->flags & AX_IR_ITEM_REQUIRED) != 0) {
                    counts->skipped_items += l->item_count - index;
                    break;
                }
                continue;
            }
            if (!s->invertible || !finite4(rect) || !(rect[2] > 0.0f) || !(rect[3] > 0.0f) ||
                !finite4(&color.r)) {
                counts->invalid_items++;
                continue;
            }
            enc = ax_color_scale(color, s->opacity);
            if (!s->linear) {
                enc = ax_color_to_srgb_premultiplied(enc);
            }
            if (correct_shapes && !s->linear) {
                flags |= RW_FLAG_SHAPE_LUT |
                         (RW_LUT_ROW(AX_IR_LUT_SHAPE_SRGB, ax_ir_text_lut_bucket(color)) << 16);
            }
            enc.a = clampf(enc.a, 0.0f, 1.0f);
            enc.r = clampf(enc.r, 0.0f, enc.a);
            enc.g = clampf(enc.g, 0.0f, enc.a);
            enc.b = clampf(enc.b, 0.0f, enc.a);
            if (enc.a <= 0.0f) {
                continue; /* invisible with source-over blending */
            }
            if (flags & RW_FLAG_ROUNDED) {
                float clamped[4];
                clamp_radii(radii, rect[2], rect[3], clamped);
                memcpy(radii, clamped, sizeof radii);
                if (radii[0] + radii[1] + radii[2] + radii[3] <= 0.0f) {
                    flags &= ~RW_FLAG_ROUNDED;
                }
            }
            o = &inst[n++];
            memcpy(o->rect, rect, sizeof rect);
            memcpy(o->radii, radii, sizeof radii);
            o->color[0] = enc.r;
            o->color[1] = enc.g;
            o->color[2] = enc.b;
            o->color[3] = enc.a;
            o->xform[0] = s->clip.a;
            o->xform[1] = s->clip.b;
            o->xform[2] = s->clip.c;
            o->xform[3] = s->clip.d;
            o->xlate[0] = s->clip.tx;
            o->xlate[1] = s->clip.ty;
            o->xlate[2] = (float)flags;
            o->xlate[3] = (float)s->head;
            o->inv[0] = s->inv.a;
            o->inv[1] = s->inv.b;
            o->inv[2] = s->inv.c;
            o->inv[3] = s->inv.d;
        }
    }
    if (clip_count == 0) {
        memset(&clips[0], 0, sizeof clips[0]); /* a storage binding is never empty */
        clip_count = 1;
    }
    counts->instances = n;
    for (i = 0; i < run_count; ++i) {
        runs[i].count = (i + 1u < run_count ? runs[i + 1u].first : n) - runs[i].first;
    }
    *out_runs = runs;
    *out_run_count = run_count;
    *out = inst;
    *out_count = n;
    *out_clips = clips;
    *out_clip_count = clip_count;
    return AX_OK;
}

/* ---- drawing ----------------------------------------------------------------------------------
 */

/* The bind group for a pipeline's layout: clips, atlas, LUT, sampler. */
static WGPUBindGroup make_group(ax_render_wgpu *r, WGPURenderPipeline pipeline,
                                const ax_gpu_upload_slice *clip_slice)
{
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entries[4] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    WGPUBindGroupDescriptor bgd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    WGPUBindGroup group;
    entries[0].binding = 0;
    entries[0].buffer = clip_slice->buffer;
    entries[0].offset = clip_slice->offset;
    entries[0].size = clip_slice->size;
    entries[1].binding = 1;
    entries[1].textureView = r->atlas.view;
    entries[2].binding = 2;
    entries[2].textureView = r->atlas.lut_view;
    entries[3].binding = 3;
    entries[3].sampler = r->atlas.sampler;
    bgd.layout = bgl;
    bgd.entryCount = 4;
    bgd.entries = entries;
    group = wgpuDeviceCreateBindGroup(ax_gpu_device(r->gpu), &bgd);
    wgpuBindGroupLayoutRelease(bgl);
    return group;
}

ax_result ax_render_wgpu_draw_ir(ax_render_wgpu *r, const ax_render_wgpu_draw *draw,
                                 const ax_render_ir *ir, ax_frame_status *out_status,
                                 ax_render_counts *out_counts)
{
    WGPUDevice device = ax_gpu_device(r->gpu);
    WGPUCommandEncoder encoder;
    WGPUCommandBuffer commands;
    ax_render_counts counts;
    ax_color bg;
    rw_instance *inst = NULL;
    rw_clip *clips = NULL;
    rw_run *runs = NULL;
    uint32_t count = 0, clip_count = 0, run_count = 0, i;
    /* The target's sRGB view, for layers that blend in linear light (issue #4's option 3). */
    const bool has_alt = draw->struct_size >= offsetof(ax_render_wgpu_draw, srgb_format) +
                                                  sizeof(WGPUTextureFormat) &&
                         draw->srgb_target != NULL && !r->linear_blend;
    bool ready, alt_ready = false;
    ax_result res;
    ax_gpu_upload_slice slice, clip_slice;
    WGPUBindGroup groups[2] = {NULL, NULL};

    AX_ASSERT(draw != NULL && draw->struct_size >= offsetof(ax_render_wgpu_draw, srgb_target));
    memset(&counts, 0, sizeof counts);
    memset(&slice, 0, sizeof slice);
    memset(&clip_slice, 0, sizeof clip_slice);
    if (ir != NULL && ir->version_major != AX_IR_VERSION_MAJOR) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_render_wgpu: Render IR %u.%u, reads %u.x",
                            ir->version_major, ir->version_minor, AX_IR_VERSION_MAJOR);
    }
    ax_render_wgpu_prepare(r, draw->format);
    ready = ax_render_wgpu_ready(r, draw->format);
    if (has_alt) {
        ax_render_wgpu_prepare(r, draw->srgb_format);
        alt_ready = ax_render_wgpu_ready(r, draw->srgb_format);
    }

    if (ready && ir != NULL) {
        const uint64_t t0 = ax_time_monotonic_ns();
        uint64_t dt;
        /* Without its textures, glyph runs are skipped and shapes are not corrected. */
        const bool lut_ok = ax__rw_atlas_begin(r);
        res = encode(r, ir, draw->width, draw->height, draw->scale, alt_ready, lut_ok, &inst,
                     &count, &clips, &clip_count, &runs, &run_count, &counts);
        if (res != AX_OK) {
            return res;
        }
        dt = ax_time_monotonic_ns() - t0;
        r->stats.encode_ns_total += dt;
        if (dt > r->stats.encode_ns_max) {
            r->stats.encode_ns_max = dt;
        }
    }

    if (count != 0) {
        /* Each draw is submitted before the next one writes, so the ring restarts per draw. */
        res = ax_gpu_upload_ring_begin_frame(r->ring);
        if (res == AX_OK) {
            res = ax_gpu_upload_ring_push(r->ring, inst, (uint64_t)count * sizeof(rw_instance), 16,
                                          &slice);
        }
        if (res == AX_OK) {
            /* 256: WebGPU's minimum guaranteed storage-buffer offset alignment. */
            res = ax_gpu_upload_ring_push(r->ring, clips, (uint64_t)clip_count * sizeof(rw_clip),
                                          256, &clip_slice);
        }
        if (res != AX_OK) {
            count = 0;
            run_count = 0;
        }
    }
    if (count == 0) {
        run_count = 0;
    }

    bg = ir != NULL ? ir->background : draw->background;
    if (!r->linear_blend) {
        bg = ax_color_to_srgb_premultiplied(bg);
    }
    encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    /* One pass per run (a run's instances share a view), the first clearing the target; at least
     * one pass, for the clear. */
    for (i = 0; i == 0 || i < run_count; ++i) {
        const bool alt = i < run_count && runs[i].alt;
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        WGPURenderPassEncoder pass;
        color.view = alt ? draw->srgb_target : draw->target;
        color.loadOp = i == 0 ? WGPULoadOp_Clear : WGPULoadOp_Load;
        color.storeOp = WGPUStoreOp_Store;
        if (i == 0 && alt) {
            /* The sRGB view decodes what it clears to: the background in linear light. */
            const ax_color lin = ir != NULL ? ir->background : draw->background;
            color.clearValue.r = clampf(lin.r, 0.0f, 1.0f);
            color.clearValue.g = clampf(lin.g, 0.0f, 1.0f);
            color.clearValue.b = clampf(lin.b, 0.0f, 1.0f);
            color.clearValue.a = clampf(lin.a, 0.0f, 1.0f);
        } else {
            color.clearValue.r = clampf(bg.r, 0.0f, 1.0f);
            color.clearValue.g = clampf(bg.g, 0.0f, 1.0f);
            color.clearValue.b = clampf(bg.b, 0.0f, 1.0f);
            color.clearValue.a = clampf(bg.a, 0.0f, 1.0f);
        }
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &color;
        pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        if (i < run_count && runs[i].count != 0) {
            WGPURenderPipeline pipeline =
                find_pipeline(r, alt ? draw->srgb_format : draw->format)->pipeline;
            if (groups[alt ? 1 : 0] == NULL) {
                groups[alt ? 1 : 0] = make_group(r, pipeline, &clip_slice);
            }
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, groups[alt ? 1 : 0], 0, NULL);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, slice.buffer, slice.offset, slice.size);
            wgpuRenderPassEncoderDraw(pass, 4, runs[i].count, 0, runs[i].first);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    for (i = 0; i < 2; ++i) {
        if (groups[i] != NULL) {
            wgpuBindGroupRelease(groups[i]);
        }
    }
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(ax_gpu_queue(r->gpu), 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    r->stats.instances_last = counts.instances;
    if (ready && ir != NULL) {
        r->stats.frames++;
    } else {
        r->stats.clear_only_frames++;
    }
    if (out_status != NULL) {
        *out_status = (ready && ir != NULL) ? AX_FRAME_PRESENTED : AX_FRAME_CLEARED;
    }
    if (out_counts != NULL) {
        *out_counts = counts;
    }
    return AX_OK;
}

void ax_render_wgpu_get_stats(ax_render_wgpu *r, ax_render_wgpu_stats *out, bool reset)
{
    uint32_t size;
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_render_wgpu_stats));
    size = out->struct_size;
    *out = r->stats;
    out->struct_size = size;
    if (reset) {
        const uint64_t created = r->stats.pipelines_created;
        const uint64_t ready_ns = r->stats.pipeline_ready_ns;
        memset(&r->stats, 0, sizeof r->stats);
        r->stats.struct_size = sizeof r->stats;
        r->stats.pipelines_created = created;
        r->stats.pipeline_ready_ns = ready_ns;
    }
}

/* ---- test hooks -------------------------------------------------------------------------------
 */

void ax__render_wgpu_test_set_flags(ax_render_wgpu *r, uint32_t flags)
{
    r->test_flags = flags;
}

bool ax__render_wgpu_test_wait_ready(ax_render_wgpu *r, WGPUTextureFormat format,
                                     uint64_t timeout_ns)
{
    const uint64_t start = ax_time_monotonic_ns();
    ax_render_wgpu_prepare(r, format);
    for (;;) {
        rw_pipeline *p = find_pipeline(r, format);
        if (p != NULL && p->pipeline != NULL && p->generation == ax_gpu_device_generation(r->gpu)) {
            return true;
        }
        if (p != NULL && p->pending) {
            (void)ax_gpu_wait(r->gpu, p->future, &p->done, 100000000ull);
        } else {
            (void)ax_gpu_poll(r->gpu);
            ax_render_wgpu_prepare(r, format);
        }
        if (ax_time_monotonic_ns() - start >= timeout_ns) {
            return false;
        }
    }
}
