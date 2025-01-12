#include "kresource_utils.h"
#include "assets/kasset_types.h"
#include "kresources/kresource_types.h"
#include "logger.h"

texture_format image_format_to_texture_format(kasset_image_format format) {
    switch (format) {
    case KASSET_IMAGE_FORMAT_RGBA8:
        return TEXTURE_FORMAT_RGBA8;

    case KASSET_IMAGE_FORMAT_UNDEFINED:
    default:
        return TEXTURE_FORMAT_UNKNOWN;
    }
}

kasset_image_format texture_format_to_image_format(texture_format format) {
    switch (format) {
    case TEXTURE_FORMAT_RGBA8:
        return KASSET_IMAGE_FORMAT_RGBA8;

    case TEXTURE_FORMAT_UNKNOWN:
    default:
        return KASSET_IMAGE_FORMAT_UNDEFINED;
    }
}

u8 channel_count_from_texture_format(texture_format format) {
    switch (format) {
    case TEXTURE_FORMAT_RGBA8:
        return 4;
    case TEXTURE_FORMAT_RGB8:
        return 3;
    default:
        return 4;
    }
}

const char* kresource_type_to_string(kresource_type type) {
    switch (type) {
    case KRESOURCE_TYPE_UNKNOWN:
        return "unknown";
    case KRESOURCE_TYPE_TEXT:
        return "text";
    case KRESOURCE_TYPE_BINARY:
        return "binary";
    case KRESOURCE_TYPE_TEXTURE:
        return "texture";
    case KRESOURCE_TYPE_MATERIAL:
        return "material";
    case KRESOURCE_TYPE_SHADER:
        return "shader";
    case KRESOURCE_TYPE_STATIC_MESH:
        return "static_mesh";
    case KRESOURCE_TYPE_SKELETAL_MESH:
        return "skeletal_mesh";
    case KRESOURCE_TYPE_BITMAP_FONT:
        return "bitmap_font";
    case KRESOURCE_TYPE_SYSTEM_FONT:
        return "system_font";
    case KRESOURCE_TYPE_SCENE:
        return "scene";
    case KRESOURCE_TYPE_HEIGHTMAP_TERRAIN:
        return "heightmap_terrain";
    case KRESOURCE_TYPE_VOXEL_TERRAIN:
        return "voxel_terrain";
    case KRESOURCE_TYPE_AUDIO:
        return "audio";
    case KRESOURCE_TYPE_COUNT:
    case KRESOURCE_KNOWN_TYPE_MAX:
        KERROR("Attempted to get string representation of count/max. Returning unknown.");
        return "unknown";
    }
}
