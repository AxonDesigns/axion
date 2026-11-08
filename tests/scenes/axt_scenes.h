/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Render IR scenes shared by the software renderer's tests, the GPU renderers' comparison against
 * it and the goldens: the coverage scenes of decision 56 and the antialiasing torture scenes
 * ("Antialiasing and image quality", Verification). Built only from deterministic math, so the
 * same scene is the same snapshot on every platform.
 */
#ifndef AXT_SCENES_H
#define AXT_SCENES_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/render/ax_render_ir.h"

/* axt_scene.flags: what ax_render_wgpu cannot draw exactly yet. */
#define AXT_SCENE_CLIPS   0x1u /* clipped layers (ax_render_wgpu skips them) */
#define AXT_SCENE_GROUPS  0x2u /* group opacity (ax_render_wgpu approximates it) */
#define AXT_SCENE_LINEAR  0x4u /* AX_IR_LAYER_LINEAR_BLEND (ax_render_wgpu blends in sRGB) */
#define AXT_SCENE_TORTURE 0x8u /* an antialiasing torture scene */

typedef struct axt_scene {
    const char *name;
    float logical_width, logical_height;
    uint32_t width, height; /* pixels */
    float scale;
    uint32_t flags;
    /* Records the scene into b (begin to end is the caller's: begin is done, end follows). */
    void (*build)(ax_ir_builder *b);
} axt_scene;

/* Every scene; *count gets their number. */
const axt_scene *axt_scenes(uint32_t *count);
const axt_scene *axt_scene_find(const char *name);

/* Records a scene: begin, build, end. NULL on failure. */
const ax_render_ir *axt_scene_record(const axt_scene *s, ax_ir_builder *b);

/* A layer turning by `degrees` about (x, y), with a w x h rect centred on it. */
void axt_rotated_rect(ax_ir_builder *b, float degrees, float x, float y, float w, float h,
                      ax_color c);

#endif
