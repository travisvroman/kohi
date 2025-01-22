#include "kresource_handler_heightmap_terrain.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_heightmap_terrain_serializer.h>
#include <strings/kname.h>

#include "kresources/kresource_types.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

typedef struct heightmap_terrain_resource_handler_info {
    kresource_heightmap_terrain* typed_resource;
    kresource_handler* handler;
    kresource_heightmap_terrain_request_info* request_info;
    kasset_heightmap_terrain* asset;
} heightmap_terrain_resource_handler_info;

static void heightmap_terrain_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_heightmap_terrain* asset, kresource_heightmap_terrain* out_heightmap_terrain_resource);

b8 kresource_handler_heightmap_terrain_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_heightmap_terrain_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_heightmap_terrain* typed_resource = (kresource_heightmap_terrain*)resource;
    kresource_heightmap_terrain_request_info* typed_request = (kresource_heightmap_terrain_request_info*)info;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    if (info->assets.base.length == 0) {
        KERROR("kresource_handler_heightmap_terrain_request requires exactly one asset.");
        return false;
    }

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    heightmap_terrain_resource_handler_info* listener_inst = kallocate(sizeof(heightmap_terrain_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener_inst->request_info = kallocate(sizeof(kresource_heightmap_terrain_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener_inst->request_info, typed_request, sizeof(kresource_heightmap_terrain_request_info));
    listener_inst->typed_resource = typed_resource;
    listener_inst->handler = self;
    listener_inst->asset = 0;

    // Proceed straight to loading state.
    //   typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    // Request the heightmap_terrain config asset.
    kresource_asset_info* asset = &info->assets.data[0];

    asset_request_info request_info = {0};
    request_info.type = KASSET_TYPE_HEIGHTMAP_TERRAIN;
    request_info.asset_name = asset->asset_name;
    request_info.package_name = asset->package_name;
    request_info.auto_release = true;
    request_info.listener_inst = listener_inst;
    request_info.callback = heightmap_terrain_kasset_on_result;
    request_info.synchronous = typed_request->base.synchronous;
    request_info.import_params_size = 0;
    request_info.import_params = 0;

    asset_system_request(self->asset_system, request_info);

    return true;
}

void kresource_handler_heightmap_terrain_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_heightmap_terrain* typed_resource = (kresource_heightmap_terrain*)resource;

        if (typed_resource->material_names && typed_resource->material_count) {
            KFREE_TYPE_CARRAY(typed_resource->material_names, kname, typed_resource->material_count);
            typed_resource->material_names = 0;
        }
    }
}

static void heightmap_terrain_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    heightmap_terrain_resource_handler_info* listener = (heightmap_terrain_resource_handler_info*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Save off the asset pointer to the array.
        listener->asset = (kasset_heightmap_terrain*)asset;

        asset_to_resource(listener->asset, listener->typed_resource);
    } else {
        KERROR("Failed to load a required asset for heightmap_terrain resource '%s'. Resource may be incorrect.", kname_string_get(listener->typed_resource->base.name));
    }

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_heightmap_terrain_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(heightmap_terrain_resource_handler_info), MEMORY_TAG_RESOURCE);
}

static void asset_to_resource(const kasset_heightmap_terrain* asset, kresource_heightmap_terrain* out_heightmap_terrain_resource) {
    // Take a copy of all of the asset properties.

    out_heightmap_terrain_resource->base.name = asset->base.name;
    out_heightmap_terrain_resource->base.generation = 0;
    out_heightmap_terrain_resource->chunk_size = asset->chunk_size;
    out_heightmap_terrain_resource->tile_scale = asset->tile_scale;
    out_heightmap_terrain_resource->heightmap_asset_name = asset->heightmap_asset_name;
    out_heightmap_terrain_resource->heightmap_asset_package_name = asset->heightmap_asset_package_name;
    out_heightmap_terrain_resource->material_count = asset->material_count;
    out_heightmap_terrain_resource->material_names = KALLOC_TYPE_CARRAY(kname, asset->material_count);
    KCOPY_TYPE_CARRAY(out_heightmap_terrain_resource->material_names, asset->material_names, kname, asset->material_count);

    out_heightmap_terrain_resource->base.state = KRESOURCE_STATE_LOADED;
}
