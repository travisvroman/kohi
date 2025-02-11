#include "kasset_scene_serializer.h"

#include "assets/kasset_types.h"
#include "core_resource_types.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"

// The current scene version.
#define SCENE_ASSET_CURRENT_VERSION 3
// The minimum supported version.
#define SCENE_ASSET_MIN_VERSION 3

static b8 serialize_node(scene_node_config* node, kson_object* node_obj);

static b8 deserialize_node(kasset* asset, scene_node_config* node, kson_object* node_obj);

const char* kasset_scene_serialize(const kasset* asset) {
    if (!asset) {
        KERROR("scene_serialize requires an asset to serialize, ya dingus!");
        KERROR("Scene serialization failed. See logs for details.");
        return 0;
    }

    kasset_scene* typed_asset = (kasset_scene*)asset;
    b8 success = false;
    const char* out_str = 0;

    // Setup the KSON tree to serialize below.
    kson_tree tree = {0};
    tree.root = kson_object_create();

    // version - always write the current version.
    if (!kson_object_value_add_int(&tree.root, "version", SCENE_ASSET_CURRENT_VERSION)) {
        KERROR("Failed to add version, which is a required field.");
        goto cleanup_kson;
    }

    // Description - optional.
    if (typed_asset->description) {
        kson_object_value_add_string(&tree.root, "description", typed_asset->description);
    }

    // Physics settings
    kson_object physics_obj = kson_object_create();
    if (!kson_object_value_add_vec3(&physics_obj, "gravity", typed_asset->physics_gravity)) {
        KERROR("Failed to serialize physics 'gravity' setting.");
        goto cleanup_kson;
    }
    if (!kson_object_value_add_boolean(&physics_obj, "enabled", typed_asset->physics_enabled)) {
        KERROR("Failed to serialize physic 'enabled' setting.");
        goto cleanup_kson;
    }
    if (!kson_object_value_add_object(&physics_obj, "physics", physics_obj)) {
        KERROR("Failed to serialize physics settings.");
        goto cleanup_kson;
    }

    // Nodes array.
    kson_array nodes_array = kson_array_create();
    for (u32 i = 0; i < typed_asset->node_count; ++i) {
        scene_node_config* node = &typed_asset->nodes[i];
        kson_object node_obj = kson_object_create();

        // Serialize the node. This is recursive, and also handles attachments.
        if (!serialize_node(node, &node_obj)) {
            KERROR("Failed to serialize root node '%s'.", node->name);
            kson_object_cleanup(&nodes_array);
            goto cleanup_kson;
        }

        // Add the object to the nodes array.
        if (!kson_array_value_add_object(&nodes_array, node_obj)) {
            KERROR("Failed to add child to children array to node '%s'.", node->name);
            kson_object_cleanup(&nodes_array);
            goto cleanup_kson;
        }
    }

    // Add the nodes array to the root object.
    if (!kson_object_value_add_array(&tree.root, "nodes", nodes_array)) {
        KERROR("Failed to add nodes, which is a required field.");
        kson_object_cleanup(&nodes_array);
        goto cleanup_kson;
    }

    // Serialize the entire thing to string now.
    out_str = kson_tree_to_string(&tree);
    if (!out_str) {
        KERROR("Failed to serialize scene to string. See logs for details.");
    }

    success = true;
cleanup_kson:
    if (!success) {
        KERROR("Scene serialization failed. See logs for details.");
    }
    kson_tree_cleanup(&tree);

    return out_str;
}

b8 kasset_scene_deserialize(const char* file_text, kasset* out_asset) {
    if (out_asset) {
        b8 success = false;
        kasset_scene* typed_asset = (kasset_scene*)out_asset;

        // Deserialize the loaded asset data
        kson_tree tree = {0};
        if (!kson_tree_from_string(file_text, &tree)) {
            KERROR("Failed to parse asset data for scene. See logs for details.");
            goto cleanup_kson;
        }

        // version is required in this case.
        if (!kson_object_property_value_get_int(&tree.root, "version", (i64*)(&typed_asset->base.meta.version))) {
            KERROR("Failed to parse version, which is a required field.");
            goto cleanup_kson;
        }

        // File versions < 3 are no longer supported
        if (typed_asset->base.meta.version < SCENE_ASSET_MIN_VERSION) {
            KERROR("Parsed scene version '%u' is below what the minimum supported version '%u' is. Check file format. Deserialization failed.", typed_asset->base.meta.version, SCENE_ASSET_MIN_VERSION);
            return false;
        } else if (typed_asset->base.meta.version > SCENE_ASSET_CURRENT_VERSION) {
            KERROR("Parsed scene version '%u' is beyond what the current version '%u' is. Check file format. Deserialization failed.", typed_asset->base.meta.version, SCENE_ASSET_CURRENT_VERSION);
            return false;
        }

        // Description comes from here, but is still optional.
        kson_object_property_value_get_string(&tree.root, "description", &typed_asset->description);

        // Physics settings, if they exist.
        kson_object physics_obj = {0};
        if (kson_object_property_value_get_object(&tree.root, "physics", &physics_obj)) {
            // Enabled - optional, default = false
            if (!kson_object_property_value_get_bool(&physics_obj, "enabled", &typed_asset->physics_enabled)) {
                KWARN("Scene parsing found a 'physics' block, but no 'enabled' was defined. Physics will be disabled for this scene.");
                typed_asset->physics_enabled = false;
            }

            // Gravity
            if (!kson_object_property_value_get_vec3(&physics_obj, "gravity", &typed_asset->physics_gravity)) {
                KWARN("Scene parsing found a 'physics' block, but no 'gravity' was defined. Using a reasonable default value.");
                typed_asset->physics_gravity = (vec3){0, -9.81f, 0};
            }
        } else {
            // Physics not defined, set zero gravity.
            typed_asset->physics_gravity = vec3_zero();
            typed_asset->physics_enabled = false;
        }

        // Nodes array.
        kson_array nodes_obj_array = {0};
        if (!kson_object_property_value_get_array(&tree.root, "nodes", &nodes_obj_array)) {
            KERROR("Failed to parse nodes, which is a required field.");
            goto cleanup_kson;
        }

        // Get the number of nodes.
        if (!kson_array_element_count_get(&nodes_obj_array, (u32*)(&typed_asset->node_count))) {
            KERROR("Failed to parse node count. Invalid format?");
            goto cleanup_kson;
        }

        // Process nodes.
        typed_asset->nodes = KALLOC_TYPE_CARRAY(scene_node_config, typed_asset->node_count);
        for (u32 i = 0; i < typed_asset->node_count; ++i) {
            scene_node_config* node = &typed_asset->nodes[i];
            kson_object node_obj;
            if (!kson_array_element_value_get_object(&nodes_obj_array, i, &node_obj)) {
                KWARN("Unable to read root node at index %u. Skipping.", i);
                continue;
            }

            // Deserialize recursively
            if (!deserialize_node(out_asset, node, &node_obj)) {
                KERROR("Unable to deserialize root node at index %u. Skipping.", i);
                continue;
            }
        }

        success = true;
    cleanup_kson:
        kson_tree_cleanup(&tree);
        return success;
    }

    KERROR("scene_deserialize serializer requires an asset to deserialize to, ya dingus!");
    return false;
}

static b8 serialize_node(scene_node_config* node, kson_object* node_obj) {
    kname node_name = node->name ? node->name : kname_create("unnamed-node");
    // Properties

    // Name, if it exists.
    if (node->name) {
        if (!kson_object_value_add_kname_as_string(node_obj, "name", node->name)) {
            KERROR("Failed to add 'name' property for node '%s'.", node_name);
            return false;
        }
    }

    // Xform as a string, if it exists.
    if (node->xform_source) {
        if (!kson_object_value_add_string(node_obj, "xform", node->xform_source)) {
            KERROR("Failed to add 'xform' property for node '%s'.", node_name);
            return false;
        }
    }

    // Process attachments by type, but place them all into the same array in the output file.
    kson_array attachment_obj_array = kson_array_create();

    if (node->attachments && node->attachment_count) {
        for (u32 i = 0; i < node->attachment_count; ++i) {
            kscene_attachment_config* attachment = &node->attachments[i];
            // Typename must exist
            if (attachment->type_name == INVALID_KNAME) {
                KERROR("Attachment named '%s' is missing a type_name, which is required for serialization. Failed to serialize.", kname_string_get(attachment->name));
                return false;
            }

            kson_object attachment_obj = kson_object_create();

            // Add name if it exists.
            if (attachment->name != INVALID_KNAME) {
                kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name);
            }

            // Type name
            kson_object_value_add_kname_as_string(&attachment_obj, "type_name", attachment->type_name);

            if (attachment->config) {
                kson_tree temp = {0};
                if (kson_tree_from_string(attachment->config, &temp)) {
                    kson_object_value_add_object(&attachment_obj, "config", temp.root);
                } else {
                    KWARN("Failed to add attachment object config for attachment '%s'. See logs for details.", kname_string_get(attachment->name));
                }
            }

            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    // Only write out the attachments array object if it contains something.
    u32 total_attachment_count = 0;
    kson_array_element_count_get(&attachment_obj_array, &total_attachment_count);
    if (total_attachment_count > 0) {
        // Add the attachments array to the parent node object.
        if (!kson_object_value_add_array(node_obj, "attachments", attachment_obj_array)) {
            KERROR("Failed to add attachments array to node '%s'.", node_name);
            kson_object_cleanup(&attachment_obj_array);
            return false;
        }
    } else {
        kson_object_cleanup(&attachment_obj_array);
    }

    // Process children if there are any.
    if (node->child_count && node->children) {
        kson_array children_array = kson_array_create();
        for (u32 i = 0; i < node->child_count; ++i) {
            scene_node_config* child = &node->children[i];
            kson_object child_obj = kson_object_create();

            // Recurse
            if (!serialize_node(child, &child_obj)) {
                KERROR("Failed to serialize child node of node '%s'.", node_name);
                kson_object_cleanup(&children_array);
                return false;
            }

            // Add it to the array.
            if (!kson_array_value_add_object(&children_array, child_obj)) {
                KERROR("Failed to add child to children array to node '%s'.", node_name);
                kson_object_cleanup(&children_array);
                return false;
            }
        }

        // Add the children array to the parent node object.
        if (!kson_object_value_add_array(node_obj, "children", children_array)) {
            KERROR("Failed to add children array to node '%s'.", node_name);
            kson_object_cleanup(&children_array);
            return false;
        }
    }

    return true;
}

static b8 deserialize_node(kasset* asset, scene_node_config* node, kson_object* node_obj) {

    // Get name, if defined. Not required.
    kson_object_property_value_get_string_as_kname(node_obj, "name", &node->name);

    // Get Xform as a string, if it exists. Optional.
    kson_object_property_value_get_string(node_obj, "xform", &node->xform_source);

    // Process attachments if there are any. These are optional.
    kson_array attachment_obj_array = {0};
    if (kson_object_property_value_get_array(node_obj, "attachments", &attachment_obj_array)) {

        // Get the number of attachments.
        if (!kson_array_element_count_get(&attachment_obj_array, (u32*)(&node->attachment_count))) {
            KERROR("Failed to parse attachment count. Invalid format?");
            return false;
        }

        if (node->attachment_count) {
            node->attachments = KALLOC_TYPE_CARRAY(kscene_attachment_config, node->attachment_count);

            // Setup the attachment array and deserialize.
            for (u32 i = 0; i < node->attachment_count; ++i) {
                kson_object attachment_obj;
                kscene_attachment_config* attachment = &node->attachments[i];
                if (!kson_array_element_value_get_object(&attachment_obj_array, i, &attachment_obj)) {
                    KWARN("Unable to read attachment at index %u. Skipping.", i);
                    continue;
                }

                // Name is optional.
                kson_object_property_value_get_string_as_kname(&attachment_obj, "name", &attachment->name);

                // Type name is required.
                if (!kson_object_property_value_get_string_as_kname(&attachment_obj, "type_name", &attachment->type_name)) {
                    KERROR("Property 'type_name' is required but not present for attachment '%s'. Skipping.", kname_string_get(attachment->name));
                    continue;
                }

                // Config is optional at this level, but may be required for certain component types.
                // Get entire "config" as a serialized string, and deserialize the config later when
                // initializing the scene.
                attachment->config = kson_object_property_value_get_object_as_source_string(&attachment_obj, "config");
            }
        }
    }

    // Process children if there are any. These are optional.
    kson_array children_obj_array = {0};
    if (kson_object_property_value_get_array(node_obj, "children", &children_obj_array)) {

        // Get the number of nodes.
        if (!kson_array_element_count_get(&children_obj_array, (u32*)(&node->child_count))) {
            KERROR("Failed to parse children count. Invalid format?");
            return false;
        }

        // Setup the child array and deserialize.
        node->children = KALLOC_TYPE_CARRAY(scene_node_config, node->child_count);
        for (u32 i = 0; i < node->child_count; ++i) {
            scene_node_config* child = &node->children[i];
            kson_object node_obj;
            if (!kson_array_element_value_get_object(&children_obj_array, i, &node_obj)) {
                KWARN("Unable to read child node at index %u. Skipping.", i);
                continue;
            }

            // Deserialize recursively
            if (!deserialize_node(asset, child, &node_obj)) {
                KERROR("Unable to deserialize root node at index %u. Skipping.", i);
                continue;
            }
        }
    }

    // Done!
    return true;
}
