
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
    self->is_binary = false;
    self->request_asset = 0;
    self->release_asset = asset_handler_text_release_asset;
    self->type = KASSET_TYPE_TEXT;
    self->type_name = KASSET_TYPE_NAME_TEXT;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = 0;
    self->text_deserialize = 0;
}

void asset_handler_text_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_text* typed_asset = (kasset_text*)asset;
    if (typed_asset->content) {
        string_free(typed_asset->content);
        typed_asset->content = 0;
    }
}
