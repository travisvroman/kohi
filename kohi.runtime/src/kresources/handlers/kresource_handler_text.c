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
#include "core/engine.h"

static void text_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void text_kasset_on_hot_reload(asset_request_result result, const struct kasset* asset, void* listener_inst);

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
    }
}

KAPI b8 kresource_handler_text_handle_hot_reload(struct kresource_handler* self, kresource* resource, kasset* asset, u32 file_watch_id) {
    if (resource && asset) {
        kresource_text* typed_resource = (kresource_text*)resource;
        kasset_text* typed_asset = (kasset_text*)asset;

        if (typed_resource->text) {
            string_free(typed_resource->text);
            typed_resource->text = 0;
        }

        typed_resource->text = string_duplicate(typed_asset->content);

        return true;
    }

    return false;
}

static void text_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    kresource_text* typed_resource = (kresource_text*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        typed_resource->text = string_duplicate(((kasset_text*)asset)->content);
        typed_resource->asset_file_watch_id = asset->file_watch_id;
        typed_resource->base.generation++;
        kresource_system_register_for_hot_reload(engine_systems_get()->kresource_state, (kresource*)typed_resource, asset->file_watch_id);
    } else {
        KERROR("Failed to load a required asset for text resource '%s'.", kname_string_get(typed_resource->base.name));
    }
}
