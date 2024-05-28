#pragma once

#include "../standard_ui_system.h"

struct geometry;

typedef struct sui_panel_internal_data {
    vec4 rect;
    vec4 colour;
    struct geometry* g;
    u32 instance_id;
    u64 frame_number;
    u8 draw_index;
    b8 is_dirty;
} sui_panel_internal_data;

KAPI b8 sui_panel_control_create(standard_ui_state* state, const char* name, vec2 size, vec4 colour, struct sui_control* out_control);
KAPI void sui_panel_control_destroy(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_panel_control_load(standard_ui_state* state, struct sui_control* self);
KAPI void sui_panel_control_unload(standard_ui_state* state, struct sui_control* self);

KAPI b8 sui_panel_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_panel_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI vec2 sui_panel_size(standard_ui_state* state, struct sui_control* self);
KAPI b8 sui_panel_control_resize(standard_ui_state* state, struct sui_control* self, vec2 new_size);
