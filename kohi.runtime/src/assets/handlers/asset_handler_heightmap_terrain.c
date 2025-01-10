#include "asset_handler_heightmap_terrain.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_heightmap_terrain_serializer.h>
#include <strings/kstring.h>

void asset_handler_heightmap_terrain_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->is_binary = false;
    self->request_asset = 0;
    self->release_asset = asset_handler_heightmap_terrain_release_asset;
    self->type = KASSET_TYPE_HEIGHTMAP_TERRAIN;
    self->type_name = KASSET_TYPE_NAME_HEIGHTMAP_TERRAIN;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_heightmap_terrain_serialize;
    self->text_deserialize = kasset_heightmap_terrain_deserialize;
}

void asset_handler_heightmap_terrain_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_heightmap_terrain* typed_asset = (kasset_heightmap_terrain*)asset;
    if (typed_asset->material_count && typed_asset->material_names) {
        kfree(typed_asset->material_names, sizeof(kname) * typed_asset->material_count, MEMORY_TAG_ARRAY);
        typed_asset->material_names = 0;
        typed_asset->material_count = 0;
    }
}
