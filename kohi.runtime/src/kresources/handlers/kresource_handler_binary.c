#include "kresource_handler_binary.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <strings/kname.h>

#include "kresources/kresource_types.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

static void kasset_binary_on_result(void* listener_inst, kasset_binary* asset);

b8 kresource_handler_binary_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_binary_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_binary* typed_resource = (kresource_binary*)resource;
    // Straight to loading state.
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    kresource_asset_info* asset_info = &info->assets.data[0];

    kasset_binary* asset = asset_system_request_binary_from_package_sync(self->asset_system, kname_string_get(asset_info->package_name), kname_string_get(asset_info->asset_name));
    kasset_binary_on_result(typed_resource, asset);

    typed_resource->base.state = KRESOURCE_STATE_LOADED;

    asset_system_release_binary(self->asset_system, asset);

    return true;
}

void kresource_handler_binary_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_binary* typed_resource = (kresource_binary*)resource;

        if (typed_resource->bytes && typed_resource->size) {
            kfree(typed_resource->bytes, typed_resource->size, MEMORY_TAG_RESOURCE);
            typed_resource->bytes = 0;
            typed_resource->size = 0;
        }
    }
}

static void kasset_binary_on_result(void* listener_inst, kasset_binary* asset) {
    kresource_binary* typed_resource = (kresource_binary*)listener_inst;
    kasset_binary* typed_asset = (kasset_binary*)asset;

    typed_resource->bytes = kallocate(typed_asset->size, MEMORY_TAG_RESOURCE);
    kcopy_memory(typed_resource->bytes, typed_asset->content, typed_asset->size);

    typed_resource->base.generation++;
}
