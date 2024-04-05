#pragma once

#include "../standard_ui_system.h"

#include "math/nine_slice.h"

typedef struct sui_button_internal_data {
    vec2i size;
    vec4 colour;
    nine_slice nslice;
    u32 instance_id;
    u64 frame_number;
    u8 draw_index;
} sui_button_internal_data;

KAPI b8 sui_button_control_create(const char* name, struct sui_control* out_control);
KAPI void sui_button_control_destroy(struct sui_control* self);
KAPI b8 sui_button_control_height_set(struct sui_control* self, i32 width);

KAPI b8 sui_button_control_load(struct sui_control* self);
KAPI void sui_button_control_unload(struct sui_control* self);

KAPI b8 sui_button_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_button_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI void sui_button_on_mouse_out(struct sui_control* self, struct sui_mouse_event event);
KAPI void sui_button_on_mouse_over(struct sui_control* self, struct sui_mouse_event event);
KAPI void sui_button_on_mouse_down(struct sui_control* self, struct sui_mouse_event event);
KAPI void sui_button_on_mouse_up(struct sui_control* self, struct sui_mouse_event event);
