#pragma once

#include "core_render_types.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/math_types.h"

KAPI kui_control kui_image_box_control_create (kui_state *state, const char *name, vec2i size);
KAPI void kui_image_box_control_destroy (kui_state *state, kui_control *self);

KAPI void kui_image_box_control_height_set (kui_state *state, kui_control self, i32 height);
KAPI void kui_image_box_control_width_set (kui_state *state, kui_control self, i32 width);
KAPI b8 kui_image_box_control_texture_set_by_name (kui_state *state, kui_control self, kname image_asset_name, kname image_asset_package_name);
KAPI b8 kui_image_box_control_texture_set (kui_state *state, kui_control self, ktexture texture);
KAPI ktexture kui_image_box_control_texture_get (kui_state *state, const kui_control self);
KAPI void kui_image_box_control_set_rect (kui_state *state, kui_control self, rect_2di rect);

KAPI b8 kui_image_box_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_image_box_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
