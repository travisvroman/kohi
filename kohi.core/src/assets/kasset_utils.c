#include "kasset_utils.h"
#include "assets/kasset_types.h"
#include "debug/kassert.h"
#include "logger.h"
#include "strings/kstring.h"

b8 kasset_util_parse_name(const char* fully_qualified_name, struct kasset_name* out_name) {
    if (!fully_qualified_name || !out_name) {
        KERROR("kasset_util_parse_name requires valid fully_qualified_name and valid pointer to out_name");
        return false;
    }
    // "Runtime.Texture.Rock.01"
    char package_name[KPACKAGE_NAME_MAX_LENGTH] = {0};
    char asset_type[KASSET_TYPE_MAX_LENGTH] = {0};
    char asset_name[KASSET_NAME_MAX_LENGTH] = {0};
    char* parts[3] = {package_name, asset_type, asset_name};

    // Get the UTF-8 string length
    u32 text_length_utf8 = string_utf8_length(fully_qualified_name);
    u32 char_length = string_length(fully_qualified_name);

    if (text_length_utf8 < 1) {
        KERROR("kasset_util_parse_name was passed an empty string for name. Nothing to be done.");
        return false;
    }

    u8 part_index = 0;
    u32 part_loc = 0;
    for (u32 c = 0; c < char_length;) {
        i32 codepoint = fully_qualified_name[c];
        u8 advance = 1;

        // Ensure the propert UTF-8 codepoint is being used.
        if (!bytes_to_codepoint(fully_qualified_name, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        if (part_index < 2 && codepoint == '.') {
            // null terminate and move on.
            parts[part_index][part_loc] = 0;
            part_index++;
            part_loc = 0;
        }

        // Add to the current part string.
        for (u32 i = 0; i < advance; ++i) {
            parts[part_index][part_loc] = c + i;
            part_loc++;
        }

        c += advance;
    }
    parts[part_index][part_loc] = 0;

    out_name->fully_qualified_name = string_duplicate(fully_qualified_name);

    return true;
}

// Static lookup table for kasset type strings.
static const char* kasset_type_strs[KASSET_TYPE_MAX] = {
    "Unknown",          // KASSET_TYPE_UNKNOWN,
    "Image",            // KASSET_TYPE_IMAGE,
    "Material",         // KASSET_TYPE_MATERIAL,
    "StaticMesh",       // KASSET_TYPE_STATIC_MESH,
    "HeightmapTerrain", // KASSET_TYPE_HEIGHTMAP_TERRAIN,
    "BitmapFont",       // KASSET_TYPE_BITMAP_FONT,
    "SystemFont",       // KASSET_TYPE_SYSTEM_FONT,
    "Text",             // KASSET_TYPE_TEXT,
    "Binary",           // KASSET_TYPE_BINARY,
    "Kson",             // KASSET_TYPE_KSON,
    "VoxelTerrain",     // KASSET_TYPE_VOXEL_TERRAIN,
    "SkeletalMesh"      // KASSET_TYPE_SKELETAL_MESH,
};

// Ensure changes to texture types break this if it isn't also updated.
STATIC_ASSERT(KASSET_TYPE_MAX == (sizeof(kasset_type_strs) / sizeof(*kasset_type_strs)), "Asset type count does not match string lookup table count.");

kasset_type kasset_type_from_string(const char* type_str) {
    for (u32 i = 0; i < KASSET_TYPE_MAX; ++i) {
        if (strings_equali(type_str, kasset_type_strs[i])) {
            return (kasset_type)i;
        }
    }
    KWARN("kasset_type_from_string: Unrecognized type '%s'. Returning unknown.");
    return KASSET_TYPE_UNKNOWN;
}

const char* kasset_type_to_string(kasset_type type) {
    KASSERT_MSG(type < KASSET_TYPE_MAX, "Provided kasset_type is not valid.");
    return string_duplicate(kasset_type_strs[type]);
}
