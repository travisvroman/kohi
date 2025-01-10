#include "asset_system.h"
#include "core/engine.h"
// Known handler types
#include "assets/handlers/asset_handler_binary.h"
#include "assets/handlers/asset_handler_bitmap_font.h"
#include "assets/handlers/asset_handler_heightmap_terrain.h"
#include "assets/handlers/asset_handler_image.h"
#include "assets/handlers/asset_handler_kson.h"
#include "assets/handlers/asset_handler_material.h"
#include "assets/handlers/asset_handler_scene.h"
#include "assets/handlers/asset_handler_shader.h"
#include "assets/handlers/asset_handler_static_mesh.h"
#include "assets/handlers/asset_handler_system_font.h"
#include "assets/handlers/asset_handler_text.h"
#include "platform/vfs.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_types.h>
#include <assets/kasset_utils.h>
#include <containers/darray.h>
#include <containers/u64_bst.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/identifier.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <strings/kname.h>
#include <strings/kstring.h>

typedef struct asset_lookup {
    // The asset itself, owned by this lookup.
    kasset asset;
    // The current number of references to the asset.
    i32 reference_count;
    // Indicates if the asset will be released when the reference_count reaches 0.
    b8 auto_release;

    u32 file_watch_id;
    PFN_kasset_on_hot_reload hot_reload_callback;
    void* hot_reload_context;
} asset_lookup;

typedef struct asset_system_state {
    vfs_state* vfs;
    // Max number of assets that can be loaded at any given time.
    u32 max_asset_count;
    // An array of lookups which contain reference and release data.
    asset_lookup* lookups;
    // A BST to use for lookups of assets by name.
    bt_node* lookup_tree;

    // An array of handlers for various asset types.
    // TODO: This does not allow for user types, but for now this is fine.
    asset_handler handlers[KASSET_TYPE_MAX];
} asset_system_state;

/* static void on_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data); */
static void asset_system_release_internal(struct asset_system_state* state, kname asset_name, kname package_name, b8 force_release);

b8 asset_system_deserialize_config(const char* config_str, asset_system_config* out_config) {
    if (!config_str || !out_config) {
        KERROR("asset_system_deserialize_config requires a valid string and a pointer to hold the config.");
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(config_str, &tree)) {
        KERROR("Failed to parse asset system configuration.");
        return false;
    }

    // max_asset_count
    if (!kson_object_property_value_get_int(&tree.root, "max_asset_count", (i64*)&out_config->max_asset_count)) {
        KERROR("max_asset_count is a required field and was not provided.");
        return false;
    }

    return true;
}

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
    state->lookups = kallocate(sizeof(asset_lookup) * state->max_asset_count, MEMORY_TAG_ENGINE);

    // Asset lookup tree.
    {
        // NOTE: BST node created when first asset is requested.
        state->lookup_tree = 0;

        // Invalidate all lookups.
        for (u32 i = 0; i < state->max_asset_count; ++i) {
            state->lookups[i].asset.id.uniqueid = INVALID_ID_U64;
        }
    }

    state->vfs = engine_systems_get()->vfs_system_state;

    // Setup handlers for known types.
    asset_handler_heightmap_terrain_create(&state->handlers[KASSET_TYPE_HEIGHTMAP_TERRAIN], state->vfs);
    asset_handler_image_create(&state->handlers[KASSET_TYPE_IMAGE], state->vfs);
    asset_handler_static_mesh_create(&state->handlers[KASSET_TYPE_STATIC_MESH], state->vfs);
    asset_handler_material_create(&state->handlers[KASSET_TYPE_MATERIAL], state->vfs);
    asset_handler_text_create(&state->handlers[KASSET_TYPE_TEXT], state->vfs);
    asset_handler_kson_create(&state->handlers[KASSET_TYPE_KSON], state->vfs);
    asset_handler_binary_create(&state->handlers[KASSET_TYPE_BINARY], state->vfs);
    asset_handler_scene_create(&state->handlers[KASSET_TYPE_SCENE], state->vfs);
    asset_handler_shader_create(&state->handlers[KASSET_TYPE_SHADER], state->vfs);
    asset_handler_system_font_create(&state->handlers[KASSET_TYPE_SYSTEM_FONT], state->vfs);
    asset_handler_bitmap_font_create(&state->handlers[KASSET_TYPE_BITMAP_FONT], state->vfs);

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
                    asset_system_release_internal(state, lookup->asset.name, lookup->asset.package_name, true);
                }
            }
            kfree(state->lookups, sizeof(asset_lookup) * state->max_asset_count, MEMORY_TAG_ARRAY);
        }

        // Destroy the BST.
        u64_bst_cleanup(state->lookup_tree);

        kzero_memory(state, sizeof(asset_system_state));
    }
}

void asset_system_request(struct asset_system_state* state, asset_request_info info) {
    KASSERT(state);
    // Lookup the asset by fully-qualified name.
    u32 lookup_index = INVALID_ID;
    const bt_node* node = u64_bst_find(state->lookup_tree, info.asset_name);
    if (node) {
        lookup_index = node->value.u32;
    }
    if (lookup_index != INVALID_ID) {
        // Valid entry found, increment the reference count and immediately make the callback.
        asset_lookup* lookup = &state->lookups[lookup_index];
        lookup->reference_count++;
        if (info.callback) {
            info.callback(ASSET_REQUEST_RESULT_SUCCESS, &lookup->asset, info.listener_inst);
        }
    } else {
        // Before requesting the new asset, get it registered in the lookup in case anything
        // else requests it while it is still being loaded.
        // Search for an empty slot;
        for (u32 i = 0; i < state->max_asset_count; ++i) {
            asset_lookup* lookup = &state->lookups[i];
            if (lookup->asset.id.uniqueid == INVALID_ID_U64) {
                bt_node_value v;
                v.u32 = i;
                bt_node* new_node = u64_bst_insert(state->lookup_tree, info.asset_name, v);
                // Save as root if this is the first asset. Otherwise it'll be part of the tree automatically.
                if (!state->lookup_tree) {
                    state->lookup_tree = new_node;
                }

                // Found a free slot, setup the asset.
                lookup->asset.id = identifier_create();
                lookup->asset.type = info.type;
                lookup->asset.name = info.asset_name;
                lookup->asset.package_name = info.package_name;
                lookup->auto_release = info.auto_release;
                lookup->file_watch_id = INVALID_ID_U32;
                lookup->hot_reload_callback = info.hot_reload_callback;
                lookup->hot_reload_context = info.hot_reload_context;

                // Get the appropriate asset handler for the type and request the asset.
                asset_handler* handler = &state->handlers[lookup->asset.type];
                if (!handler->request_asset) {
                    // If no request_asset function pointer exists, use a "default" vfs request.
                    // Create and pass along a context.
                    // NOTE: The VFS takes a copy of this context, so the lifecycle doesn't matter.
                    asset_handler_request_context context = {0};
                    context.asset = &lookup->asset;
                    context.handler = handler;
                    context.listener_instance = info.listener_inst;
                    context.user_callback = info.callback;

                    vfs_request_info request_info = {0};
                    request_info.watch_for_hot_reload = lookup->hot_reload_callback ? true : false;
                    request_info.asset_name = lookup->asset.name;
                    request_info.package_name = lookup->asset.package_name;
                    request_info.import_params = info.import_params;
                    request_info.import_params_size = info.import_params_size;
                    request_info.is_binary = handler->is_binary;
                    request_info.context = &context;
                    request_info.context_size = sizeof(asset_handler_request_context);
                    request_info.get_source = false;
                    request_info.vfs_callback = asset_handler_base_on_asset_loaded;
                    if (info.synchronous) {
                        vfs_asset_data out_data = vfs_request_asset_sync(state->vfs, request_info);
                        asset_handler_base_on_asset_loaded(state->vfs, out_data);
                    } else {
                        vfs_request_asset(state->vfs, request_info);
                    }
                } else {
                    handler->request_asset(handler, &lookup->asset, info.listener_inst, info.callback);
                }
                return;
            }
        }
        // If this point is reached, it is not possible to register any more assets. Config should be adjusted
        // to handle more entries.
        KFATAL("The asset system has reached maximum capacity of allowed assets (%d). Please adjust configuration to allow for more if needed.", state->max_asset_count);
        info.callback(ASSET_REQUEST_RESULT_INTERNAL_FAILURE, 0, info.listener_inst);
    }
}

static void asset_system_release_internal(struct asset_system_state* state, kname asset_name, kname package_name, b8 force_release) {
    if (state) {
        // Lookup the asset by fully-qualified name.
        u32 lookup_index = INVALID_ID;
        const bt_node* node = u64_bst_find(state->lookup_tree, asset_name);
        if (node) {
            lookup_index = node->value.u32;
        }
        if (lookup_index != INVALID_ID) {
            // Valid entry found, decrement the reference count.
            asset_lookup* lookup = &state->lookups[lookup_index];
            lookup->reference_count--;
            if (force_release || (lookup->reference_count < 1 && lookup->auto_release)) {
                // Auto release set and criteria met, so call asset handler's 'unload' function.
                kasset* asset = &lookup->asset;
                kasset_type type = asset->type;
                asset_handler* handler = &state->handlers[type];
                if (!handler->release_asset) {
                    KWARN("No release setup on handler for asset type %d, asset_name='%s', package_name='%s'", type, kname_string_get(asset_name), kname_string_get(package_name));
                } else {
                    // Release the asset-specific data.
                    // TODO: Jobify this call.
                    handler->release_asset(handler, asset);
                }

                kzero_memory(asset, sizeof(kasset));

                // Ensure the lookup is invalidated.
                lookup->asset.id.uniqueid = INVALID_ID_U64;
                lookup->asset.generation = INVALID_ID;
                lookup->reference_count = 0;
                lookup->auto_release = false;
            }
        } else {
            // Entry not found, nothing to do.
            KWARN("asset_system_release: Attempted to release asset '%s' (package '%s'), which does not exist or is not already loaded. Nothing to do.", kname_string_get(asset_name), kname_string_get(package_name));
        }
    }
}

void asset_system_release(struct asset_system_state* state, kname asset_name, kname package_name) {
    asset_system_release_internal(state, asset_name, package_name, false);
}

void asset_system_on_handler_result(struct asset_system_state* state, asset_request_result result, kasset* asset, void* listener_instance, PFN_kasset_on_result callback) {
    if (state && asset) {
        switch (result) {
        case ASSET_REQUEST_RESULT_SUCCESS: {
            // See if the asset already exists first.
            u32 lookup_index = INVALID_ID;
            const bt_node* node = u64_bst_find(state->lookup_tree, asset->name);
            if (node) {
                lookup_index = node->value.u32;
            }
            if (lookup_index != INVALID_ID) {
                // Valid entry found, increment the reference count and immediately make the callback.
                asset_lookup* lookup = &state->lookups[lookup_index];
                lookup->reference_count++;
                lookup->asset.generation++;
                if (callback) {
                    callback(ASSET_REQUEST_RESULT_SUCCESS, &lookup->asset, listener_instance);
                }
            } else {
                // NOTE: The lookup should already exist at this point as defined above in asset_system_request.
                KERROR("Could not find valid lookup for asset '%s', package '%s'.", kname_string_get(asset->name), kname_string_get(asset->package_name));
                if (callback) {
                    callback(ASSET_REQUEST_RESULT_INTERNAL_FAILURE, 0, listener_instance);
                }
            }
        } break;
        case ASSET_REQUEST_RESULT_INVALID_PACKAGE:
            KERROR("Asset '%s' load failed: An invalid package was specified.", kname_string_get(asset->name));
            break;
        case ASSET_REQUEST_RESULT_INVALID_NAME:
            KERROR("Asset '%s' load failed: An invalid asset name was specified.", kname_string_get(asset->name));
            break;
        case ASSET_REQUEST_RESULT_INVALID_ASSET_TYPE:
            KERROR("Asset '%s' load failed: An invalid asset type was specified.", kname_string_get(asset->name));
            break;
        case ASSET_REQUEST_RESULT_PARSE_FAILED:
            KERROR("Asset '%s' load failed: The parsing stage of the asset load failed.", kname_string_get(asset->name));
            break;
        case ASSET_REQUEST_RESULT_GPU_UPLOAD_FAILED:
            KERROR("Asset '%s' load failed: The GPU-upload stage of the asset load failed.", kname_string_get(asset->name));
            break;
        default:
            KERROR("Asset '%s' load failed: An unspecified error has occurred.", kname_string_get(asset->name));
            break;
        }
    }
}

b8 asset_type_is_binary(kasset_type type) {
    switch (type) {
        // NOTE: Specify text-type assets here (i.e. assets that should be opened as text, not binary).
    case KASSET_TYPE_HEIGHTMAP_TERRAIN:
    case KASSET_TYPE_MATERIAL:
    case KASSET_TYPE_SCENE:
    case KASSET_TYPE_KSON:
    case KASSET_TYPE_TEXT:
    case KASSET_TYPE_BITMAP_FONT:
    case KASSET_TYPE_SYSTEM_FONT:
        return false;

    default:
        // NOTE: default for assets is binary.
        return true;
    }
}
