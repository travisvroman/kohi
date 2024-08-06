#include "asset_handler_kson.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/vfs.h>
#include <serializers/kasset_kson_serializer.h>
#include <strings/kstring.h>

#include "systems/asset_system.h"
#include "systems/material_system.h"

void asset_handler_kson_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->request_asset = asset_handler_kson_request_asset;
    self->release_asset = asset_handler_kson_release_asset;
    self->type = KASSET_TYPE_KSON;
    self->type_name = KASSET_TYPE_NAME_KSON;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_kson_serialize;
    self->text_deserialize = kasset_kson_deserialize;
}

void asset_handler_kson_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback) {
    struct vfs_state* vfs_state = engine_systems_get()->vfs_system_state;
    // Create and pass along a conkson.
    // NOTE: The VFS takes a copy of this conkson, so the lifecycle doesn't matter.
    asset_handler_request_context context = {0};
    context.asset = asset;
    context.handler = self;
    context.listener_instance = listener_instance;
    context.user_callback = user_callback;
    vfs_request_asset(vfs_state, &asset->meta, false, false, sizeof(asset_handler_request_context), &context, asset_handler_base_on_asset_loaded);
}

void asset_handler_kson_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_kson* typed_asset = (kasset_kson*)asset;
    if (typed_asset->source_text) {
        string_free(typed_asset->source_text);
        typed_asset->source_text = 0;
    }
    kson_tree_cleanup(&typed_asset->tree);
}
