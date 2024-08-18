#include "kasset_bitmap_font_serializer.h"

#include "assets/kasset_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "strings/kstring.h"

#include <stdio.h>

#define VERIFY_LINE(line_type, line_num, expected, actual)                                                                                     \
    if (actual != expected) {                                                                                                                  \
        KERROR("Error in file format reading type '%s', line %u. Expected %d element(s) but read %d.", line_type, line_num, expected, actual); \
        goto cleanup;                                                                                                                          \
    }

b8 kasset_bitmap_font_deserialize(const char* file_text, kasset* out_asset) {
    b8 success = false;
    if (!file_text || !out_asset) {
        KERROR("kasset_kson_deserialize requires valid pointers to file_text and out_asset.");
        return success;
    }

    if (out_asset->type != KASSET_TYPE_BITMAP_FONT) {
        KERROR("kasset_bitmap_font_serialize requires a kson asset to serialize.");
        return success;
    }

    kasset_bitmap_font* typed_asset = (kasset_bitmap_font*)out_asset;

    u32 page_count = 0;
    u32 glyph_count = 0;
    u32 kerning_count = 0;

    kzero_memory(typed_asset, sizeof(kasset_bitmap_font));
    char line_buf[512] = "";
    char* p = &line_buf[0];
    u32 line_length = 0;
    u32 line_num = 0;
    u32 glyphs_read = 0;
    u8 pages_read = 0;
    u32 kernings_read = 0;
    u32 start_from = 0;
    while (true) {
        start_from += line_length; // TODO: might need +1 for \n?
        if (!string_line_get(file_text, 511, start_from, &p, &line_length)) {
            /* if (!filesystem_read_line(mtl_file, 511, &p, &line_length)) { */
            break;
        }

        // Skip blank lines.
        if (line_length < 1) {
            continue;
        }

        char first_char = line_buf[0];
        switch (first_char) {
        case 'i': {
            // 'info' line

            // NOTE: only extract the face and size, ignore the rest.
            char face_buf[512];
            kzero_memory(face_buf, sizeof(char) * 512);
            i32 elements_read = sscanf(
                line_buf,
                "info face=\"%[^\"]\" size=%u",
                face_buf,
                &typed_asset->size);
            VERIFY_LINE("info", line_num, 2, elements_read);

            typed_asset->face = kname_create(face_buf);

            break;
        }
        case 'c': {
            // 'common', 'char' or 'chars' line
            if (line_buf[1] == 'o') {
                // common
                i32 elements_read = sscanf(
                    line_buf,
                    "common lineHeight=%d base=%u scaleW=%d scaleH=%d pages=%d", // ignore everything else.
                    &typed_asset->line_height,
                    &typed_asset->baseline,
                    &typed_asset->atlas_size_x,
                    &typed_asset->atlas_size_y,
                    &page_count);

                VERIFY_LINE("common", line_num, 5, elements_read);

                // Allocate the pages array.
                if (page_count > 0) {
                    if (!typed_asset->pages.data) {
                        typed_asset->pages = array_kasset_bitmap_font_page_create(page_count);
                    }
                } else {
                    KERROR("Pages is 0, which should not be possible. Font file reading aborted.");
                    goto cleanup;
                }
            } else if (line_buf[1] == 'h') {
                if (line_buf[4] == 's') {
                    // chars line
                    i32 elements_read = sscanf(line_buf, "chars count=%u", &glyph_count);
                    VERIFY_LINE("chars", line_num, 1, elements_read);

                    // Allocate the glyphs array.
                    if (glyph_count > 0) {
                        if (!typed_asset->glyphs.data) {
                            typed_asset->glyphs = array_kasset_bitmap_font_glyph_create(glyph_count);
                        }
                    } else {
                        KERROR("Glyph count is 0, which should not be possible. Font file reading aborted.");
                        goto cleanup;
                    }
                } else {
                    // Assume 'char' line
                    kasset_bitmap_font_glyph* g = &typed_asset->glyphs.data[glyphs_read];

                    i32 elements_read = sscanf(
                        line_buf,
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
            kasset_bitmap_font_page* page = &typed_asset->pages.data[pages_read];
            char file_buf[512];
            kzero_memory(file_buf, sizeof(char) * 512);
            i32 elements_read = sscanf(
                line_buf,
                "page id=%hhi file=\"%[^\"]\"",
                &page->id,
                file_buf);

            // Strip the extension.
            string_filename_no_extension_from_path(file_buf, file_buf);

            page->image_asset_name = kname_create(file_buf);

            VERIFY_LINE("page", line_num, 2, elements_read);

            break;
        }
        case 'k': {
            // 'kernings' or 'kerning' line
            if (line_buf[7] == 's') {
                // Kernings
                i32 elements_read = sscanf(line_buf, "kernings count=%u", &kerning_count);

                VERIFY_LINE("kernings", line_num, 1, elements_read);

                // Allocate kernings array
                if (kerning_count) {
                    if (!typed_asset->kernings.data) {
                        typed_asset->kernings = array_kasset_bitmap_font_kerning_create(kerning_count);
                    }
                }
            } else if (line_buf[7] == ' ') {
                // Kerning record
                kasset_bitmap_font_kerning* k = &typed_asset->kernings.data[kernings_read];
                i32 elements_read = sscanf(
                    line_buf,
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

    success = true;

cleanup:
    if (!success) {
        array_kasset_bitmap_font_page_destroy(&typed_asset->pages);
        array_kasset_bitmap_font_glyph_destroy(&typed_asset->glyphs);
        array_kasset_bitmap_font_kerning_destroy(&typed_asset->kernings);
    }

    return success;
}
