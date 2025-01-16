#pragma once

#include <assets/asset_handler_types.h>

KAPI void asset_handler_kson_create(struct asset_handler* self, struct vfs_state* vfs);
KAPI void asset_handler_kson_release_asset(struct asset_handler* self, struct kasset* asset);
