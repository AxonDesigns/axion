/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window: the scene recorded into the Render IR (scene.c).
 */
#ifndef M0_SCENE_H
#define M0_SCENE_H

#include <stdbool.h>

#include "axion/render/ax_render_ir.h"
#include "m0_window.h"

/* Records one frame of the scene for a window of w x h logical units at time t (seconds, the
 * predicted present time). csd: the window draws its own title band and borders. The builder has
 * been begun; the caller ends or publishes it. */
void m0_scene_record(ax_ir_builder *b, float w, float h, double t, bool csd);

#endif
