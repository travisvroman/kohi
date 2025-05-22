#include "kresource_handler_text.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <strings/kname.h>

#include "kresources/kresource_types.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

b8 kresource_handler_text_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_text_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_text* typed_resource = (kresource_text*)resource;
    // Straight to loading state.
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    kresource_asset_info* asset_info = &info->assets.data[0];

    kasset_text* asset = asset_system_request_text_from_package_sync(self->asset_system, kname_string_get(asset_info->package_name), kname_string_get(asset_info->asset_name));
    if (!asset) {
        KERROR("%s - Failed to load asset. See logs for details.", __FUNCTION__);
        return false;
    }

    // Take a copy of the string.
    typed_resource->text = string_duplicate(asset->content);

    // Release the asset.
    asset_system_release_text(self->asset_system, asset);

    resource->state = KRESOURCE_STATE_LOADED;

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
