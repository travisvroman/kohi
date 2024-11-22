#include "kasset_kresource_utils.h"
#include "kresources/kresource_types.h"
#include "logger.h"

kresource_material_type kasset_material_type_to_kresource(kasset_material_type type) {
    switch (type) {
    case KASSET_MATERIAL_TYPE_UNKNOWN:
        return KRESOURCE_MATERIAL_TYPE_UNKNOWN;
    case KASSET_MATERIAL_TYPE_STANDARD:
        return KRESOURCE_MATERIAL_TYPE_STANDARD;
    case KASSET_MATERIAL_TYPE_WATER:
        return KRESOURCE_MATERIAL_TYPE_WATER;
    case KASSET_MATERIAL_TYPE_BLENDED:
        return KRESOURCE_MATERIAL_TYPE_BLENDED;
    case KASSET_MATERIAL_TYPE_COUNT:
        KWARN("kasset_material_type_to_kresource converting count - did you mean to do this?");
        return KRESOURCE_MATERIAL_TYPE_COUNT;
    case KASSET_MATERIAL_TYPE_CUSTOM:
        return KRESOURCE_MATERIAL_TYPE_CUSTOM;
    }
}
kasset_material_type kresource_material_type_to_kasset(kresource_material_type type) {
    switch (type) {
    case KRESOURCE_MATERIAL_TYPE_UNKNOWN:
        return KASSET_MATERIAL_TYPE_UNKNOWN;
    case KRESOURCE_MATERIAL_TYPE_STANDARD:
        return KASSET_MATERIAL_TYPE_STANDARD;
    case KRESOURCE_MATERIAL_TYPE_WATER:
        return KASSET_MATERIAL_TYPE_WATER;
    case KRESOURCE_MATERIAL_TYPE_BLENDED:
        return KASSET_MATERIAL_TYPE_BLENDED;
    case KRESOURCE_MATERIAL_TYPE_COUNT:
        return KASSET_MATERIAL_TYPE_COUNT;
        KWARN("kresource_material_type_to_kasset converting count - did you mean to do this?");
    case KRESOURCE_MATERIAL_TYPE_CUSTOM:
        return KASSET_MATERIAL_TYPE_CUSTOM;
    }
}

kresource_material_model kasset_material_model_to_kresource(kasset_material_model model) {
    switch (model) {
    case KASSET_MATERIAL_MODEL_UNLIT:
        return KRESOURCE_MATERIAL_MODEL_UNLIT;
    case KASSET_MATERIAL_MODEL_PBR:
        return KRESOURCE_MATERIAL_MODEL_PBR;
    case KASSET_MATERIAL_MODEL_PHONG:
        return KRESOURCE_MATERIAL_MODEL_PHONG;
    case KASSET_MATERIAL_MODEL_COUNT:
        KWARN("kasset_material_model_to_kresource converting count - did you mean to do this?");
        return KRESOURCE_MATERIAL_MODEL_COUNT;
    case KASSET_MATERIAL_MODEL_CUSTOM:
        return KRESOURCE_MATERIAL_MODEL_CUSTOM;
    }
}
kasset_material_model kresource_material_model_to_kasset(kresource_material_model model) {
    switch (model) {
    case KRESOURCE_MATERIAL_MODEL_UNLIT:
        return KASSET_MATERIAL_MODEL_UNLIT;
    case KRESOURCE_MATERIAL_MODEL_PBR:
        return KASSET_MATERIAL_MODEL_PBR;
    case KRESOURCE_MATERIAL_MODEL_PHONG:
        return KASSET_MATERIAL_MODEL_PHONG;
    case KRESOURCE_MATERIAL_MODEL_COUNT:
        KWARN("kresource_material_model_to_kasset converting count - did you mean to do this?");
        return KASSET_MATERIAL_MODEL_COUNT;
    case KRESOURCE_MATERIAL_MODEL_CUSTOM:
        return KASSET_MATERIAL_MODEL_CUSTOM;
    }
}

kresource_material_texture_map_channel kasset_material_tex_map_channel_to_kresource(kasset_material_texture_map_channel channel) {
    switch (channel) {
    case KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_R:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_R;
    case KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_G:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_G;
    case KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_B:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_B;
    case KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_A:
        return KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_A;
    }
}
kasset_material_texture_map_channel kresource_material_tex_map_channel_to_kasset(kresource_material_texture_map_channel channel) {
    switch (channel) {
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_R:
        return KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_R;
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_G:
        return KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_G;
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_B:
        return KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_B;
    case KRESOURCE_MATERIAL_TEXTURE_MAP_CHANNEL_A:
        return KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_A;
    }
}

kresource_material_texture kasset_material_texture_to_kresource(kasset_material_texture texture) {
    kresource_material_texture t;
    t.resource_name = texture.resource_name;
    t.sampler_name = texture.sampler_name;
    t.package_name = texture.package_name;
    t.channel = kasset_material_tex_map_channel_to_kresource(texture.channel);
    return t;
}
kasset_material_texture kresource_material_texture_to_kasset(kresource_material_texture texture) {
    kasset_material_texture t;
    t.resource_name = texture.resource_name;
    t.sampler_name = texture.sampler_name;
    t.package_name = texture.package_name;
    t.channel = kresource_material_tex_map_channel_to_kasset(texture.channel);
    return t;
}

kresource_material_sampler kasset_material_sampler_to_kresource(kasset_material_sampler sampler) {
    kresource_material_sampler s;
    s.name = sampler.name;
    s.filter_mag = sampler.filter_mag;
    s.filter_min = sampler.filter_min;
    s.repeat_u = sampler.repeat_u;
    s.repeat_v = sampler.repeat_v;
    s.repeat_w = sampler.repeat_w;
    return s;
}
kasset_material_sampler kresource_material_sampler_to_kasset(kresource_material_sampler sampler) {
    kasset_material_sampler s;
    s.name = sampler.name;
    s.filter_mag = sampler.filter_mag;
    s.filter_min = sampler.filter_min;
    s.repeat_u = sampler.repeat_u;
    s.repeat_v = sampler.repeat_v;
    s.repeat_w = sampler.repeat_w;
    return s;
}
