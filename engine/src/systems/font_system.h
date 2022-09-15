#pragma once

#include "math/math_types.h"
#include "renderer/renderer_types.inl"

typedef struct system_font_config {
    char* name;
    u16 default_size;
    char* resource_name;
} system_font_config;

typedef struct bitmap_font_config {
    char* name;
    u16 size;
    char* resource_name;
} bitmap_font_config;

typedef struct font_system_config {
    u8 default_system_font_count;
    system_font_config* system_font_configs;
    u8 default_bitmap_font_count;
    bitmap_font_config* bitmap_font_configs;
    u8 max_system_font_count;
    u8 max_bitmap_font_count;
    b8 auto_release;
} font_system_config;

struct ui_text;

b8 font_system_initialize(u64* memory_requirement, void* memory, font_system_config* config);
void font_system_shutdown(void* memory);

b8 font_system_load_system_font(system_font_config* config);
b8 font_system_load_bitmap_font(bitmap_font_config* config);

/**
 * @brief Attempts to acquire a font of the given name and assign it to the given ui_text.
 *
 * @param font_name The name of the font to acquire. Must be an already loaded font.
 * @param font_size The font size. Ignored for bitmap fonts.
 * @param text A pointer to the text object for which to acquire the font.
 * @return True on success; otherwise false.
 */
b8 font_system_acquire(const char* font_name, u16 font_size, struct ui_text* text);

/**
 * @brief Releases references to the font held by the provided ui_text.
 *
 * @param text A pointer to the text object to release the font from.
 * @return True on success; otherwise false.
 */
b8 font_system_release(struct ui_text* text);

b8 font_system_verify_atlas(font_data* font, const char* text);
