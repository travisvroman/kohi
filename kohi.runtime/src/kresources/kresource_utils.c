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
