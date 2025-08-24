#include "kasset_utils.h"

#include "assets/kasset_types.h"
#include "debug/kassert.h"
#include "logger.h"
#include "strings/kstring.h"

// Static lookup table for kasset type strings.
static const char* kasset_type_strs[KASSET_TYPE_MAX] = {
    "Unknown",          // KASSET_TYPE_UNKNOWN,
    "Image",            // KASSET_TYPE_IMAGE,
    "Material",         // KASSET_TYPE_MATERIAL,
    "StaticMesh",       // KASSET_TYPE_STATIC_MESH,
    "HeightmapTerrain", // KASSET_TYPE_HEIGHTMAP_TERRAIN,
    "Scene",            // KASSET_TYPE_SCENE
    "BitmapFont",       // KASSET_TYPE_BITMAP_FONT,
    "SystemFont",       // KASSET_TYPE_SYSTEM_FONT,
    "Text",             // KASSET_TYPE_TEXT,
    "Binary",           // KASSET_TYPE_BINARY,
    "Kson",             // KASSET_TYPE_KSON,
    "VoxelTerrain",     // KASSET_TYPE_VOXEL_TERRAIN,
    "SkinnedMesh",      // KASSET_TYPE_SKINNED_MESH,
    "Audio",            // KASSET_TYPE_AUDIO,
    "Shader"            // KASSET_TYPE_SHADER,
};

// Ensure changes to asset types break this if it isn't also updated.
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

b8 kasset_type_is_binary(kasset_type type) {
    switch (type) {
    default:
    case KASSET_TYPE_UNKNOWN:
    case KASSET_TYPE_MAX:
    case KASSET_TYPE_MATERIAL:
    case KASSET_TYPE_HEIGHTMAP_TERRAIN:
    case KASSET_TYPE_SCENE:
    case KASSET_TYPE_SYSTEM_FONT:
    case KASSET_TYPE_TEXT:
    case KASSET_TYPE_KSON:
    case KASSET_TYPE_SHADER:
        return false;
    case KASSET_TYPE_IMAGE:
    case KASSET_TYPE_STATIC_MESH:
    case KASSET_TYPE_BITMAP_FONT:
    case KASSET_TYPE_BINARY:
    case KASSET_TYPE_VOXEL_TERRAIN:
    case KASSET_TYPE_SKINNED_MESH:
    case KASSET_TYPE_AUDIO:
        return true;
    }
}
