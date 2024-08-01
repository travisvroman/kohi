#pragma once

#include "systems/asset_system.h"

struct kasset;
struct vfs_state;

typedef struct asset_handler {
    kasset_type type;
    const char* type_name;

    /** @brief Cache a pointer to the VFS state for fast lookup. */
    struct vfs_state* vfs;

    /**
     * @brief Requests an asset from the given handler.
     */
    void (*request_asset)(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback);

    void (*release_asset)(struct asset_handler* self, struct kasset* asset);

} asset_handler;

typedef struct asset_handler_request_context {
    struct asset_handler* handler;
    void* listener_instance;
    PFN_kasset_on_result user_callback;
    struct kasset* asset;
} asset_handler_request_context;