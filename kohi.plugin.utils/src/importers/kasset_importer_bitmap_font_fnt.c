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

b8 kasset_bitmap_font_fnt_import (const char *source_path, const char *target_path) {
	if (!source_path || !target_path) {
		KERROR("%s requires valid source_path and target_path.", __FUNCTION__);
		return false;
	}

	const char *data = filesystem_read_entire_text_file(source_path);
	if (!data) {
		KERROR("Error reading source bitmap font file (%s). See logs for details.", source_path);
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

	asset.page_count = fnt_asset.page_count;
	asset.pages = KALLOC_TYPE_CARRAY(kasset_bitmap_font_page, fnt_asset.page_count);
	KCOPY_TYPE_CARRAY(asset.pages, fnt_asset.pages, kasset_bitmap_font_page, fnt_asset.page_count);

	asset.glyph_count = fnt_asset.glyph_count;
	asset.glyphs = KALLOC_TYPE_CARRAY(kasset_bitmap_font_glyph, fnt_asset.glyph_count);
	KCOPY_TYPE_CARRAY(asset.glyphs, fnt_asset.glyphs, kasset_bitmap_font_glyph, fnt_asset.glyph_count);

	asset.kerning_count = fnt_asset.kerning_count;
	if (fnt_asset.kerning_count) {
		asset.kernings = KALLOC_TYPE_CARRAY(kasset_bitmap_font_kerning, fnt_asset.kerning_count);
		KCOPY_TYPE_CARRAY(asset.kernings, fnt_asset.kernings, kasset_bitmap_font_kerning, fnt_asset.kerning_count);
	}

	// Cleanup fnt asset.
	if (fnt_asset.pages) {
		kfree(fnt_asset.pages);
	}
	if (fnt_asset.glyphs) {
		kfree(fnt_asset.glyphs);
	}
	if (fnt_asset.kernings) {
		kfree(fnt_asset.kernings);
	}
	if (fnt_asset.face_name) {
		string_free(fnt_asset.face_name);
	}

	// Serialize data and write out kbf file (binary Kohi Bitmap Font).
	u64 serialized_size = 0;
	void *serialized_data = kasset_bitmap_font_serialize(&asset, &serialized_size);
	if (!serialized_data || !serialized_size) {
		KERROR("Failed to serialize binary Kohi Bitmap Font.");
		return false;
	}

	// Write out .kbf file.
	b8 success = true;
	if (!filesystem_write_entire_binary_file(target_path, serialized_size, serialized_data)) {
		KWARN("Failed to write .kbf (Kohi Bitmap Font) file. See logs for details.");
		success = false;
	}

	if (serialized_data) {
		kfree(serialized_data);
	}

	return success;
}
