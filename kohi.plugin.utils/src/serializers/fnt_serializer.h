
#pragma once

#include <assets/kasset_types.h>
#include <defines.h>
#include <math/math_types.h>

typedef struct fnt_source_asset {
    char* face_name;
    u32 size;
    b8 bold;
    b8 italic;
    b8 uniode;
    u32 line_height;
    u32 baseline;
    u32 atlas_size_x;
    u32 atlas_size_y;

    u32 glyph_count;
    kasset_bitmap_font_glyph* glyphs;

    u32 kerning_count;
    kasset_bitmap_font_kerning* kernings;

    u32 page_count;
    kasset_bitmap_font_page* pages;
} fnt_source_asset;

KAPI b8 fnt_serializer_serialize(const fnt_source_asset* source_asset, const char** out_file_text);

/**
 * Attempts to deserialize the contents of FNT bitmap font text file.
 *
 * @param mtl_file_text The fnt file content. Optional.
 * @param out_fnt_source_asset A pointer to hold the deserialized bitmap font data. Optional unless fnt_file_text is provided, then required.
 * @return True on success; otherwise false.
 */
KAPI b8 fnt_serializer_deserialize(const char* fnt_file_text, fnt_source_asset* out_fnt_source_asset);
