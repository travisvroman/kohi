#include "kresource_handler_binary.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <strings/kname.h>

#include "containers/darray.h"
#include "core/event.h"
#include "kresources/kresource_types.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"
#include "core/engine.h"

static void binary_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);

b8 kresource_handler_binary_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_binary_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_binary* typed_resource = (kresource_binary*)resource;
    /* typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED; */
    /* typed_resource->base.state = KRESOURCE_STATE_INITIALIZED; */
    // Straight to loading state.
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    kresource_asset_info* asset_info = &info->assets.data[0];

    asset_request_info request_info = {0};
    request_info.type = asset_info->type;
    request_info.asset_name = asset_info->asset_name;
    request_info.package_name = asset_info->package_name;
    request_info.auto_release = true;
    request_info.listener_inst = typed_resource;
    request_info.callback = binary_kasset_on_result;
    request_info.synchronous = true;
    request_info.import_params_size = 0;
    request_info.import_params = 0;
    asset_system_request(self->asset_system, request_info);

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

b8 kresource_handler_binary_handle_hot_reload(struct kresource_handler* self, kresource* resource, kasset* asset, u32 file_watch_id) {
    if (resource && asset) {
        kresource_binary* typed_resource = (kresource_binary*)resource;
        kasset_binary* typed_asset = (kasset_binary*)asset;

        if (typed_resource->bytes && typed_resource->size) {
            kfree(typed_resource->bytes, typed_resource->size, MEMORY_TAG_RESOURCE);
            typed_resource->bytes = 0;
            typed_resource->size = 0;
        }

        typed_resource->size = typed_asset->size;
        typed_resource->bytes = kallocate(typed_asset->size, MEMORY_TAG_RESOURCE);
        kcopy_memory(typed_resource->bytes, typed_asset->content, typed_asset->size);

        return true;
    }

    return false;
}

static void binary_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    kresource_binary* typed_resource = (kresource_binary*)listener_inst;
    kasset_binary* typed_asset = (kasset_binary*)asset;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {

        typed_resource->bytes = kallocate(typed_asset->size, MEMORY_TAG_RESOURCE);
        kcopy_memory(typed_resource->bytes, typed_asset->content, typed_asset->size);

        typed_resource->base.generation++;
        kresource_system_register_for_hot_reload(engine_systems_get()->kresource_state, (kresource*)typed_resource, asset->file_watch_id);
    } else {
        KERROR("Failed to load a required asset for binary resource '%s'.", kname_string_get(typed_resource->base.name));
    }
}
