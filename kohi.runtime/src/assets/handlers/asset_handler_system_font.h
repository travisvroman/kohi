#pragma once

#include <assets/asset_handler_types.h>

KAPI void asset_handler_system_font_create(struct asset_handler* self, struct vfs_state* vfs);
KAPI void asset_handler_system_font_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback);
KAPI void asset_handler_system_font_release_asset(struct asset_handler* self, struct kasset* asset);
