

#include "assets/kasset_types.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

// The number of channels per PBR material.
// i.e. albedo, normal, metallic/roughness/AO combined
#define PBR_MATERIAL_CHANNEL_COUNT 3

typedef struct material_resource_handler_info {
    kresource_material* typed_resource;
    kresource_handler* handler;
    kresource_material_request_info* request_info;
    kasset_material* asset;
} material_resource_handler_info;

static void material_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);

kresource* kresource_handler_material_allocate(void) {
    return (kresource*)kallocate(sizeof(kresource_material), MEMORY_TAG_RESOURCE);
}

b8 kresource_handler_material_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_material_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_material* typed_resource = (kresource_material*)resource;
    kresource_material_request_info* typed_request = (kresource_material_request_info*)info;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    if (info->assets.base.length != 1) {
        KERROR("kresource_handler_material_request requires exactly one asset.");
        return false;
    }

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    material_resource_handler_info* listener_inst = kallocate(sizeof(material_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener_inst->request_info = kallocate(sizeof(kresource_material_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener_inst->request_info, typed_request, sizeof(kresource_material_request_info));
    listener_inst->typed_resource = typed_resource;
    listener_inst->handler = self;
    listener_inst->asset = 0;

    typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;

    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    kresource_asset_info* asset_info = &info->assets.data[0];
    asset_system_request(
        self->asset_system,
        asset_info->type,
        asset_info->package_name,
        asset_info->asset_name,
        true,
        listener_inst,
        material_kasset_on_result,
        0,
        0);

    return true;
}

void kresource_handler_material_release(kresource_handler* self, kresource* resource) {
}

static b8 process_asset_material_map(kname material_name, kasset_material_map* map, kresource_texture_map* target_map) {
    target_map->repeat_u = map->repeat_u;
    target_map->repeat_v = map->repeat_v;
    target_map->repeat_w = map->repeat_w;
    target_map->filter_minify = map->filter_min;
    target_map->filter_magnify = map->filter_mag;
    target_map->internal_id = 0;
    target_map->generation = INVALID_ID;
    target_map->mip_levels = 0; // TODO: Do we need this?
    if (!renderer_kresource_texture_map_resources_acquire(engine_systems_get()->renderer_system, target_map)) {
        KERROR("Failed to acquire texture map resources for material '%s', for the '%s' map.", material_name, map->name);
        return false;
    }

    target_map->texture = texture_system_request(
        map->image_asset_name,
        map->image_asset_package_name,
        0,
        0);

    return true;
}

typedef enum mra_state {
    MRA_STATE_UNINITIALIZED,
    MRA_STATE_REQUESTED,
    MRA_STATE_LOADED
} mra_state;

typedef enum mra_indices {
    MRA_INDEX_METALLIC = 0,
    MRA_INDEX_ROUGHNESS = 1,
    MRA_INDEX_AO = 2
} mra_indices;

typedef struct material_mra_data {
    mra_state state;
    kasset_material_map_channel channel;
    kname image_asset_name;
    kname image_asset_package_name;
    kasset_image* asset;
} material_mra_data;

typedef struct material_mra_listener {
    kresource_texture_map* metallic_roughness_ao_map;
    kasset_material_map metallic_roughness_ao_map_config;
    material_mra_data map_assets[3];
    kresource_material* typed_resource;
} material_mra_listener;

static void material_on_metallic_roughness_ao_image_asset_loaded(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        material_mra_listener* listener = (material_mra_listener*)listener_inst;

        // Test for which asset loaded.
        for (u32 i = 0; i < 3; ++i) {
            material_mra_data* m = &listener->map_assets[i];

            if (m->image_asset_name == asset->name) {
                m->state = MRA_STATE_LOADED;
                m->asset = (kasset_image*)asset;
                break;
            }
        }

        // Boot if we are waiting on an asset to finish loading.
        for (u32 i = 0; i < 3; ++i) {
            material_mra_data* m = &listener->map_assets[i];
            if (m->state == MRA_STATE_REQUESTED) {
                KTRACE("Still waiting on asset '%s'...", kname_string_get(m->image_asset_name));
                return;
            }
        }

        // This means everything that was request to load has loaded, and combination of asset channel data may begin.
        u32 width = U32_MAX, height = U32_MAX;
        u8* pixels = 0;
        u32 pixel_array_size = 0;
        for (u32 i = 0; i < 3; ++i) {
            material_mra_data* m = &listener->map_assets[i];
            if (m->state == MRA_STATE_LOADED) {
                if (width == U32_MAX || height == U32_MAX) {
                    width = m->asset->width;
                    height = m->asset->height;
                    pixel_array_size = sizeof(u8) * width * height * 4;
                    pixels = kallocate(pixel_array_size, MEMORY_TAG_RESOURCE);
                } else if (width != m->asset->width || height != m->asset->height) {
                    KWARN("All assets for material metallic, roughness and AO maps must be the same resolution. Default data will be used instead.");
                    // Use default data instead by releasing the asset and resetting the state.
                    asset_system_release(engine_systems_get()->asset_state, m->image_asset_name, m->image_asset_package_name);
                    m->asset = 0;
                    m->state = MRA_STATE_UNINITIALIZED;
                }
            } else if (m->state == MRA_STATE_UNINITIALIZED) {
                // TODO: Use default data instead.
            }
        }

        if (!pixels) {
            KERROR("Pixel array not created during asset load for material. This likely means other errors have occurred. Check logs.");
            return;
        }

        for (u32 i = 0; i < 3; ++i) {
            material_mra_data* m = &listener->map_assets[i];
            if (m->state == MRA_STATE_LOADED) {
                u8 offset = 0;
                switch (m->channel) {
                default:
                case KASSET_MATERIAL_MAP_CHANNEL_METALLIC:
                    offset = 0;
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_ROUGHNESS:
                    offset = 1;
                    break;

                case KASSET_MATERIAL_MAP_CHANNEL_AO:
                    offset = 2;
                    break;
                }
                for (u64 row = 0; row < height; ++row) {
                    for (u64 col = 0; col < width; ++col) {
                        u64 index = (row * width) + col;
                        u64 index_bpp = index * 4;
                        pixels[index_bpp + offset] = m->asset->pixels[index_bpp + 0]; // Pull from the red channel
                    }
                }

            } else if (m->state == MRA_STATE_UNINITIALIZED) {
                // Use default data instead.
                u32 offset = 0;
                u8 value = 0;
                switch (m->channel) {
                default:
                case KASSET_MATERIAL_MAP_CHANNEL_METALLIC:
                    offset = 0;
                    value = 0; // Default for metallic is black.
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_ROUGHNESS:
                    offset = 1;
                    value = 128; // Default for roughness is medium grey
                    break;

                case KASSET_MATERIAL_MAP_CHANNEL_AO:
                    offset = 2;
                    value = 255; // Default for AO is white.
                    break;
                }

                for (u64 row = 0; row < height; ++row) {
                    for (u64 col = 0; col < width; ++col) {
                        u64 index = (row * width) + col;
                        u64 index_bpp = index * 4;
                        pixels[index_bpp + offset] = value;
                    }
                }
            }
        }

        if (!listener->metallic_roughness_ao_map->texture) {

            kresource_texture_map* target_map = listener->metallic_roughness_ao_map;
            target_map->repeat_u = listener->metallic_roughness_ao_map_config.repeat_u;
            target_map->repeat_v = listener->metallic_roughness_ao_map_config.repeat_v;
            target_map->repeat_w = listener->metallic_roughness_ao_map_config.repeat_w;
            target_map->filter_minify = listener->metallic_roughness_ao_map_config.filter_min;
            target_map->filter_magnify = listener->metallic_roughness_ao_map_config.filter_mag;
            target_map->internal_id = 0;
            target_map->generation = INVALID_ID;
            target_map->mip_levels = 0; // TODO: Do we need this?

            // Setup texture map resources.
            if (!renderer_kresource_texture_map_resources_acquire(engine_systems_get()->renderer_system, target_map)) {
                KERROR("Failed to acquire texture map resources for material '%s', for the '%s' map.", kname_string_get(listener->typed_resource->base.name), listener->metallic_roughness_ao_map_config.name);
                return;
            }

            const char* map_name = string_format("%s_metallic_roughness_ao_generated", listener->typed_resource->base.name);
            listener->typed_resource->metallic_roughness_ao_map.texture = texture_system_request_writeable(
                kname_create(map_name),
                width, height, KRESOURCE_TEXTURE_FORMAT_RGBA8, false, false);
            string_free(map_name);

            if (!texture_system_write_data((kresource_texture*)listener->metallic_roughness_ao_map->texture, 0, pixel_array_size, pixels)) {
                KERROR("Failed to upload combined texture data for material resource '%s'", kname_string_get(listener->typed_resource->base.name));
                return;
            }

            KTRACE("Successfully uploaded combined texture data for material resource '%s'", kname_string_get(listener->typed_resource->base.name));
        }

    } else {
        KERROR("Asset failed to load. See logs for details.");
    }
}

static void material_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    material_resource_handler_info* listener = (material_resource_handler_info*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Save off the asset pointer to the array.
        listener->asset = (kasset_material*)asset;

        // TODO: Need to think about hot-reloading here, and how/where listening should happen. Maybe in the resource system?

        listener->typed_resource->type = KRESOURCE_MATERIAL_TYPE_UNKNOWN;

        // Examine the material asset's type.
        switch (listener->asset->type) {
        default:
        case KMATERIAL_TYPE_UNKNOWN:
        case KMATERIAL_TYPE_COUNT:
            KERROR("Unknown material type cannot be processed.");
            return;
        case KMATERIAL_TYPE_UNLIT:
            for (u32 i = 0; i < listener->asset->property_count; ++i) {
                kasset_material_property* prop = &listener->asset->properties[i];

                if (prop->name == kname_create("diffuse_colour")) {
                    listener->typed_resource->diffuse_colour = prop->value.v4;
                }
            }
            for (u32 i = 0; i < listener->asset->map_count; ++i) {
                kasset_material_map* map = &listener->asset->maps[i];
                if (map->channel == KASSET_MATERIAL_MAP_CHANNEL_DIFFUSE) {
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->albedo_diffuse_map)) {
                        KERROR("Failed to process material map. See logs for details.");
                    }
                } else {
                    KERROR("An unlit material does not use a map of '%u' type (name='%s'). Skipping.", map->channel, map->name);
                    continue;
                }
            }

            break;
        case KMATERIAL_TYPE_PHONG:
            for (u32 i = 0; i < listener->asset->property_count; ++i) {
                kasset_material_property* prop = &listener->asset->properties[i];

                if (prop->name == kname_create("diffuse_colour")) {
                    listener->typed_resource->diffuse_colour = prop->value.v4;
                } else if (prop->name == kname_create("specular_strength")) {
                    listener->typed_resource->specular_strength = prop->value.f32;
                } else {
                    KWARN("Property '%s' for material '%s' not recognized. Skipping.", kname_string_get(prop->name), kname_string_get(listener->typed_resource->base.name));
                }
            }
            for (u32 i = 0; i < listener->asset->map_count; ++i) {
                kasset_material_map* map = &listener->asset->maps[i];
                switch (map->channel) {
                case KASSET_MATERIAL_MAP_CHANNEL_DIFFUSE:
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->albedo_diffuse_map)) {
                        KERROR("Failed to process material map '%s'. See logs for details.", map->name);
                    }
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_NORMAL:
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->normal_map)) {
                        KERROR("Failed to process material map '%s'. See logs for details.", map->name);
                    }
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_SPECULAR:
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->specular_map)) {
                        KERROR("Failed to process material map '%s'. See logs for details.", map->name);
                    }
                default:
                    KERROR("An unlit material does not use a map of '%u' type (name='%s'). Skipping.", map->channel, map->name);
                    break;
                }
            }
            break;
        case KMATERIAL_TYPE_PBR: {
            for (u32 i = 0; i < listener->asset->property_count; ++i) {
                kasset_material_property* prop = &listener->asset->properties[i];

                /* if (prop->name == kname_create("diffuse_colour")) {
                    listener->typed_resource->diffuse_colour = prop->value.v4;
                } else if (prop->name == kname_create("specular_strength")) {
                    listener->typed_resource->specular_strength = prop->value.f32;
                } else { */
                KWARN("Property '%s' for material '%s' not recognized. Skipping.", kname_string_get(prop->name), kname_string_get(listener->typed_resource->base.name));
                /* } */
            }

            material_mra_listener* listener_inst = 0;
            for (u32 i = 0; i < listener->asset->map_count; ++i) {
                kasset_material_map* map = &listener->asset->maps[i];
                switch (map->channel) {
                case KASSET_MATERIAL_MAP_CHANNEL_ALBEDO:
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->albedo_diffuse_map)) {
                        KERROR("Failed to process material map '%s'. See logs for details.", map->name);
                    }
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_NORMAL:
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->normal_map)) {
                        KERROR("Failed to process material map '%s'. See logs for details.", map->name);
                    }
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_EMISSIVE:
                    if (!process_asset_material_map(listener->typed_resource->base.name, map, &listener->typed_resource->emissive_map)) {
                        KERROR("Failed to process material map '%s'. See logs for details.", map->name);
                    }
                    break;
                case KASSET_MATERIAL_MAP_CHANNEL_METALLIC:
                case KASSET_MATERIAL_MAP_CHANNEL_ROUGHNESS:
                case KASSET_MATERIAL_MAP_CHANNEL_AO: {
                    if (!listener_inst) {
                        listener_inst = kallocate(sizeof(material_mra_listener), MEMORY_TAG_RESOURCE);
                        listener_inst->metallic_roughness_ao_map = &listener->typed_resource->metallic_roughness_ao_map;
                        listener_inst->metallic_roughness_ao_map_config = listener->asset->maps[i];
                    }
                    u32 index;
                    switch (map->channel) {
                    default:
                    case KASSET_MATERIAL_MAP_CHANNEL_METALLIC:
                        index = MRA_INDEX_METALLIC;
                        break;
                    case KASSET_MATERIAL_MAP_CHANNEL_ROUGHNESS:
                        index = MRA_INDEX_ROUGHNESS;
                        break;
                    case KASSET_MATERIAL_MAP_CHANNEL_AO:
                        index = MRA_INDEX_AO;
                        break;
                    }

                    material_mra_data* m = &listener_inst->map_assets[index];
                    m->state = MRA_STATE_REQUESTED;
                    m->channel = map->channel;
                    m->image_asset_package_name = map->image_asset_package_name;
                    m->image_asset_name = map->image_asset_name;
                    break;
                default:
                    KERROR("An unlit material does not use a map of '%u' type (name='%s'). Skipping.", map->channel, map->name);
                    break;
                }
                }

                // Perform the actual asset requests for the "combined" metallic/roughness/ao map.
                // This must be done after flags are flipped because this process is asynchronous and can otherwise
                // cause a race condition.
                if (listener_inst) {
                    for (u32 i = 0; i < PBR_MATERIAL_CHANNEL_COUNT; ++i) {
                        material_mra_data* m = &listener_inst->map_assets[i];

                        // Request a writeable texture to use as the final combined metallic/roughness/ao map.
                        asset_system_request(
                            engine_systems_get()->asset_state,
                            KASSET_TYPE_IMAGE,
                            m->image_asset_package_name,
                            m->image_asset_name,
                            true,
                            listener_inst,
                            material_on_metallic_roughness_ao_image_asset_loaded,
                            0,
                            0);
                    }
                }
            }

            // Acquire instance resources from the PBR shader.
            kresource_material* m = listener->typed_resource;

            kresource_texture_map* material_maps[PBR_MATERIAL_CHANNEL_COUNT] = {&m->albedo_diffuse_map,
                                                                                &m->normal_map,
                                                                                &m->metallic_roughness_ao_map};

            u32 pbr_shader_id = shader_system_get_id("Shader.PBRMaterial");
            if (!shader_system_shader_group_acquire(pbr_shader_id, PBR_MATERIAL_CHANNEL_COUNT, material_maps, &m->group_id)) {
                KASSERT_MSG(false, "Failed to group acquire renderer resources for default PBR material. Application cannot continue.");
            }

        } break;
        case KMATERIAL_TYPE_CUSTOM:
            KASSERT_MSG(false, "custom material type not yet supported.");
            return;
        case KMATERIAL_TYPE_PBR_WATER:
            KASSERT_MSG(false, "water material type not yet supported.");
            return;
        }

        listener->typed_resource->base.state = KRESOURCE_STATE_LOADED;
    } else {
        KERROR("Failed to load a required asset for material resource '%s'. Resource may not appear correctly when rendered.", kname_string_get(listener->typed_resource->base.name));
    }

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_material_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(material_resource_handler_info), MEMORY_TAG_RESOURCE);
}
