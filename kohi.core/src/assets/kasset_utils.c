#include "kasset_utils.h"

#include "assets/kasset_types.h"
#include "debug/kassert.h"
#include "logger.h"
#include "strings/kstring.h"

// Static lookup table for kasset type strings.
static const char *kasset_type_strs[KASSET_TYPE_MAX] = {
	"unknown",		 // KASSET_TYPE_UNKNOWN,
	"image",		 // KASSET_TYPE_IMAGE,
	"material",		 // KASSET_TYPE_MATERIAL,
	"hf_terrain",	 // KASSET_TYPE_HEIGHTFIELD_TERRAIN,
	"hm_terrain",	 // KASSET_TYPE_HEIGHTMAP_TERRAIN,
	"scene",		 // KASSET_TYPE_SCENE
	"bitmap_font",	 // KASSET_TYPE_BITMAP_FONT,
	"system_font",	 // KASSET_TYPE_SYSTEM_FONT,
	"text",			 // KASSET_TYPE_TEXT,
	"binary",		 // KASSET_TYPE_BINARY,
	"kson",			 // KASSET_TYPE_KSON,
	"voxel_terrain", // KASSET_TYPE_VOXEL_TERRAIN,
	"reserved_2",	 // KASSET_TYPE_RESERVED_2,
	"audio",		 // KASSET_TYPE_AUDIO,
	"shader",		 // KASSET_TYPE_SHADER,
	"model"			 // KASSET_TYPE_MODEL,
};

// Ensure changes to asset types break this if it isn't also updated.
STATIC_ASSERT(KASSET_TYPE_MAX == (sizeof(kasset_type_strs) / sizeof(*kasset_type_strs)), "Asset type count does not match string lookup table count.");

kasset_type kasset_type_from_string (const char *type_str) {
	for (u32 i = 0; i < KASSET_TYPE_MAX; ++i) {
		if (strings_equali(type_str, kasset_type_strs[i])) {
			return (kasset_type)i;
		}
	}
	KWARN("kasset_type_from_string: Unrecognized type '%s'. Returning unknown.", type_str);
	return KASSET_TYPE_UNKNOWN;
}

const char *kasset_type_to_string (kasset_type type) {
	KASSERT_MSG(type < KASSET_TYPE_MAX, "Provided kasset_type is not valid.");
	return string_duplicate(kasset_type_strs[type]);
}

b8 kasset_type_is_binary (kasset_type type) {
	switch (type) {
	default:
	case KASSET_TYPE_UNKNOWN:
	case KASSET_TYPE_MAX:
	case KASSET_TYPE_MATERIAL:
	case KASSET_TYPE_HEIGHTMAP_TERRAIN:
	case KASSET_TYPE_HEIGHTFIELD_TERRAIN:
	case KASSET_TYPE_SCENE:
	case KASSET_TYPE_RESERVED_2:
	case KASSET_TYPE_SYSTEM_FONT:
	case KASSET_TYPE_TEXT:
	case KASSET_TYPE_KSON:
	case KASSET_TYPE_SHADER:
		return false;
	case KASSET_TYPE_IMAGE:
	case KASSET_TYPE_BITMAP_FONT:
	case KASSET_TYPE_BINARY:
	case KASSET_TYPE_VOXEL_TERRAIN:
	case KASSET_TYPE_AUDIO:
		return true;
	}
}
