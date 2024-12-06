#include "kresource_handler_text.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <strings/kname.h>

#include "containers/darray.h"
#include "core/event.h"
#include "kresources/kresource_types.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

static void text_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_text* asset, kresource_text* out_text);
static void text_kasset_on_hot_reload(asset_request_result result, const struct kasset* asset, void* listener_inst);

kresource* kresource_handler_text_allocate(void) {
    return (kresource*)KALLOC_TYPE(kresource_text, MEMORY_TAG_RESOURCE);
}

b8 kresource_handler_text_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_text_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_text* typed_resource = (kresource_text*)resource;
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
    request_info.callback = text_kasset_on_result;
    request_info.synchronous = true;
    request_info.hot_reload_callback = text_kasset_on_hot_reload;
    request_info.hot_reload_context = typed_resource;
    request_info.import_params_size = 0;
    request_info.import_params = 0;
    asset_system_request(self->asset_system, request_info);

    return true;
}

void kresource_handler_text_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_text* typed_resource = (kresource_text*)resource;

        if (typed_resource->text) {
            string_free(typed_resource->text);
            typed_resource->text = 0;
        }

        KFREE_TYPE(typed_resource, kresource_text, MEMORY_TAG_RESOURCE);
    }
}

static void text_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    kresource_text* typed_resource = (kresource_text*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        typed_resource->text = string_duplicate(((kasset_text*)asset)->content);
        if (asset->file_watch_id != INVALID_ID) {
            if (!typed_resource->base.asset_file_watch_ids) {
                typed_resource->base.asset_file_watch_ids = darray_create(u32);
            }
            darray_push(typed_resource->base.asset_file_watch_ids, asset->file_watch_id);
        }
        typed_resource->base.generation++;
    } else {
        KERROR("Failed to load a required asset for text resource '%s'.", kname_string_get(typed_resource->base.name));
    }
}

static void text_kasset_on_hot_reload(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    kresource_text* typed_resource = (kresource_text*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Free the old text data and replace it with the new data from the asset.
        if (typed_resource->text) {
            string_free(typed_resource->text);
        }
        typed_resource->text = string_duplicate(((kasset_text*)asset)->content);

        // TODO: Implement #include directives here at this level so it's handled the same
        // regardless of what backend is being used.

        typed_resource->base.generation++;

        // Fire off an event that this asset has hot-reloaded.
        // Sender should be a pointer to the resource itself.
        event_context context = {0};
        context.data.u32[0] = asset->file_watch_id;
        event_fire(EVENT_CODE_RESOURCE_HOT_RELOADED, (void*)typed_resource, context);
    } else {
        KWARN("Hot reload was triggered for text resource '%s', but was unsuccessful. See logs for details.", kname_string_get(typed_resource->base.name));
    }
}
