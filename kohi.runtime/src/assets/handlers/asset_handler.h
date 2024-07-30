#pragma once

#include "systems/asset_system.h"

struct kasset;

typedef struct asset_handler {
    const char* type;

    /**
     * @brief Requests an asset from the given handler.
     */
    void (*request_asset)(struct asset_handler* self, struct kasset* asset, PFN_kasset_on_result user_callback);

    void (*release_asset)(struct asset_handler* self, struct kasset* asset);

} asset_handler;
