#include "asset_handler_material.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_material_serializer.h>
#include <strings/kstring.h>

#include "systems/asset_system.h"
#include "systems/material_system.h"

void asset_handler_material_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->request_asset = asset_handler_material_request_asset;
    self->release_asset = asset_handler_material_release_asset;
    self->type = KASSET_TYPE_MATERIAL;
    self->type_name = KASSET_TYPE_NAME_MATERIAL;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_material_serialize;
    self->text_deserialize = kasset_material_deserialize;
}

void asset_handler_material_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback) {
    struct vfs_state* vfs_state = engine_systems_get()->vfs_system_state;
    // Create and pass along a context.
    // NOTE: The VFS takes a copy of this context, so the lifecycle doesn't matter.
    asset_handler_request_context context = {0};
    context.asset = asset;
    context.handler = self;
    context.listener_instance = listener_instance;
    context.user_callback = user_callback;
    vfs_request_asset(vfs_state, &asset->meta, false, false, sizeof(asset_handler_request_context), &context, asset_handler_base_on_asset_loaded);
}

void asset_handler_material_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_material* typed_asset = (kasset_material*)asset;
    if (typed_asset->name) {
        string_free(typed_asset->name);
        typed_asset->name = 0;
    }
    if (typed_asset->map_count && typed_asset->maps) {
        for (u32 i = 0; i < typed_asset->map_count; ++i) {
            kasset_material_map* map = &typed_asset->maps[i];
            if (map->name) {
                string_free(map->name);
            }
            if (map->image_asset_name) {
                string_free(map->image_asset_name);
            }
        }
        kfree(typed_asset->maps, sizeof(kasset_material_map) * typed_asset->map_count, MEMORY_TAG_ARRAY);
        typed_asset->maps = 0;
        typed_asset->map_count = 0;
    }
    if (typed_asset->property_count && typed_asset->properties) {
        for (u32 i = 0; i < typed_asset->property_count; ++i) {
            kasset_material_property* prop = &typed_asset->properties[i];
            if (prop->name) {
                string_free(prop->name);
            }
        }
        kfree(typed_asset->properties, sizeof(kasset_material_property) * typed_asset->property_count, MEMORY_TAG_ARRAY);
        typed_asset->properties = 0;
        typed_asset->property_count = 0;
    }
}
