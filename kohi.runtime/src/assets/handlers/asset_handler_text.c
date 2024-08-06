#include "asset_handler_binary.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/vfs.h>
#include <strings/kstring.h>

#include "systems/asset_system.h"
#include "systems/material_system.h"

// fake a binary "serializer", which really just takes a copy of the data.
static void* kasset_binary_serialize(const kasset* asset, u64* out_size) {
    if (asset->type != KASSET_TYPE_BINARY) {
        KERROR("kasset_binary_serialize requires a binary asset to serialize.");
        return 0;
    }

    kasset_binary* typed_asset = (kasset_binary*)asset;

    void* out_data = kallocate(typed_asset->size, MEMORY_TAG_ASSET);
    *out_size = typed_asset->size;
    kcopy_memory(out_data, typed_asset->content, typed_asset->size);
    return out_data;
}

static b8 kasset_binary_deserialize(u64 size, const void* data, kasset* out_asset) {
    if (!data || !out_asset || !size) {
        KERROR("kasset_binary_deserialize requires valid pointers to data and out_asset as well as a nonzero size.");
        return false;
    }

    if (out_asset->type != KASSET_TYPE_BINARY) {
        KERROR("kasset_binary_serialize requires a binary asset to serialize.");
        return 0;
    }

    kasset_binary* typed_asset = (kasset_binary*)out_asset;
    typed_asset->size = size;
    void* content = kallocate(typed_asset->size, MEMORY_TAG_ASSET);
    kcopy_memory(content, data, typed_asset->size);
    typed_asset->content = content;

    return true;
}

void asset_handler_binary_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->request_asset = asset_handler_binary_request_asset;
    self->release_asset = asset_handler_binary_release_asset;
    self->type = KASSET_TYPE_BINARY;
    self->type_name = KASSET_TYPE_NAME_BINARY;
    self->binary_serialize = kasset_binary_serialize;
    self->binary_deserialize = kasset_binary_deserialize;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
}

void asset_handler_binary_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback) {
    struct vfs_state* vfs_state = engine_systems_get()->vfs_system_state;
    // Create and pass along a conbinary.
    // NOTE: The VFS takes a copy of this conbinary, so the lifecycle doesn't matter.
    asset_handler_request_context context = {0};
    context.asset = asset;
    context.handler = self;
    context.listener_instance = listener_instance;
    context.user_callback = user_callback;
    vfs_request_asset(vfs_state, &asset->meta, true, false, sizeof(asset_handler_request_context), &context, asset_handler_base_on_asset_loaded);
}

void asset_handler_binary_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_binary* typed_asset = (kasset_binary*)asset;
    if (typed_asset->content) {
        string_free(typed_asset->content);
        typed_asset->content = 0;
    }
}
