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
    self->is_binary = false;
    self->request_asset = 0;
    self->release_asset = asset_handler_kson_release_asset;
    self->type = KASSET_TYPE_KSON;
    self->type_name = KASSET_TYPE_NAME_KSON;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_kson_serialize;
    self->text_deserialize = kasset_kson_deserialize;
}

void asset_handler_kson_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_kson* typed_asset = (kasset_kson*)asset;
    if (typed_asset->source_text) {
        string_free(typed_asset->source_text);
        typed_asset->source_text = 0;
    }
    kson_tree_cleanup(&typed_asset->tree);
}
