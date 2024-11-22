#include "kresource_utils.h"
#include "assets/kasset_types.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "resources/resource_types.h"

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

material_type kresource_material_type_to_material_type(kresource_material_type type) {
    switch (type) {
    case KRESOURCE_MATERIAL_TYPE_UNKNOWN:
        return MATERIAL_TYPE_UNKNOWN;
    case KRESOURCE_MATERIAL_TYPE_STANDARD:
        return MATERIAL_TYPE_STANDARD;
    case KRESOURCE_MATERIAL_TYPE_WATER:
        return MATERIAL_TYPE_WATER;
    case KRESOURCE_MATERIAL_TYPE_BLENDED:
        return MATERIAL_TYPE_BLENDED;
    case KRESOURCE_MATERIAL_TYPE_CUSTOM:
        return MATERIAL_TYPE_CUSTOM;
    case KRESOURCE_MATERIAL_TYPE_COUNT:
        KERROR("kresource_material_type_to_material_type - Don't try to convert 'count', ya dingus!");
        return MATERIAL_TYPE_UNKNOWN;
    }
}
kresource_material_type material_type_to_kresource_material_type(kresource_material_type type) {
    switch (type) {
    case MATERIAL_TYPE_UNKNOWN:
        return KRESOURCE_MATERIAL_TYPE_UNKNOWN;
    case MATERIAL_TYPE_STANDARD:
        return KRESOURCE_MATERIAL_TYPE_STANDARD;
    case MATERIAL_TYPE_WATER:
        return KRESOURCE_MATERIAL_TYPE_WATER;
    case MATERIAL_TYPE_BLENDED:
        return KRESOURCE_MATERIAL_TYPE_BLENDED;
    case MATERIAL_TYPE_CUSTOM:
        return KRESOURCE_MATERIAL_TYPE_CUSTOM;
    case MATERIAL_TYPE_COUNT:
        KERROR("material_type_to_kresource_material_type - Don't try to convert 'count', ya dingus!");
        return KRESOURCE_MATERIAL_TYPE_UNKNOWN;
    }
}

material_model kresource_material_model_to_material_model(kresource_material_model model) {
    switch (model) {
    case KRESOURCE_MATERIAL_MODEL_UNLIT:
        return MATERIAL_MODEL_UNLIT;
    case KRESOURCE_MATERIAL_MODEL_PBR:
        return MATERIAL_MODEL_PBR;
    case KRESOURCE_MATERIAL_MODEL_PHONG:
        return MATERIAL_MODEL_PHONG;
    case KRESOURCE_MATERIAL_MODEL_CUSTOM:
        return MATERIAL_MODEL_CUSTOM;
    case KRESOURCE_MATERIAL_MODEL_COUNT:
        KERROR("kresource_material_model_to_material_model - Don't try to convert 'count', ya dingus!");
        return MATERIAL_MODEL_UNLIT;
    }
}
kresource_material_model material_model_to_kresource_material_model(material_model model) {
    switch (model) {
    case MATERIAL_MODEL_UNLIT:
        return KRESOURCE_MATERIAL_MODEL_UNLIT;
    case MATERIAL_MODEL_PBR:
        return KRESOURCE_MATERIAL_MODEL_PBR;
    case MATERIAL_MODEL_PHONG:
        return KRESOURCE_MATERIAL_MODEL_PHONG;
    case MATERIAL_MODEL_CUSTOM:
        return KRESOURCE_MATERIAL_MODEL_CUSTOM;
    case MATERIAL_MODEL_COUNT:
        return KRESOURCE_MATERIAL_MODEL_UNLIT;
        KERROR("material_model_to_kresource_material_model - Don't try to convert 'count', ya dingus!");
    }
}
