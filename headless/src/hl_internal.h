/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The headless host's internals: ax_render_wgpu offscreen (gpu.c with the gpu module, no_gpu.c
 * without it).
 */
#ifndef AX_HEADLESS_INTERNAL_H
#define AX_HEADLESS_INTERNAL_H

#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/base/ax_thread.h>
#include <axion/headless/ax_headless.h>
#include <axion/render/ax_render_ir.h>

typedef struct hl_gpu hl_gpu;

/* AX_OK when a GPU renderer can be created here (AX_ERR_UNSUPPORTED without one). App thread. */
ax_result ax__hl_gpu_probe(void);

/* Render side (the thread that draws). */
ax_result ax__hl_gpu_create(const ax_allocator *allocator, hl_gpu **out);
void ax__hl_gpu_destroy(hl_gpu *g); /* NULL is fine */

/* Draws ir (or the background) at w x h pixels and reads it back into rgba (RGBA8,
 * premultiplied, rows packed). */
ax_result ax__hl_gpu_draw(hl_gpu *g, const ax_render_ir *ir, uint32_t w, uint32_t h, float scale,
                          ax_color background, uint8_t *rgba);

/* semantics.c: a surface's semantic tree (decision 108), filled by its accessibility sink on the
 * scene thread, read by the testing API's queries under `lock`. */
typedef struct hl_tree hl_tree;
hl_tree *ax__hl_tree_create(const ax_allocator *allocator, ax_mutex *lock);
void ax__hl_tree_destroy(hl_tree *t); /* NULL is fine */
void ax__hl_tree_update(void *user, const struct ax_a11y_update *update); /* the sink */
/* headless.c: the tree of root's surface, or NULL. */
hl_tree *ax__hl_tree_of(ax_headless *h, ax_node root);

#endif
