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

static void binary_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_binary* asset, kresource_binary* out_binary);
static void binary_kasset_on_hot_reload(asset_request_result result, const struct kasset* asset, void* listener_inst);

kresource* kresource_handler_binary_allocate(void) {
    return (kresource*)KALLOC_TYPE(kresource_binary, MEMORY_TAG_RESOURCE);
}

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
    request_info.hot_reload_callback = binary_kasset_on_hot_reload;
    request_info.hot_reload_context = typed_resource;
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

        KFREE_TYPE(typed_resource, kresource_binary, MEMORY_TAG_RESOURCE);
    }
}

static void binary_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    kresource_binary* typed_resource = (kresource_binary*)listener_inst;
    kasset_binary* typed_asset = (kasset_binary*)asset;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {

        typed_resource->bytes = kallocate(typed_asset->size, MEMORY_TAG_RESOURCE);
        kcopy_memory(typed_resource->bytes, typed_asset->content, typed_asset->size);
        if (asset->file_watch_id != INVALID_ID) {
            if (!typed_resource->base.asset_file_watch_ids) {
                typed_resource->base.asset_file_watch_ids = darray_create(u32);
            }
            darray_push(typed_resource->base.asset_file_watch_ids, asset->file_watch_id);
        }
        typed_resource->base.generation++;
    } else {
        KERROR("Failed to load a required asset for binary resource '%s'.", kname_string_get(typed_resource->base.name));
    }
}

static void binary_kasset_on_hot_reload(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    kresource_binary* typed_resource = (kresource_binary*)listener_inst;
    kasset_binary* typed_asset = (kasset_binary*)asset;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Free the old binary data and replace it with the new data from the asset.
        if (typed_resource->bytes) {
            kfree(typed_resource->bytes, typed_resource->size, MEMORY_TAG_RESOURCE);
        }
        typed_resource->bytes = kallocate(typed_asset->size, MEMORY_TAG_RESOURCE);
        kcopy_memory(typed_resource->bytes, typed_asset->content, typed_asset->size);

        typed_resource->base.generation++;

        // Fire off an event that this asset has hot-reloaded.
        // Sender should be a pointer to the resource itself.
        event_context context = {0};
        context.data.u32[0] = asset->file_watch_id;
        event_fire(EVENT_CODE_RESOURCE_HOT_RELOADED, (void*)typed_resource, context);
    } else {
        KWARN("Hot reload was triggered for binary resource '%s', but was unsuccessful. See logs for details.", kname_string_get(typed_resource->base.name));
    }
}
