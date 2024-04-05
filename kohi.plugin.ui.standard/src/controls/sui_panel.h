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

KAPI b8 sui_panel_control_create(const char* name, vec2 size, vec4 colour, struct sui_control* out_control);
KAPI void sui_panel_control_destroy(struct sui_control* self);

KAPI b8 sui_panel_control_load(struct sui_control* self);
KAPI void sui_panel_control_unload(struct sui_control* self);

KAPI b8 sui_panel_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_panel_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI vec2 sui_panel_size(struct sui_control* self);
KAPI b8 sui_panel_control_resize(struct sui_control* self, vec2 new_size);
