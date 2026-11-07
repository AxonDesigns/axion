/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Render IR: the readable text dump.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "axion/render/ax_render_ir.h"

typedef struct out {
    ax_ir_write_fn write;
    void *ctx;
} out;

static void emit(out *o, unsigned indent, const char *fmt, ...) AX_PRINTF_LIKE(3, 4);

static void emit(out *o, unsigned indent, const char *fmt, ...)
{
    char line[512];
    va_list args;
    int n;
    unsigned i;
    for (i = 0; i < indent; ++i) {
        o->write(o->ctx, "  ", 2);
    }
    va_start(args, fmt);
    n = vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof line) {
        n = (int)sizeof line - 1;
    }
    o->write(o->ctx, line, (size_t)n);
    o->write(o->ctx, "\n", 1);
}

static const char *blend_name(uint32_t mode)
{
    static const char *const names[] = {"normal", "multiply", "screen", "overlay",
                                        "darken", "lighten",  "plus"};
    return mode < AX_COUNTOF(names) ? names[mode] : NULL;
}

static void dump_layer(out *o, const ax_render_ir *ir, uint32_t index, unsigned depth)
{
    const ax_ir_layer *l = ax_ir_layer_at(ir, index);
    const ax_affine2 t = l->transform;
    char props[384];
    size_t used = 0;
    ax_ir_item_iter it;
    const ax_ir_item_header *h;

#define PROP(...)                                                                           \
    do {                                                                                    \
        int n_ = snprintf(props + used, sizeof props - used, __VA_ARGS__);                  \
        if (n_ > 0) {                                                                       \
            used = used + (size_t)n_ < sizeof props ? used + (size_t)n_ : sizeof props - 1; \
        }                                                                                   \
    } while (0)

    props[0] = '\0';
    if (index == 0) {
        PROP(" root");
    }
    if (l->flags & AX_IR_LAYER_IMPLICIT) {
        PROP(" implicit");
    }
    if (l->flags & AX_IR_LAYER_LINEAR_BLEND) {
        PROP(" linear-blend");
    }
    if (t.a != 1.0f || t.b != 0.0f || t.c != 0.0f || t.d != 1.0f || t.tx != 0.0f || t.ty != 0.0f) {
        PROP(" transform (%g %g %g %g %g %g)", (double)t.a, (double)t.b, (double)t.c, (double)t.d,
             (double)t.tx, (double)t.ty);
    }
    if (l->clip.kind == AX_IR_CLIP_RECT || l->clip.kind == AX_IR_CLIP_ROUNDED_RECT) {
        PROP(" clip %s (%g %g %g %g)", l->clip.kind == AX_IR_CLIP_RECT ? "rect" : "rounded_rect",
             (double)l->clip.rect.x, (double)l->clip.rect.y, (double)l->clip.rect.w,
             (double)l->clip.rect.h);
        if (l->clip.kind == AX_IR_CLIP_ROUNDED_RECT) {
            PROP(" radii (%g %g %g %g)", (double)l->clip.radii[0], (double)l->clip.radii[1],
                 (double)l->clip.radii[2], (double)l->clip.radii[3]);
        }
    } else if (l->clip.kind == AX_IR_CLIP_PATH) {
        PROP(" clip path %u", l->clip.path);
    } else if (l->clip.kind != AX_IR_CLIP_NONE) {
        PROP(" clip kind %u", l->clip.kind);
    }
    if (l->opacity != 1.0f) {
        PROP(" opacity %g", (double)l->opacity);
    }
    if (l->blend_mode != AX_IR_BLEND_NORMAL) {
        if (blend_name(l->blend_mode) != NULL) {
            PROP(" blend %s", blend_name(l->blend_mode));
        } else {
            PROP(" blend %u", l->blend_mode);
        }
    }
    if (l->effects != AX_IR_NO_INDEX) {
        PROP(" effects %u", l->effects);
    }
#undef PROP
    emit(o, depth, "layer %u%s items %u", index, props, l->item_count);

    it = ax_ir_items(ir, l);
    while ((h = ax_ir_item_next(&it)) != NULL) {
        if (h->type == AX_IR_ITEM_RECT) {
            ax_ir_rect_item r;
            memcpy(&r, h, sizeof r);
            emit(o, depth + 1, "rect (%g %g %g %g) color (%g %g %g %g)", (double)r.rect.x,
                 (double)r.rect.y, (double)r.rect.w, (double)r.rect.h, (double)r.color.r,
                 (double)r.color.g, (double)r.color.b, (double)r.color.a);
        } else if (h->type == AX_IR_ITEM_ROUNDED_RECT) {
            ax_ir_rounded_rect_item r;
            memcpy(&r, h, sizeof r);
            emit(o, depth + 1, "rounded_rect (%g %g %g %g) radii (%g %g %g %g) color (%g %g %g %g)",
                 (double)r.rect.x, (double)r.rect.y, (double)r.rect.w, (double)r.rect.h,
                 (double)r.radii[0], (double)r.radii[1], (double)r.radii[2], (double)r.radii[3],
                 (double)r.color.r, (double)r.color.g, (double)r.color.b, (double)r.color.a);
        } else {
            emit(o, depth + 1, "item type %u flags 0x%x size %u", h->type, h->flags, h->size);
        }
    }
}

void ax_ir_dump(const ax_render_ir *ir, ax_ir_write_fn write, void *ctx)
{
    out o;
    uint32_t stack[64];
    unsigned depth = 0;
    uint32_t i;

    o.write = write;
    o.ctx = ctx;
    emit(&o, 0, "render_ir %u.%u size %gx%g time_ns %llu seq %llu background (%g %g %g %g)",
         ir->version_major, ir->version_minor, (double)ir->logical_width,
         (double)ir->logical_height, (unsigned long long)ir->frame_time_ns,
         (unsigned long long)ir->sequence, (double)ir->background.r, (double)ir->background.g,
         (double)ir->background.b, (double)ir->background.a);
    if ((ir->damage_flags & AX_IR_DAMAGE_FULL) != 0) {
        emit(&o, 0, "damage full");
    } else {
        for (i = 0; i < ir->damage_count; ++i) {
            emit(&o, 0, "damage (%g %g %g %g)", (double)ir->damage[i].x, (double)ir->damage[i].y,
                 (double)ir->damage[i].w, (double)ir->damage[i].h);
        }
    }
    for (i = 0; i < ir->layer_count; ++i) {
        while (depth > 0 && ax_ir_layer_at(ir, stack[depth - 1])->subtree_end <= i) {
            --depth;
        }
        dump_layer(&o, ir, i, depth);
        if (ax_ir_layer_at(ir, i)->subtree_end > i + 1 && depth < AX_COUNTOF(stack)) {
            stack[depth++] = i;
        }
    }
}
