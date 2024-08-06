#include "asset_handler_text.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <strings/kstring.h>

#include "systems/asset_system.h"
#include "systems/material_system.h"

void asset_handler_text_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->request_asset = asset_handler_text_request_asset;
    self->release_asset = asset_handler_text_release_asset;
    self->type = KASSET_TYPE_TEXT;
    self->type_name = KASSET_TYPE_NAME_TEXT;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = 0;
    self->text_deserialize = 0;
}

void asset_handler_text_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback) {
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

void asset_handler_text_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_text* typed_asset = (kasset_text*)asset;
    if (typed_asset->content) {
        string_free(typed_asset->content);
        typed_asset->content = 0;
    }
}
