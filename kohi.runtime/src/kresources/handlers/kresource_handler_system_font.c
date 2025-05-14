#include "kresource_handler_system_font.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_system_font_serializer.h>
#include <strings/kname.h>

#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

static void system_font_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_system_font* asset, kresource_system_font* out_system_font);

b8 kresource_handler_system_font_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_system_font_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_system_font* typed_resource = (kresource_system_font*)resource;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    if (info->assets.base.length < 1) {
        KERROR("kresource_handler_system_font_request requires exactly one asset.");
        return false;
    }

    typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    // Load the asset from disk synchronously.
    kasset_system_font* asset = asset_system_request_system_font_from_package_sync(
        engine_systems_get()->asset_state,
        kname_string_get(info->assets.data[0].package_name),
        kname_string_get(info->assets.data[0].asset_name));

    kresource_system_font* out_system_font = typed_resource;

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

    // Destroy the request.
    array_kresource_asset_info_destroy((array_kresource_asset_info*)&info->assets);

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
