#include "asset_handler_heightmap_terrain.h"

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

static void asset_handler_heightmap_terrain_on_asset_loaded_callback(const char* name, vfs_asset_data asset_data);

void asset_handler_heightmap_terrain_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->request_asset = asset_handler_heightmap_terrain_request_asset;
    self->release_asset = asset_handler_heightmap_terrain_release_asset;
    self->type = KASSET_TYPE_HEIGHTMAP_TERRAIN;
    self->type_name = KASSET_TYPE_NAME_HEIGHTMAP_TERRAIN;
}

void asset_handler_heightmap_terrain_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback) {
    // struct asset_system_state* asset_state = engine_systems_get()->asset_state;
    struct vfs_state* vfs_state = engine_systems_get()->vfs_system_state;
    // Create and pass along a context.
    // NOTE: The VFS takes a copy of this context, so the lifecycle doesn't matter.
    asset_handler_request_context context = {0};
    context.asset = asset;
    context.handler = self;
    context.listener_instance = listener_instance;
    context.user_callback = user_callback;
    vfs_request_asset(vfs_state, &asset->meta.name, false, sizeof(asset_handler_request_context), &context, asset_handler_heightmap_terrain_on_asset_loaded_callback);
}

void asset_handler_heightmap_terrain_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_heightmap_terrain* typed_asset = (kasset_heightmap_terrain*)asset;
    if (typed_asset->heightmap_filename) {
        string_free(typed_asset->heightmap_filename);
        typed_asset->heightmap_filename = 0;
    }
    if (typed_asset->material_count && typed_asset->material_names) {
        for (u32 i = 0; i < typed_asset->material_count; ++i) {
            const char* material_name = typed_asset->material_names[i];
            if (material_name) {
                string_free(material_name);
                material_name = 0;
            }
        }
        kfree(typed_asset->material_names, sizeof(const char*) * typed_asset->material_count, MEMORY_TAG_ARRAY);
        typed_asset->material_names = 0;
        typed_asset->material_count = 0;
    }
}

static void asset_handler_heightmap_terrain_on_asset_loaded_callback(const char* name, vfs_asset_data asset_data) {
    b8 success = false;

    // This handler requires context.
    KASSERT_MSG(asset_data.context_size && asset_data.context, "asset_handler_heightmap_terrain_on_asset_loaded_callback requires valid context.");

    // Take a copy of the context first as it gets freed immediately upon return of this function.
    asset_handler_request_context context = *((asset_handler_request_context*)asset_data.context);
    if (context.asset) {
        kasset_heightmap_terrain* typed_asset = (kasset_heightmap_terrain*)context.asset;

        // Deserialize the loaded asset data
        const char* text = asset_data.text;
        kson_tree tree = {0};
        if (!kson_tree_from_string(text, &tree)) {
            KERROR("Failed to parse asset data for heightmap terrain. See logs for details.");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }

        // version
        if (!kson_object_property_value_get_int(&tree.root, "version", (i64*)(&typed_asset->base.meta.version))) {
            KERROR("Failed to parse version, which is a required field.");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }

        // heightmap_filename
        if (!kson_object_property_value_get_string(&tree.root, "heightmap_filename", &typed_asset->heightmap_filename)) {
            KERROR("Failed to parse heightmap_filename, which is a required field.");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }

        // chunk_size
        if (!kson_object_property_value_get_int(&tree.root, "chunk_size", (i64*)(&typed_asset->chunk_size))) {
            KERROR("Failed to parse chunk_size, which is a required field.");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }

        // tile_scale - vectors are represented as strings, so get that then parse it.
        const char* temp_tile_scale_str = 0;
        if (!kson_object_property_value_get_string(&tree.root, "tile_scale", &temp_tile_scale_str)) {
            KERROR("Failed to parse tile_scale, which is a required field.");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }
        if (!string_to_vec3(temp_tile_scale_str, &typed_asset->tile_scale)) {
            KWARN("Failed to parse tile_scale from string, defaulting to scale of 1. Check file format.");
            typed_asset->tile_scale = vec3_one();
        }
        string_free(temp_tile_scale_str);
        temp_tile_scale_str = 0;

        // Material names array.
        kson_array material_names_obj_array = {0};
        if (!kson_object_property_value_get_object(&tree.root, "material_names", &material_names_obj_array)) {
            KERROR("Failed to parse material_names, which is a required field.");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }

        // Get the number of elements.
        if (!kson_array_element_count_get(&material_names_obj_array, (u32*)(&typed_asset->material_count))) {
            KERROR("Failed to parse material_names count. Invalid format?");
            context.user_callback(ASSET_REQUEST_RESULT_PARSE_FAILED, context.asset, context.listener_instance);
            goto cleanup_kson;
        }

        // Setup the new array.
        typed_asset->material_names = kallocate(sizeof(const char*) * typed_asset->material_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < typed_asset->material_count; ++i) {
            if (!kson_array_element_value_get_string(&material_names_obj_array, i, &typed_asset->material_names[i])) {
                KWARN("Unable to read material name at index %u, using default of '%s' instead.", DEFAULT_TERRAIN_MATERIAL_NAME);
                // Take a duplicate since the cleanup code won't know a constant is used here.
                typed_asset->material_names[i] = string_duplicate(DEFAULT_TERRAIN_MATERIAL_NAME);
            }
        }

        success = true;
    cleanup_kson:
        kson_tree_cleanup(&tree);
        if (!success) {
            if (typed_asset->heightmap_filename) {
                string_free(typed_asset->heightmap_filename);
                typed_asset->heightmap_filename = 0;
            }
            if (typed_asset->material_count && typed_asset->material_names) {
                for (u32 i = 0; i < typed_asset->material_count; ++i) {
                    const char* material_name = typed_asset->material_names[i];
                    if (material_name) {
                        string_free(material_name);
                        material_name = 0;
                    }
                }
                kfree(typed_asset->material_names, sizeof(const char*) * typed_asset->material_count, MEMORY_TAG_ARRAY);
                typed_asset->material_names = 0;
                typed_asset->material_count = 0;
            }
        } else {
            // The operation was a success. Tell the listener about it.
            context.user_callback(ASSET_REQUEST_RESULT_SUCCESS, context.asset, context.listener_instance);
        }
    }
}
