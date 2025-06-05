#include "importers/kasset_importer_bitmap_font_fnt.h"

#include <assets/kasset_types.h>
#include <core_render_types.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <serializers/kasset_bitmap_font_serializer.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include "serializers/fnt_serializer.h"

b8 kasset_bitmap_font_fnt_import(const char* output_directory, const char* output_filename, u64 data_size, const void* data, void* params) {
    if (!data_size || !data) {
        KERROR("%s requires valid pointers to self and data, as well as a nonzero data_size.", __FUNCTION__);
        return false;
    }

    // Handle FNT file import.
    fnt_source_asset fnt_asset = {0};
    if (!fnt_serializer_deserialize(data, &fnt_asset)) {
        KERROR("FNT file import failed! See logs for details.");
        return false;
    }

    // Convert FNT asset to kasset_bitmap_font.
    kasset_bitmap_font asset = {0};
    asset.baseline = fnt_asset.baseline;
    asset.face = kname_create(fnt_asset.face_name);
    asset.size = fnt_asset.size;
    asset.line_height = fnt_asset.line_height;
    asset.atlas_size_x = fnt_asset.atlas_size_x;
    asset.atlas_size_y = fnt_asset.atlas_size_y;

    asset.pages = array_kasset_bitmap_font_page_create(fnt_asset.page_count);
    KCOPY_TYPE_CARRAY(asset.pages.data, fnt_asset.pages, kasset_bitmap_font_page, fnt_asset.page_count);

    asset.glyphs = array_kasset_bitmap_font_glyph_create(fnt_asset.glyph_count);
    KCOPY_TYPE_CARRAY(asset.glyphs.data, fnt_asset.glyphs, kasset_bitmap_font_glyph, fnt_asset.glyph_count);

    if (fnt_asset.kerning_count) {
        asset.kernings = array_kasset_bitmap_font_kerning_create(fnt_asset.kerning_count);
        KCOPY_TYPE_CARRAY(asset.kernings.data, fnt_asset.kernings, kasset_bitmap_font_kerning, fnt_asset.kerning_count);
    }

    // Cleanup fnt asset.
    KFREE_TYPE_CARRAY(fnt_asset.pages, kasset_bitmap_font_page, fnt_asset.page_count);
    KFREE_TYPE_CARRAY(fnt_asset.glyphs, kasset_bitmap_font_glyph, fnt_asset.glyph_count);
    KFREE_TYPE_CARRAY(fnt_asset.kernings, kasset_bitmap_font_kerning, fnt_asset.kerning_count);
    string_free(fnt_asset.face_name);

    // Serialize data and write out kbf file (binary Kohi Bitmap Font).
    u64 serialized_size = 0;
    void* serialized_data = kasset_bitmap_font_serialize(&asset, &serialized_size);
    if (!serialized_data || !serialized_size) {
        KERROR("Failed to serialize binary Kohi Bitmap Font.");
        return false;
    }

    // Write out .kbf file.
    const char* out_path = string_format("%s/%s.%s", output_directory, output_filename, "kbf");
    b8 success = true;
    if (!filesystem_write_entire_binary_file(out_path, serialized_size, serialized_data)) {
        KWARN("Failed to write .kbf (Kohi Bitmap Font) file. See logs for details.");
        success = false;
    }

    return success;
}
