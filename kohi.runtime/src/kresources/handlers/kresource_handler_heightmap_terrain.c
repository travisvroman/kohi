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

static void kasset_heightmap_terrain_on_result(void* listener_inst, struct kasset_heightmap_terrain* asset);

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
    heightmap_terrain_resource_handler_info* listener = kallocate(sizeof(heightmap_terrain_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener->request_info = kallocate(sizeof(kresource_heightmap_terrain_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener->request_info, typed_request, sizeof(kresource_heightmap_terrain_request_info));
    listener->typed_resource = typed_resource;
    listener->handler = self;
    listener->asset = 0;

    // Proceed straight to loading state.
    //   typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    // Request the heightmap_terrain config asset.
    kresource_asset_info* asset_info = &info->assets.data[0];

    kasset_heightmap_terrain* asset = asset_system_request_heightmap_terrain_from_package(
        self->asset_system,
        kname_string_get(asset_info->package_name),
        kname_string_get(asset_info->asset_name),
        listener,
        kasset_heightmap_terrain_on_result);
    if (!asset) {
        KERROR("Error loading static mesh asset. See logs for details.");
        return false;
    }

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

static void kasset_heightmap_terrain_on_result(void* listener_inst, struct kasset_heightmap_terrain* asset) {
    heightmap_terrain_resource_handler_info* listener = (heightmap_terrain_resource_handler_info*)listener_inst;
    // Save off the asset pointer to the array.
    listener->asset = (kasset_heightmap_terrain*)asset;

    listener->typed_resource->base.name = INVALID_KNAME; // FIXME: asset names? asset->name;
    listener->typed_resource->base.generation = 0;
    listener->typed_resource->chunk_size = asset->chunk_size;
    listener->typed_resource->tile_scale = asset->tile_scale;
    listener->typed_resource->heightmap_asset_name = asset->heightmap_asset_name;
    listener->typed_resource->heightmap_asset_package_name = asset->heightmap_asset_package_name;
    listener->typed_resource->material_count = asset->material_count;
    listener->typed_resource->material_names = KALLOC_TYPE_CARRAY(kname, asset->material_count);
    KCOPY_TYPE_CARRAY(listener->typed_resource->material_names, asset->material_names, kname, asset->material_count);

    listener->typed_resource->base.state = KRESOURCE_STATE_LOADED;

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_heightmap_terrain_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(heightmap_terrain_resource_handler_info), MEMORY_TAG_RESOURCE);
}
