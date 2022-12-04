#pragma once

#include "math/math_types.h"
#include "resources/resource_types.h"
#include "renderer/renderer_types.inl"

struct font_data;

typedef enum ui_text_type {
    UI_TEXT_TYPE_BITMAP,
    UI_TEXT_TYPE_SYSTEM
} ui_text_type;

typedef struct ui_text {
    u32 unique_id;
    ui_text_type type;
    struct font_data* data;
    renderbuffer vertex_buffer;
    renderbuffer index_buffer;
    char* text;
    transform transform;
    u32 instance_id;
    u64 render_frame_number;
} ui_text;

KAPI b8 ui_text_create(ui_text_type type, const char* font_name, u16 font_size, const char* text_content, ui_text* out_text);
KAPI void ui_text_destroy(ui_text* text);

KAPI void ui_text_set_position(ui_text* u_text, vec3 position);
KAPI void ui_text_set_text(ui_text* u_text, const char* text);

KAPI void ui_text_draw(ui_text* u_text);
