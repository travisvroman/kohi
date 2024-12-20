#include "fnt_serializer.h"

#include <containers/darray.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include <stdio.h> // sscanf

static b8 import_fnt_file(const char* fnt_file_text, fnt_source_asset* out_mtl_source_asset);

b8 fnt_serializer_serialize(const fnt_source_asset* source_asset, const char** out_file_text) {
    KASSERT_MSG(false, "Not yet implemented");
    return false;
}

b8 fnt_serializer_deserialize(const char* fnt_file_text, fnt_source_asset* out_fnt_source_asset) {
    if (!fnt_file_text || !out_fnt_source_asset) {
        KERROR("fnt_serializer_deserialize requires valid pointers to fnt_file_text and out_fnt_source_asset.");
        return false;
    }

    return import_fnt_file(fnt_file_text, out_fnt_source_asset);
}

#define VERIFY_LINE(line_type, line_num, expected, actual)                                                                                     \
    if (actual != expected) {                                                                                                                  \
        KERROR("Error in file format reading type '%s', line %u. Expected %d element(s) but read %d.", line_type, line_num, expected, actual); \
        return false;                                                                                                                          \
    }

static b8 import_fnt_file(const char* fnt_file_text, fnt_source_asset* out_asset) {
    KDEBUG("Importing source bitmap font .fnt file ...");

    kzero_memory(out_asset, sizeof(fnt_source_asset));
    char* line = 0;
    char line_buffer[512];
    char* p = &line_buffer[0];
    u32 line_length = 0;
    u8 addl_advance = 0; // To skip \n, or \r\n, etc.
    u32 line_num = 0;
    u32 glyphs_read = 0;
    u8 pages_read = 0;
    u32 kernings_read = 0;
    u32 start_from = 0;
    while (true) {
        start_from += line_length + addl_advance;
        ++line_num; // Increment the number right away, since most text editors' line display is 1-indexed.
        kzero_memory(line_buffer, 512);
        if (!string_line_get(fnt_file_text, 511, start_from, &p, &line_length, &addl_advance)) {
            break;
        }

        // Trim the line first.
        line = string_trim(line_buffer);

        // Skip blank lines.
        if (line_length < 1) {
            continue;
        }

        char first_char = line[0];
        switch (first_char) {
        case 'i': {
            // 'info' line

            // NOTE: only extract the face and size, ignore the rest.
            char face_name_buf[512] = {0};
            kzero_memory(face_name_buf, sizeof(char) * 512);
            i32 elements_read = sscanf(
                line_buffer,
                "info face=\"%[^\"]\" size=%u",
                face_name_buf,
                &out_asset->size);
            VERIFY_LINE("info", line_num, 2, elements_read);
            out_asset->face_name = string_duplicate(face_name_buf);
            break;
        }
        case 'c': {
            // 'common', 'char' or 'chars' line
            if (line[1] == 'o') {
                // common
                i32 elements_read = sscanf(
                    line,
                    "common lineHeight=%d base=%u scaleW=%d scaleH=%d pages=%d", // ignore everything else.
                    &out_asset->line_height,
                    &out_asset->baseline,
                    &out_asset->atlas_size_x,
                    &out_asset->atlas_size_y,
                    &out_asset->page_count);

                VERIFY_LINE("common", line_num, 5, elements_read);

                // Allocate the pages array.
                if (out_asset->page_count > 0) {
                    if (!out_asset->pages) {
                        out_asset->pages = KALLOC_TYPE_CARRAY(kasset_bitmap_font_page, out_asset->page_count);
                    }
                } else {
                    KERROR("Pages is 0, which should not be possible. Font file reading aborted.");
                    return false;
                }
            } else if (line[1] == 'h') {
                if (line[4] == 's') {
                    // chars line
                    i32 elements_read = sscanf(line, "chars count=%u", &out_asset->glyph_count);
                    VERIFY_LINE("chars", line_num, 1, elements_read);

                    // Allocate the glyphs array.
                    if (out_asset->glyph_count > 0) {
                        if (!out_asset->glyphs) {
                            out_asset->glyphs = KALLOC_TYPE_CARRAY(kasset_bitmap_font_glyph, out_asset->glyph_count);
                        }
                    } else {
                        KERROR("Glyph count is 0, which should not be possible. Font file reading aborted.");
                        return false;
                    }
                } else {
                    // Assume 'char' line
                    kasset_bitmap_font_glyph* g = &out_asset->glyphs[glyphs_read];

                    i32 elements_read = sscanf(
                        line,
                        "char id=%d x=%hu y=%hu width=%hu height=%hu xoffset=%hd yoffset=%hd xadvance=%hd page=%hhu chnl=%*u",
                        &g->codepoint,
                        &g->x,
                        &g->y,
                        &g->width,
                        &g->height,
                        &g->x_offset,
                        &g->y_offset,
                        &g->x_advance,
                        &g->page_id);

                    VERIFY_LINE("char", line_num, 9, elements_read);

                    glyphs_read++;
                }
            } else {
                // invalid, ignore
            }
            break;
        }
        case 'p': {
            // 'page' line
            kasset_bitmap_font_page* page = &out_asset->pages[pages_read];
            char page_file_str[200] = {0};
            i32 elements_read = sscanf(
                line,
                "page id=%hhi file=\"%[^\"]\"",
                &page->id,
                page_file_str);

            VERIFY_LINE("page", line_num, 2, elements_read);

            page->image_asset_name = kname_create(page_file_str);

            break;
        }
        case 'k': {
            // 'kernings' or 'kerning' line
            if (line[7] == 's') {
                // Kernings
                i32 elements_read = sscanf(line, "kernings count=%u", &out_asset->kerning_count);

                VERIFY_LINE("kernings", line_num, 1, elements_read);

                // Allocate kernings array
                if (!out_asset->kernings) {
                    out_asset->kernings = KALLOC_TYPE_CARRAY(kasset_bitmap_font_kerning, out_asset->kerning_count);
                }
            } else if (line[7] == ' ') {
                // Kerning record
                kasset_bitmap_font_kerning* k = &out_asset->kernings[kernings_read];
                i32 elements_read = sscanf(
                    line,
                    "kerning first=%i  second=%i amount=%hi",
                    &k->codepoint_0,
                    &k->codepoint_1,
                    &k->amount);

                VERIFY_LINE("kerning", line_num, 3, elements_read);
            }
            break;
        }
        default:
            // Skip the line.
            break;
        }
    }

    return true;
}
