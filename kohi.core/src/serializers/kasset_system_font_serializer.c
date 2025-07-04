#include "kasset_system_font_serializer.h"

#include "assets/kasset_types.h"

#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"

#define SYSTEM_FONT_FORMAT_VERSION 1

const char* kasset_system_font_serialize(const kasset* asset) {
    if (!asset) {
        KERROR("kasset_system_font_serialize requires an asset to serialize, ya dingus!");
        return 0;
    }

    kasset_system_font* typed_asset = (kasset_system_font*)asset;
    const char* out_str = 0;

    // Setup the KSON tree to serialize below.
    kson_tree tree = {0};
    tree.root = kson_object_create();

    // version
    if (!kson_object_value_add_int(&tree.root, "version", SYSTEM_FONT_FORMAT_VERSION)) {
        KERROR("Failed to add version, which is a required field.");
        goto cleanup_kson;
    }

    // ttf_asset_name
    if (!kson_object_value_add_string(&tree.root, "ttf_asset_name", kname_string_get(typed_asset->ttf_asset_name))) {
        KERROR("Failed to add ttf_asset_name, which is a required field.");
        goto cleanup_kson;
    }

    // ttf_asset_package_name
    if (!kson_object_value_add_kname_as_string(&tree.root, "ttf_asset_package_name", typed_asset->ttf_asset_package_name)) {
        KERROR("Failed to add ttf_asset_package_name, which is a required field.");
        goto cleanup_kson;
    }

    // faces
    kson_array faces_array = kson_array_create();
    for (u32 i = 0; i < typed_asset->face_count; ++i) {
        if (!kson_array_value_add_kname_as_string(&faces_array, typed_asset->faces[i].name)) {
            KWARN("Unable to set face name at index %u. Skipping.", i);
            continue;
        }
    }
    if (!kson_object_value_add_array(&tree.root, "faces", faces_array)) {
        KERROR("Failed to add faces, which is a required field.");
        goto cleanup_kson;
    }

    out_str = kson_tree_to_string(&tree);
    if (!out_str) {
        KERROR("Failed to serialize system_font to string. See logs for details.");
    }

cleanup_kson:
    kson_tree_cleanup(&tree);

    return out_str;
}

b8 kasset_system_font_deserialize(const char* file_text, kasset_system_font* out_asset) {
    if (out_asset) {
        b8 success = false;

        // Deserialize the loaded asset data
        kson_tree tree = {0};
        if (!kson_tree_from_string(file_text, &tree)) {
            KERROR("Failed to parse asset data for system_font. See logs for details.");
            goto cleanup_kson;
        }

        // version
        i64 version = 0;
        if (!kson_object_property_value_get_int(&tree.root, "version", &version)) {
            KERROR("Failed to parse version, which is a required field.");
            goto cleanup_kson;
        }

        // ttf_asset_name
        if (!kson_object_property_value_get_string_as_kname(&tree.root, "ttf_asset_name", &out_asset->ttf_asset_name)) {
            KERROR("Failed to parse ttf_asset_name, which is a required field.");
            goto cleanup_kson;
        }

        // ttf_asset_package_name
        if (!kson_object_property_value_get_string_as_kname(&tree.root, "ttf_asset_package_name", &out_asset->ttf_asset_package_name)) {
            KERROR("Failed to get ttf_asset_package_name, which is a required field.");
            goto cleanup_kson;
        }

        // Faces array
        kson_array face_array = {0};
        if (!kson_object_property_value_get_array(&tree.root, "faces", &face_array)) {
            KERROR("Failed to parse faces, which is a required field.");
            goto cleanup_kson;
        }

        // Get the number of elements.
        if (!kson_array_element_count_get(&face_array, (u32*)(&out_asset->face_count))) {
            KERROR("Failed to parse face count. Invalid format?");
            goto cleanup_kson;
        }

        // Setup the new array.
        out_asset->faces = kallocate(sizeof(kasset_system_font_face) * out_asset->face_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < out_asset->face_count; ++i) {
            if (!kson_array_element_value_get_string_as_kname(&face_array, i, &out_asset->faces[i].name)) {
                KWARN("Unable to read face name at index %u. Skipping", i);
                continue;
            }
        }

        success = true;
    cleanup_kson:
        kson_tree_cleanup(&tree);
        if (!success) {
            if (out_asset->face_count && out_asset->faces) {
                kfree(out_asset->faces, sizeof(kasset_system_font_face) * out_asset->face_count, MEMORY_TAG_ARRAY);
                out_asset->faces = 0;
                out_asset->face_count = 0;
            }
        }
        return success;
    }

    KERROR("kasset_system_font_deserialize serializer requires an asset to deserialize to, ya dingus!");
    return false;
}
