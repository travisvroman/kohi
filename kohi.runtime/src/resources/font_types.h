#pragma once

#include <defines.h>

#include "renderer/renderer_types.h"

typedef struct font_glyph {
    i32 codepoint;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    i16 x_offset;
    i16 y_offset;
    i16 x_advance;
    u8 page_id;
} font_glyph;

typedef struct font_kerning {
    i32 codepoint_0;
    i32 codepoint_1;
    i16 amount;
} font_kerning;

typedef enum font_type {
    FONT_TYPE_BITMAP,
    FONT_TYPE_SYSTEM
} font_type;

typedef struct font_data {
    font_type type;
    char face[256];
    u32 size;
    i32 line_height;
    i32 baseline;
    i32 atlas_size_x;
    i32 atlas_size_y;
    kresource_texture atlas_texture;
    kresource_texture_map atlas;
    u32 glyph_count;
    font_glyph* glyphs;
    u32 kerning_count;
    font_kerning* kernings;
    f32 tab_x_advance;
    u32 internal_data_size;
    void* internal_data;
} font_data;

typedef struct bitmap_font_page {
    i8 id;
    char file[256];
} bitmap_font_page;

typedef struct bitmap_font_resource_data {
    font_data data;
    u32 page_count;
    bitmap_font_page* pages;
} bitmap_font_resource_data;

typedef struct system_font_face {
    char name[256];
} system_font_face;

typedef struct system_font_resource_data {
    // darray
    system_font_face* fonts;
    u64 binary_size;
    void* font_binary;
} system_font_resource_data;
