#include "asset_handler_image.h"

#include "systems/asset_system.h"

#include <assets/kasset_importer_registry.h>
#include <assets/kasset_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/vfs.h>
#include <serializers/kasset_binary_image_serializer.h>
#include <strings/kstring.h>

void asset_handler_image_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->is_binary = true;
    self->request_asset = 0;
    self->release_asset = asset_handler_image_release_asset;
    self->type = KASSET_TYPE_IMAGE;
    self->type_name = KASSET_TYPE_NAME_IMAGE;
    self->binary_serialize = kasset_binary_image_serialize;
    self->binary_deserialize = kasset_binary_image_deserialize;
    self->text_serialize = 0;
    self->text_deserialize = 0;
}

void asset_handler_image_release_asset(struct asset_handler* self, struct kasset* asset) {
    if (asset) {
        kasset_image* typed_asset = (kasset_image*)asset;
        // Asset type-specific data cleanup
        typed_asset->format = KASSET_IMAGE_FORMAT_UNDEFINED;
        typed_asset->width = 0;
        typed_asset->height = 0;
        typed_asset->mip_levels = 0;
        typed_asset->channel_count = 0;
    }
}
