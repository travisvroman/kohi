#include "kasset_heightmap_terrain_serializer.h"

#include "assets/kasset_types.h"

#include "logger.h"
#include "math/kmath.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"
#include "strings/kstring.h"

const char* kasset_heightmap_terrain_serialize(const kasset_heightmap_terrain* asset) {
    if (!asset) {
        KERROR("kasset_heightmap_serialize requires an asset to serialize, ya dingus!");
        return 0;
    }

    kasset_heightmap_terrain* typed_asset = (kasset_heightmap_terrain*)asset;
    const char* out_str = 0;

    // Setup the KSON tree to serialize below.
    kson_tree tree = {0};
    tree.root = kson_object_create();

    // version
    if (!kson_object_value_add_int(&tree.root, "version", typed_asset->version)) {
        KERROR("Failed to add version, which is a required field.");
        goto cleanup_kson;
    }

    // heightmap_asset_name
    if (!kson_object_value_add_kname_as_string(&tree.root, "heightmap_asset_name", typed_asset->heightmap_asset_name)) {
        KERROR("Failed to add heightmap_asset_name, which is a required field.");
        goto cleanup_kson;
    }

    // heightmap_asset_package_name - optional
    kson_object_value_add_kname_as_string(&tree.root, "heightmap_asset_package_name", typed_asset->heightmap_asset_package_name);

    // chunk_size
    if (!kson_object_value_add_int(&tree.root, "chunk_size", typed_asset->chunk_size)) {
        KERROR("Failed to add chunk_size, which is a required field.");
        goto cleanup_kson;
    }

    // tile_scale
    if (!kson_object_value_add_vec3(&tree.root, "tile_scale", typed_asset->tile_scale)) {
        KERROR("Failed to add tile_scale, which is a required field.");
        goto cleanup_kson;
    }

    // Material names array.
    kson_array material_names_array = kson_array_create();
    for (u32 i = 0; i < typed_asset->material_count; ++i) {
        if (!kson_array_value_add_string(&material_names_array, kname_string_get(typed_asset->material_names[i]))) {
            KWARN("Unable to set material name at index %u, using default of '%s' instead.", "default_terrain");
        }
    }
    if (!kson_object_value_add_array(&tree.root, "material_names", material_names_array)) {
        KERROR("Failed to add material_names, which is a required field.");
        goto cleanup_kson;
    }

    out_str = kson_tree_to_string(&tree);
    if (!out_str) {
        KERROR("Failed to serialize heightmap terrain to string. See logs for details.");
    }

cleanup_kson:
    kson_tree_cleanup(&tree);

    return out_str;
}

b8 kasset_heightmap_terrain_deserialize(const char* file_text, kasset_heightmap_terrain* out_asset) {
    if (out_asset) {
        b8 success = false;
        kasset_heightmap_terrain* typed_asset = (kasset_heightmap_terrain*)out_asset;

        // Deserialize the loaded asset data
        kson_tree tree = {0};
        if (!kson_tree_from_string(file_text, &tree)) {
            KERROR("Failed to parse asset data for heightmap terrain. See logs for details.");
            goto cleanup_kson;
        }

        // version
        if (!kson_object_property_value_get_int(&tree.root, "version", (i64*)(&typed_asset->version))) {
            KERROR("Failed to parse version, which is a required field.");
            goto cleanup_kson;
        }

        // heightmap_asset_name
        if (!kson_object_property_value_get_string_as_kname(&tree.root, "heightmap_asset_name", &typed_asset->heightmap_asset_name)) {
            KERROR("Failed to parse heightmap_asset_name, which is a required field.");
            goto cleanup_kson;
        }

        // heightmap_asset_package_name - optional, can be found automatically.
        kson_object_property_value_get_string_as_kname(&tree.root, "heightmap_asset_package_name", &typed_asset->heightmap_asset_package_name);

        // chunk_size
        if (!kson_object_property_value_get_int(&tree.root, "chunk_size", (i64*)(&typed_asset->chunk_size))) {
            KERROR("Failed to parse chunk_size, which is a required field.");
            goto cleanup_kson;
        }

        // tile_scale - optional with default of 1.
        if (kson_object_property_value_get_vec3(&tree.root, "tile_scale", &typed_asset->tile_scale)) {
            typed_asset->tile_scale = vec3_one();
        }

        // Material names array.
        kson_array material_names_obj_array = {0};
        if (!kson_object_property_value_get_object(&tree.root, "material_names", &material_names_obj_array)) {
            KERROR("Failed to parse material_names, which is a required field.");
            goto cleanup_kson;
        }

        // Get the number of elements.
        if (!kson_array_element_count_get(&material_names_obj_array, (u32*)(&typed_asset->material_count))) {
            KERROR("Failed to parse material_names count. Invalid format?");
            goto cleanup_kson;
        }

        // Setup the new array.
        typed_asset->material_names = kallocate(sizeof(const char*) * typed_asset->material_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < typed_asset->material_count; ++i) {
            const char* mat_name = 0;
            if (!kson_array_element_value_get_string(&material_names_obj_array, i, &mat_name)) {
                KWARN("Unable to read material name at index %u, using default of '%s' instead.", "default_terrain");
                // Take a duplicate since the cleanup code won't know a constant is used here.
                typed_asset->material_names[i] = kname_create("default_terrain");
            }
            typed_asset->material_names[i] = kname_create(mat_name);
            string_free(mat_name);
        }

        success = true;
    cleanup_kson:
        kson_tree_cleanup(&tree);
        if (!success) {
            if (typed_asset->material_count && typed_asset->material_names) {
                kfree(typed_asset->material_names, sizeof(kname) * typed_asset->material_count, MEMORY_TAG_ARRAY);
                typed_asset->material_names = 0;
                typed_asset->material_count = 0;
            }
        }
        return success;
    }

    KERROR("kasset_heightmap_terrain_deserialize serializer requires an asset to deserialize to, ya dingus!");
    return false;
}
