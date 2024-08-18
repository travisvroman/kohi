#include "kresource_handler_texture.h"
#include "containers/array.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

typedef struct texture_resource_handler_info {
    kresource* resource;
    kresource_handler* handler;
} texture_resource_handler_info;

static void texture_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);

b8 kresource_handler_texture_request(struct kresource_handler* self, kresource* resource, kresource_request_info info) {
    if (!self || !resource) {
        KERROR("kresource_handler_texture_request requires valid pointers to self and resource.");
        return false;
    }

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    texture_resource_handler_info* listener_inst = kallocate(sizeof(texture_resource_handler_info), MEMORY_TAG_RESOURCE);

    for (array_iterator it = info.assets.begin(&info.assets.base); !it.end(&it); it.next(&it)) {
        kresource_asset_info* asset_info = it.value(&it);
        asset_system_request(
            self->asset_system,
            asset_info->type,
            asset_info->package_name,
            asset_info->asset_name,
            true,
            listener_inst,
            texture_kasset_on_result);
    }

    return true;
}

void kresource_handler_texture_release(struct kresource_handler* self, kresource* resource) {
    if (resource) {
        //
    }
}

static void texture_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    //
}
