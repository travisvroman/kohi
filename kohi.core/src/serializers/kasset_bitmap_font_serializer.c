#include "kasset_bitmap_font_serializer.h"

#include "assets/kasset_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "strings/kstring.h"

typedef struct bitmap_font_header {
	// The base binary asset header. Must always be the first member.
	binary_asset_header base;

	u32 font_size;
	i32 line_height;
	i32 baseline;
	i32 atlas_size_x;
	i32 atlas_size_y;
	u32 glyph_count;
	u32 kerning_count;
	u32 page_count;
	u32 face_name_len;
} bitmap_font_header;

void *kasset_bitmap_font_serialize (const kasset_bitmap_font *asset, u64 *out_size) {
	if (!asset) {
		KERROR("Cannot serialize without an asset, ya dingus!");
		return 0;
	}

	/**
	 * File layout is header, face name string, glyphs, kernings, pages
	 */
	bitmap_font_header header = {0};

	// Base attributes.
	header.base.magic = ASSET_MAGIC;
	header.base.type = (u32)KASSET_TYPE_BITMAP_FONT;
	header.base.data_block_size = 0;
	// Always write the most current version.
	header.base.version = 1;

	kasset_bitmap_font *typed_asset = (kasset_bitmap_font *)asset;

	const char *face_str = kname_string_get(typed_asset->face);
	header.face_name_len = string_length(face_str);

	header.font_size = typed_asset->size;
	header.line_height = typed_asset->line_height;
	header.baseline = typed_asset->baseline;
	header.atlas_size_x = typed_asset->atlas_size_x;
	header.atlas_size_y = typed_asset->atlas_size_y;
	header.glyph_count = typed_asset->glyph_count;
	header.kerning_count = typed_asset->kerning_count;
	header.page_count = typed_asset->page_count;

	// Calculate the total required size first (for everything after the header.
	header.base.data_block_size += header.face_name_len;
	header.base.data_block_size += (sizeof(kasset_bitmap_font_glyph) * typed_asset->glyph_count);
	header.base.data_block_size += (sizeof(kasset_bitmap_font_kerning) * typed_asset->kerning_count);

	// Iterate pages and save the length, then the string asset name for each.
	for (u32 i = 0; i < typed_asset->page_count; ++i) {
		const char *str = kname_string_get(typed_asset->pages[i].image_asset_name);
		u32 len = string_length(str);
		header.base.data_block_size += sizeof(u32); // For the length
		header.base.data_block_size += len;			// For the actual string.
	}

	// The total space required for the data block.
	*out_size = sizeof(bitmap_font_header) + header.base.data_block_size;

	// Allocate said block.
	void *block = kallocate(*out_size, MEMORY_TAG_SERIALIZER);
	// Write the header.
	kcopy_memory(block, &header, sizeof(bitmap_font_header));

	// For this asset, it's not quite a simple manner of just using the byte block.
	// Start by moving past the header.
	u64 offset = sizeof(bitmap_font_header);

	// Face name.
	kcopy_memory(block + offset, face_str, header.face_name_len);
	offset += header.face_name_len;

	// Glyphs can be written as-is
	u64 glyph_size = (sizeof(kasset_bitmap_font_glyph) * typed_asset->glyph_count);
	kcopy_memory(block + offset, typed_asset->glyphs, glyph_size);
	offset += glyph_size;

	// Kernings can be written as-is
	u64 kerning_size = (sizeof(kasset_bitmap_font_kerning) * typed_asset->kerning_count);
	kcopy_memory(block + offset, typed_asset->kernings, kerning_size);
	offset += kerning_size;

	// Pages need to write asset name string length, then the actual string.
	for (u32 i = 0; i < typed_asset->page_count; ++i) {
		const char *str = kname_string_get(typed_asset->pages[i].image_asset_name);
		u32 len = string_length(str);

		kcopy_memory(block + offset, &len, sizeof(u32));
		offset += sizeof(u32);

		kcopy_memory(block + offset, str, sizeof(char) * len);
		offset += len;
	}

	// Return the serialized block of memory.
	return block;
}

b8 kasset_bitmap_font_deserialize (u64 size, const void *block, kasset_bitmap_font *out_asset) {
	if (!size || !block || !out_asset) {
		KERROR("Cannot deserialize without a nonzero size, block of memory and a kasset_bitmap_font to write to.");
		return false;
	}

	const bitmap_font_header *header = block;
	if (header->base.magic != ASSET_MAGIC) {
		KERROR("Memory is not a Kohi binary asset.");
		return false;
	}

	kasset_type type = (kasset_type)header->base.type;
	if (type != KASSET_TYPE_BITMAP_FONT) {
		KERROR("Memory is not a Kohi bitmap font asset.");
		return false;
	}

	/* out_asset->meta.version = header->base.version; */ // TODO: version

	kasset_bitmap_font *typed_asset = (kasset_bitmap_font *)out_asset;
	typed_asset->baseline = header->baseline;
	typed_asset->line_height = header->line_height;
	typed_asset->size = header->font_size;
	typed_asset->atlas_size_x = header->atlas_size_x;
	typed_asset->atlas_size_y = header->atlas_size_y;
	typed_asset->kerning_count = header->kerning_count;
	if (header->kerning_count) {
		typed_asset->kernings = KALLOC_TYPE_CARRAY(kasset_bitmap_font_kerning, header->kerning_count);
	}
	typed_asset->page_count = header->page_count;
	if (header->page_count) {
		typed_asset->pages = KALLOC_TYPE_CARRAY(kasset_bitmap_font_page, header->page_count);
	}
	typed_asset->glyph_count = header->glyph_count;
	if (header->glyph_count) {
		typed_asset->glyphs = KALLOC_TYPE_CARRAY(kasset_bitmap_font_glyph, header->glyph_count);
	} else {
		KERROR("Attempting to load a bitmap font asset that has no glyphs. But why?");
		return false;
	}

	u64 offset = sizeof(bitmap_font_header);

	// Face name.
	char *face_str = kallocate(header->face_name_len + 1, MEMORY_TAG_STRING);
	kcopy_memory(face_str, block + offset, header->face_name_len);
	typed_asset->face = kname_create(face_str);
	string_free(face_str);
	offset += header->face_name_len;

	// Glyphs - at least one is required.
	if (header->glyph_count) {
		KCOPY_TYPE_CARRAY(typed_asset->glyphs, block + offset, kasset_bitmap_font_glyph, header->glyph_count);
		offset += sizeof(kasset_bitmap_font_glyph) * header->glyph_count;
	} else {
		KERROR("Attempting to load a bitmap font asset that has no glyphs. But why?");
		return false;
	}

	// Kernings - optional.
	if (header->kerning_count) {
		KCOPY_TYPE_CARRAY(typed_asset->kernings, block + offset, kasset_bitmap_font_kerning, header->kerning_count);
		offset += sizeof(kasset_bitmap_font_kerning) * header->kerning_count;
	}

	// Pages - at least one is required.
	if (header->page_count) {
		for (u32 i = 0; i < header->page_count; ++i) {
			// String length.
			u32 len = 0;
			kcopy_memory(&len, block + offset, sizeof(u32));
			offset += sizeof(u32);

			u64 alloc_size = sizeof(char) * len;
			char *str = kallocate(alloc_size + 1, MEMORY_TAG_STRING);
			kcopy_memory(str, block + offset, alloc_size);
			offset += len;

			typed_asset->pages[i].id = i;
			typed_asset->pages[i].image_asset_name = kname_create(str);
			string_free(str);
		}
	} else {
		KERROR("Attempting to load a bitmap font asset that has no pages. But why?");
		return false;
	}

	return true;
}
