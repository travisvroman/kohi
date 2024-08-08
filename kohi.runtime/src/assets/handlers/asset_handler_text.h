#pragma once

#include <assets/asset_handler_types.h>

KAPI void asset_handler_text_create(struct asset_handler* self, struct vfs_state* vfs);
KAPI void asset_handler_text_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback);
KAPI void asset_handler_text_release_asset(struct asset_handler* self, struct kasset* asset);