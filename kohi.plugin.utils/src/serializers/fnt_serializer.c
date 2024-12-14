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

static b8 import_fnt_material_library_file(const char* mtl_file_text, fnt_source_asset* out_mtl_source_asset);

b8 fnt_serializer_serialize(const fnt_source_asset* source_asset, const char** out_file_text) {
    KASSERT_MSG(false, "Not yet implemented");
    return false;
}

b8 fnt_serializer_deserialize(const char* fnt_file_text, fnt_source_asset* out_fnt_source_asset) {
    if (!fnt_file_text || !out_fnt_source_asset) {
        KERROR("fnt_serializer_deserialize requires valid pointers to fnt_file_text and out_fnt_source_asset.");
        return false;
    }

    return import_fnt_material_library_file(fnt_file_text, out_fnt_source_asset);
}

#define VERIFY_LINE(line_type, line_num, expected, actual)                                                                                     \
    if (actual != expected) {                                                                                                                  \
        KERROR("Error in file format reading type '%s', line %u. Expected %d element(s) but read %d.", line_type, line_num, expected, actual); \
        return false;                                                                                                                          \
    }

static b8 import_fnt_material_library_file(const char* mtl_file_text, fnt_source_asset* out_asset) {
    KDEBUG("Importing source bitmap .fnt file ...");

    kzero_memory(out_asset, sizeof(fnt_source_asset));
    char line_buf[512] = "";
    char* p = &line_buf[0];
    u32 line_length = 0;
    u32 line_num = 0;
    u32 glyphs_read = 0;
    u8 pages_read = 0;
    u32 kernings_read = 0;
    u32 start_from = 0;
    while (true) {
        start_from += line_length;
        ++line_num; // Increment the number right away, since most text editors' line display is 1-indexed.
        if (!string_line_get(mtl_file_text, 511, start_from, &p, &line_length)) {
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
            i32 elements_read = sscanf(
                line_buf,
                "info face=\"%[^\"]\" size=%u",
                out_asset->face_name,
                &out_asset->size);
            VERIFY_LINE("info", line_num, 2, elements_read);
            break;
        }
        case 'c': {
            // 'common', 'char' or 'chars' line
            if (line_buf[1] == 'o') {
                // common
                i32 elements_read = sscanf(
                    line_buf,
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
                        out_asset->pages = kallocate(sizeof(bitmap_font_page) * out_asset->page_count, MEMORY_TAG_ARRAY);
                    }
                } else {
                    KERROR("Pages is 0, which should not be possible. Font file reading aborted.");
                    return false;
                }
            } else if (line_buf[1] == 'h') {
                if (line_buf[4] == 's') {
                    // chars line
                    i32 elements_read = sscanf(line_buf, "chars count=%u", &out_asset->glyph_count);
                    VERIFY_LINE("chars", line_num, 1, elements_read);

                    // Allocate the glyphs array.
                    if (out_asset->glyph_count > 0) {
                        if (!out_asset->glyphs) {
                            out_asset->glyphs = kallocate(sizeof(font_glyph) * out_asset->glyph_count, MEMORY_TAG_ARRAY);
                        }
                    } else {
                        KERROR("Glyph count is 0, which should not be possible. Font file reading aborted.");
                        return false;
                    }
                } else {
                    // Assume 'char' line
                    font_glyph* g = &out_asset->glyphs[glyphs_read];

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
            bitmap_font_page* page = &out_asset->pages[pages_read];
            i32 elements_read = sscanf(
                line_buf,
                "page id=%hhi file=\"%[^\"]\"",
                &page->id,
                page->file);

            // Strip the extension.
            string_filename_no_extension_from_path(page->file, page->file);

            VERIFY_LINE("page", line_num, 2, elements_read);

            break;
        }
        case 'k': {
            // 'kernings' or 'kerning' line
            if (line_buf[7] == 's') {
                // Kernings
                i32 elements_read = sscanf(line_buf, "kernings count=%u", &out_asset->kerning_count);

                VERIFY_LINE("kernings", line_num, 1, elements_read);

                // Allocate kernings array
                if (!out_asset->kernings) {
                    out_asset->kernings = kallocate(sizeof(font_kerning) * out_asset->kerning_count, MEMORY_TAG_ARRAY);
                }
            } else if (line_buf[7] == ' ') {
                // Kerning record
                font_kerning* k = &out_asset->kernings[kernings_read];
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

    return true;
}
