#pragma once

#include "math/math_types.h"
#include "resources/resource_types.h"
#include "renderer/renderer_types.h"

struct font_data;

typedef enum ui_text_type {
    UI_TEXT_TYPE_BITMAP,
    UI_TEXT_TYPE_SYSTEM
} ui_text_type;

typedef struct ui_text {
    char* name;
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

KAPI b8 ui_text_create(const char* name, ui_text_type type, const char* font_name, u16 font_size, const char* text_content, ui_text* out_text);
KAPI void ui_text_destroy(ui_text* text);

/**
 * @brief Sets the position on the given UI text object.
 *
 * @param u_text A pointer to the UI text whose text will be set.
 * @param text The position to be set.
 */
KAPI void ui_text_position_set(ui_text* u_text, vec3 position);
/**
 * @brief Sets the text on the given UI text object.
 *
 * @param u_text A pointer to the UI text whose text will be set.
 * @param text The text to be set.
 */
KAPI void ui_text_text_set(ui_text* u_text, const char* text);
/** 
 * @brief Draws the given UI text.
 *
 * @param u_text A pointer to the text to be drawn.
 */
KAPI void ui_text_draw(ui_text* u_text);

