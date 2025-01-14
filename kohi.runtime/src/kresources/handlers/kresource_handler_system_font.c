#include "kresource_handler_system_font.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_system_font_serializer.h>
#include <strings/kname.h>

#include "kresources/kresource_types.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

typedef struct system_font_resource_handler_info {
    kresource_system_font* typed_resource;
    kresource_handler* handler;
    kresource_system_font_request_info* request_info;
    kasset_system_font* asset;
} system_font_resource_handler_info;

static void system_font_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_system_font* asset, kresource_system_font* out_system_font);

b8 kresource_handler_system_font_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_system_font_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_system_font* typed_resource = (kresource_system_font*)resource;
    kresource_system_font_request_info* typed_request = (kresource_system_font_request_info*)info;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    if (info->assets.base.length < 1) {
        KERROR("kresource_handler_system_font_request requires exactly one asset.");
        return false;
    }

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    system_font_resource_handler_info* listener_inst = kallocate(sizeof(system_font_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener_inst->request_info = kallocate(sizeof(kresource_system_font_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener_inst->request_info, typed_request, sizeof(kresource_system_font_request_info));
    listener_inst->typed_resource = typed_resource;
    listener_inst->handler = self;
    listener_inst->asset = 0;

    typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;

    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    kresource_asset_info* asset_info = &info->assets.data[0];

    asset_request_info request_info = {0};
    request_info.type = asset_info->type;
    request_info.asset_name = asset_info->asset_name;
    request_info.package_name = asset_info->package_name;
    request_info.auto_release = true;
    request_info.listener_inst = listener_inst;
    request_info.callback = system_font_kasset_on_result;
    request_info.synchronous = typed_request->base.synchronous;
    request_info.import_params_size = 0;
    request_info.import_params = 0;
    asset_system_request(self->asset_system, request_info);

    return true;
}

void kresource_handler_system_font_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_system_font* typed_resource = (kresource_system_font*)resource;

        if (typed_resource->faces && typed_resource->face_count) {
            KFREE_TYPE_CARRAY(typed_resource->faces, kname, typed_resource->face_count);
        }
    }
}

static void system_font_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    system_font_resource_handler_info* listener = (system_font_resource_handler_info*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Save off the asset pointer to the array.
        listener->asset = (kasset_system_font*)asset;

        asset_to_resource(listener->asset, listener->typed_resource);

        // Release the asset.
        asset_system_release(listener->handler->asset_system, asset->name, asset->package_name);
    } else {
        KERROR("Failed to load a required asset for system_font resource '%s'. Resource may not appear correctly when rendered.", kname_string_get(listener->typed_resource->base.name));
    }

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_system_font_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(system_font_resource_handler_info), MEMORY_TAG_RESOURCE);
}

static void asset_to_resource(const kasset_system_font* asset, kresource_system_font* out_system_font) {
    // Take a copy of all of the asset properties.

    out_system_font->ttf_asset_name = asset->ttf_asset_name;
    out_system_font->ttf_asset_package_name = asset->ttf_asset_package_name;
    out_system_font->face_count = asset->face_count;
    out_system_font->faces = KALLOC_TYPE_CARRAY(kname, out_system_font->face_count);
    KCOPY_TYPE_CARRAY(out_system_font->faces, asset->faces, kname, out_system_font->face_count);

    // NOTE: The binary should also have been loaded by this point. Take a copy of it.
    out_system_font->font_binary_size = asset->font_binary_size;
    out_system_font->font_binary = kallocate(out_system_font->font_binary_size, MEMORY_TAG_RESOURCE);
    kcopy_memory(out_system_font->font_binary, asset->font_binary, out_system_font->font_binary_size);

    out_system_font->base.state = KRESOURCE_STATE_LOADED;
}
