#include "kresource_utils.h"
#include "assets/kasset_types.h"
#include "kresources/kresource_types.h"

kresource_texture_format image_format_to_texture_format(kasset_image_format format) {
    switch (format) {
    case KASSET_IMAGE_FORMAT_RGBA8:
        return KRESOURCE_TEXTURE_FORMAT_RGBA8;

    case KASSET_IMAGE_FORMAT_UNDEFINED:
    default:
        return KRESOURCE_TEXTURE_FORMAT_UNKNOWN;
    }
}

kasset_image_format texture_format_to_image_format(kresource_texture_format format) {
    switch (format) {
    case KRESOURCE_TEXTURE_FORMAT_RGBA8:
        return KASSET_IMAGE_FORMAT_RGBA8;

    case KRESOURCE_TEXTURE_FORMAT_UNKNOWN:
    default:
        return KASSET_IMAGE_FORMAT_UNDEFINED;
    }
}

u8 channel_count_from_texture_format(kresource_texture_format format) {
    switch (format) {
    case KRESOURCE_TEXTURE_FORMAT_RGBA8:
        return 4;
    case KRESOURCE_TEXTURE_FORMAT_RGB8:
        return 3;
    default:
        return 4;
    }
}

texture_channel kresource_texture_map_channel_to_texture_channel(kresource_material_texture_map_channel channel) {
    switch (channel) {
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_R:
        return TEXTURE_CHANNEL_R;
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_G:
        return TEXTURE_CHANNEL_G;
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_B:
        return TEXTURE_CHANNEL_B;
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_A:
        return TEXTURE_CHANNEL_A;
    }
}
kresource_material_texture_map_channel texture_channel_to_kresource_texture_map_channel(texture_channel channel) {
    switch (channel) {
    case TEXTURE_CHANNEL_R:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_R;
    case TEXTURE_CHANNEL_G:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_G;
    case TEXTURE_CHANNEL_B:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_B;
    case TEXTURE_CHANNEL_A:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_A;
    }
}
