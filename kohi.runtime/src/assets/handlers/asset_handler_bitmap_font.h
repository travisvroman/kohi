#pragma once

#include <assets/asset_handler_types.h>

KAPI void asset_handler_bitmap_font_create(struct asset_handler* self, struct vfs_state* vfs);
KAPI void asset_handler_bitmap_font_release_asset(struct asset_handler* self, struct kasset* asset);
