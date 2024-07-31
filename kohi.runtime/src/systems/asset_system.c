#include "asset_system.h"
#include "assets/handlers/asset_handler.h"
#include "assets/kasset_utils.h"
#include "defines.h"
#include "identifiers/identifier.h"

#include <assets/kasset_types.h>
#include <complex.h>
#include <containers/darray.h>
#include <containers/hashtable.h>
#include <debug/kassert.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <strings/kstring.h>

typedef struct asset_lookup {
    // The asset itself, owned by this lookup.
    kasset asset;
    // The current number of references to the asset.
    i32 reference_count;
    // Indicates if the asset will be released when the reference_count reaches 0.
    b8 auto_release;
} asset_lookup;

typedef struct asset_system_state {
    // Max number of assets that can be loaded at any given time.
    u32 max_asset_count;
    // An array of lookups which contain reference and release data.
    asset_lookup* lookups;
    // hashtable to find lookups by name.
    hashtable lookup_table;
    // Block of memory for the lookup hashtable.
    void* lookup_table_block;

    // An array of handlers for various asset types.
    // TODO: This does not allow for user types, but for now this is fine.
    asset_handler handlers[KASSET_TYPE_MAX];
} asset_system_state;

static void asset_system_release_internal(struct asset_system_state* state, const char* fully_qualified_name, b8 force_release);

b8 asset_system_initialize(u64* memory_requirement, struct asset_system_state* state, const asset_system_config* config) {
    if (!memory_requirement) {
        KERROR("asset_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }

    *memory_requirement = sizeof(asset_system_state);

    // Just doing a memory size lookup, don't count as a failure.
    if (!state) {
        return true;
    } else if (!config) {
        KERROR("asset_system_initialize: A pointer to valid configuration is required. Initialization failed.");
        return false;
    }

    state->max_asset_count = config->max_asset_count;

    // Asset lookup table.
    {
        state->lookups = kallocate(sizeof(asset_lookup) * state->max_asset_count, MEMORY_TAG_ARRAY);
        state->lookup_table_block = kallocate(sizeof(u32) * state->max_asset_count, MEMORY_TAG_HASHTABLE);
        hashtable_create(sizeof(u32), state->max_asset_count, state->lookup_table_block, false, &state->lookup_table);

        // Invalidate all entries in the lookup table.
        u32 invalid = INVALID_ID;
        if (!hashtable_fill(&state->lookup_table, &invalid)) {
            KERROR("asset_system_initialize: Failed to fill lookup table with invalid ids at init. Initialization failed.");
            return false;
        }

        // Invalidate all lookups.
        for (u32 i = 0; i < state->max_asset_count; ++i) {
            state->lookups[i].asset.id.uniqueid = INVALID_ID_U64;
        }
    }

    return true;
}

void asset_system_shutdown(struct asset_system_state* state) {
    if (state) {
        if (state->lookups) {
            // Unload all currently-held lookups.
            for (u32 i = 0; i < state->max_asset_count; ++i) {
                asset_lookup* lookup = &state->lookups[i];
                if (lookup->asset.id.uniqueid != INVALID_ID_U64) {
                    // Force release the asset.
                    asset_system_release_internal(state, lookup->asset.meta.name.fully_qualified_name, true);
                }
            }
            kfree(state->lookups, sizeof(asset_lookup) * state->max_asset_count, MEMORY_TAG_ARRAY);
        }

        hashtable_destroy(&state->lookup_table);
        if (state->lookup_table_block) {
            kfree(state->lookup_table_block, sizeof(u32) * state->max_asset_count, MEMORY_TAG_HASHTABLE);
        }

        kzero_memory(state, sizeof(asset_system_state));
    }
}

void asset_system_request(struct asset_system_state* state, const char* fully_qualified_name, b8 auto_release, void* listener_instance, PFN_kasset_on_result callback) {
    KASSERT(state);
    // Lookup the asset by fully-qualified name.
    u32 lookup_index = INVALID_ID;
    if (hashtable_get(&state->lookup_table, fully_qualified_name, &lookup_index) && lookup_index != INVALID_ID) {
        // Valid entry found, increment the reference count and immediately make the callback.
        asset_lookup* lookup = &state->lookups[lookup_index];
        lookup->reference_count++;
        lookup->asset.generation++;
        if (callback) {
            callback(ASSET_REQUEST_RESULT_SUCCESS, &lookup->asset, listener_instance);
        }
    } else {
        // Before requesting the new asset, get it registered in the lookup in case anything
        // else requests it while it is still being loaded.
        // Search for an empty slot;
        for (u32 i = 0; i < state->max_asset_count; ++i) {
            asset_lookup* lookup = &state->lookups[i];
            if (lookup->asset.id.uniqueid == INVALID_ID_U64) {

                if (!hashtable_set(&state->lookup_table, fully_qualified_name, &i)) {
                    KERROR("asset_system_request was unable to set an entry into the lookup table for asset '%s'.", fully_qualified_name);
                    callback(ASSET_REQUEST_RESULT_INTERNAL_FAILURE, 0, listener_instance);
                    return;
                }

                // Found a free slot, setup the asset.
                lookup->asset.id = identifier_create();
                // Parse the asset name into parts.
                kasset_util_parse_name(fully_qualified_name, &lookup->asset.meta.name);

                // Get the appropriate asset handler for the type and request the asset.
                asset_handler* handler = &state->handlers[lookup->asset.meta.asset_type];
                if (!handler->request_asset) {
                    KERROR("No handler setup for asset type %d, fully_qualified_name='%s'", lookup->asset.meta.asset_type, fully_qualified_name);
                    callback(ASSET_REQUEST_RESULT_NO_HANDLER, 0, listener_instance);
                }
                handler->request_asset(handler, &lookup->asset, callback);

                return;
            }
        }
        // If this point is reached, it is not possible to register any more assets. Config should be adjusted
        // to handle more entries.
        KFATAL("The asset system has reached maximum capacity of allowed assets (%d). Please adjust configuration to allow for more if needed.", state->max_asset_count);
        callback(ASSET_REQUEST_RESULT_INTERNAL_FAILURE, 0, listener_instance);
    }
}

static void asset_system_release_internal(struct asset_system_state* state, const char* fully_qualified_name, b8 force_release) {
    if (state) {
        // Lookup the asset by fully-qualified name.
        u32 lookup_index = INVALID_ID;
        if (hashtable_get(&state->lookup_table, fully_qualified_name, &lookup_index) && lookup_index != INVALID_ID) {
            // Valid entry found, decrement the reference count.
            asset_lookup* lookup = &state->lookups[lookup_index];
            lookup->reference_count--;
            if (force_release || (lookup->reference_count < 1 && lookup->auto_release)) {
                // Auto release set and criteria met.
                // TODO: call asset handler's 'unload' function.
                //
                // Ensure the lookup is invalidated.
                lookup->asset.id.uniqueid = INVALID_ID_U64;
                lookup->asset.generation = INVALID_ID;
                lookup->reference_count = 0;
                lookup->auto_release = false;
            }
        } else {
            // Entry not found, nothing to do.
            KWARN("asset_system_release: Attempted to release an asset which does not exist or is not already loaded. Nothing to do.");
        }
    }
}

void asset_system_release(struct asset_system_state* state, const char* name) {
    asset_system_release_internal(state, name, false);
}

void asset_system_on_handler_result(struct asset_system_state* state, asset_request_result result, kasset* asset, void* listener_instance, PFN_kasset_on_result callback) {
    if (state && asset) {
        switch (result) {
        case ASSET_REQUEST_RESULT_SUCCESS: {
            // See if the asset already exists first.
            u32 lookup_index = INVALID_ID;
            if (hashtable_get(&state->lookup_table, asset->meta.name.fully_qualified_name, &lookup_index) && lookup_index != INVALID_ID) {
                // Valid entry found, increment the reference count and immediately make the callback.
                asset_lookup* lookup = &state->lookups[lookup_index];
                lookup->reference_count++;
                lookup->asset.generation++;
                if (callback) {
                    callback(ASSET_REQUEST_RESULT_SUCCESS, &lookup->asset, listener_instance);
                }
            } else {
            }

        } break;
        case ASSET_REQUEST_RESULT_INVALID_PACKAGE:
            KERROR("Asset '%s' load failed: An invalid package was specified.", asset->meta.name.fully_qualified_name);
            break;
        case ASSET_REQUEST_RESULT_INVALID_NAME:
            KERROR("Asset '%s' load failed: An invalid asset name was specified.", asset->meta.name.fully_qualified_name);
            break;
        case ASSET_REQUEST_RESULT_INVALID_ASSET_TYPE:
            KERROR("Asset '%s' load failed: An invalid asset type was specified.", asset->meta.name.fully_qualified_name);
            break;
        case ASSET_REQUEST_RESULT_PARSE_FAILED:
            KERROR("Asset '%s' load failed: The parsing stage of the asset load failed.", asset->meta.name.fully_qualified_name);
            break;
        case ASSET_REQUEST_RESULT_GPU_UPLOAD_FAILED:
            KERROR("Asset '%s' load failed: The GPU-upload stage of the asset load failed.", asset->meta.name.fully_qualified_name);
            break;
        default:
            KERROR("Asset '%s' load failed: An unspecified error has occurred.", asset->meta.name.fully_qualified_name);
            break;
        }
    }
}
