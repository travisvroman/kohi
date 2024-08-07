#include "kasset_scene_serializer.h"

#include "assets/kasset_types.h"

#include "logger.h"
#include "math/kmath.h"
#include "parsers/kson_parser.h"
#include "strings/kstring.h"

// The current scene version.
#define KASSET_SCENE_VERSION 2

static b8 serialize_node(kasset_scene_node* node, kson_object* node_obj);
static b8 serialize_attachment(kasset_scene_node_attachment* attachment, kson_object* attachment_obj);

static b8 deserialize_node(kasset* asset, kasset_scene_node* node, kson_object* node_obj);
static b8 deserialize_attachment(kasset* asset, kasset_scene_node_attachment* attachment, kson_object* attachment_obj);

const char* kasset_scene_serialize(const kasset* asset) {
    if (!asset) {
        KERROR("kasset_scene_serialize requires an asset to serialize, ya dingus!");
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
    if (!kson_object_value_add_int(&tree.root, "version", KASSET_SCENE_VERSION)) {
        KERROR("Failed to add version, which is a required field.");
        goto cleanup_kson;
    }

    // Description - optional.
    if (typed_asset->description) {
        kson_object_value_add_string(&tree.root, "description", typed_asset->description);
    }

    // Nodes array.
    kson_array nodes_array = kson_array_create();
    for (u32 i = 0; i < typed_asset->node_count; ++i) {
        kasset_scene_node* node = &typed_asset->nodes[i];
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

        // Determine the asset version first. Version 1 has a top-level "properties" object
        // that was removed in v2+. v1 Also does not list a version number, whereas v2+ does.
        kson_object properties_obj = {0};
        if (kson_object_property_value_get_object(&tree.root, "properties", &properties_obj)) {
            // This is a version 1 file.
            out_asset->meta.version = 1;

            // Description is also extracted from here for v1. This is optional, however, so don't
            // bother checking the result.
            kson_object_property_value_get_string(&properties_obj, "description", &typed_asset->description);

            // NOTE: v1 files also had a "name", but this will be ignored in favour of the asset name itself.
        } else {
            // File is v2+, extract the version and description from the root node.

            // version is required in this case.
            if (!kson_object_property_value_get_int(&tree.root, "version", (i64*)(&typed_asset->base.meta.version))) {
                KERROR("Failed to parse version, which is a required field.");
                goto cleanup_kson;
            }

            // Description comes from here, but is still optional.
            kson_object_property_value_get_string(&tree.root, "description", &typed_asset->description);
        }

        // Nodes array.
        kson_array nodes_obj_array = {0};
        if (!kson_object_property_value_get_object(&tree.root, "nodes", &nodes_obj_array)) {
            KERROR("Failed to parse nodes, which is a required field.");
            goto cleanup_kson;
        }

        // Get the number of nodes.
        if (!kson_array_element_count_get(&nodes_obj_array, (u32*)(&typed_asset->node_count))) {
            KERROR("Failed to parse node count. Invalid format?");
            goto cleanup_kson;
        }

        // Process nodes.
        typed_asset->nodes = kallocate(sizeof(kasset_scene_node) * typed_asset->node_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < typed_asset->node_count; ++i) {
            kasset_scene_node* node = &typed_asset->nodes[i];
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

    KERROR("kasset_scene_deserialize serializer requires an asset to deserialize to, ya dingus!");
    return false;
}

static b8 serialize_attachment(kasset_scene_node_attachment* attachment, kson_object* attachment_obj) {
    const char* attachment_name = attachment->name ? attachment->name : "unnamed-attachment";

    // Name, if it exists.
    if (attachment->name) {
        if (!kson_object_value_add_string(attachment_obj, "name", attachment->name)) {
            KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
            return false;
        }
    }

    // Add the type.
    const char* type_str = kasset_scene_node_attachment_type_strings[attachment->type];
    if (!kson_object_value_add_string(attachment_obj, "type", type_str)) {
        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
        return false;
    }

    // Process based on attachment type.
    switch (attachment->type) {

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_SKYBOX: {
        kasset_scene_node_attachment_skybox* typed_attachment = (kasset_scene_node_attachment_skybox*)attachment;

        // Cubemap name
        const char* cubemap_name = typed_attachment->cubemap_image_asset_name ? typed_attachment->cubemap_image_asset_name : "default_skybox";
        if (!kson_object_value_add_string(attachment_obj, "cubemap_image_asset_name", cubemap_name)) {
            KERROR("Failed to add 'cubemap_image_asset_name' property for attachment '%s'.", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT: {
        kasset_scene_node_attachment_directional_light* typed_attachment = (kasset_scene_node_attachment_directional_light*)attachment;

        // Colour
        if (!kson_object_value_add_vec4(attachment_obj, "colour", typed_attachment->colour)) {
            KERROR("Failed to add 'colour' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Direction
        if (!kson_object_value_add_vec4(attachment_obj, "direction", typed_attachment->direction)) {
            KERROR("Failed to add 'direction' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_distance
        if (!kson_object_value_add_float(attachment_obj, "shadow_distance", typed_attachment->shadow_distance)) {
            KERROR("Failed to add 'shadow_distance' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_fade_distance
        if (!kson_object_value_add_float(attachment_obj, "shadow_fade_distance", typed_attachment->shadow_fade_distance)) {
            KERROR("Failed to add 'shadow_fade_distance' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_split_mult
        if (!kson_object_value_add_float(attachment_obj, "shadow_split_mult", typed_attachment->shadow_split_mult)) {
            KERROR("Failed to add 'shadow_split_mult' property for attachment '%s'.", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT: {
        kasset_scene_node_attachment_point_light* typed_attachment = (kasset_scene_node_attachment_point_light*)attachment;

        // Colour
        if (!kson_object_value_add_vec4(attachment_obj, "colour", typed_attachment->colour)) {
            KERROR("Failed to add 'colour' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Position
        if (!kson_object_value_add_vec4(attachment_obj, "position", typed_attachment->position)) {
            KERROR("Failed to add 'position' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Constant
        if (!kson_object_value_add_float(attachment_obj, "constant_f", typed_attachment->constant_f)) {
            KERROR("Failed to add 'constant_f' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Linear
        if (!kson_object_value_add_float(attachment_obj, "linear", typed_attachment->linear)) {
            KERROR("Failed to add 'linear' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Quadratic
        if (!kson_object_value_add_float(attachment_obj, "quadratic", typed_attachment->quadratic)) {
            KERROR("Failed to add 'quadratic' property for attachment '%s'.", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH: {
        kasset_scene_node_attachment_static_mesh* typed_attachment = (kasset_scene_node_attachment_static_mesh*)attachment;

        // Asset name
        const char* asset_name = typed_attachment->asset_name;
        if (!asset_name) {
            KWARN("Attempted to load static mesh (name: '%s') without an asset name. A default mesh name will be used.", attachment_name);
            asset_name = "default_static_mesh";
        }
        if (!kson_object_value_add_string(attachment_obj, "asset_name", asset_name)) {
            KERROR("Failed to add 'cubemap_image_asset_name' property for attachment '%s'.", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN: {
        kasset_scene_node_attachment_heightmap_terrain* typed_attachment = (kasset_scene_node_attachment_heightmap_terrain*)attachment;

        // Asset name
        if (typed_attachment->asset_name) {
            if (!kson_object_value_add_string(attachment_obj, "asset_name", typed_attachment->asset_name)) {
                KERROR("Failed to add 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        } else {
            KERROR("Cannot add heightmap terrain (name: '%s') without an 'asset_name'!", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE: {
        // NOTE: Intentionally blank until additional config is added to water planes.
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT:
        KERROR("Stop trying to serialize the count member of the enum, ya dingus!");
        return false;
    }

    return true;
}

static b8 serialize_node(kasset_scene_node* node, kson_object* node_obj) {
    const char* node_name = node->name ? node->name : "unnamed-node";
    // Properties

    // Name, if it exists.
    if (node->name) {
        if (!kson_object_value_add_string(node_obj, "name", node->name)) {
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

    // Process attachments.
    if (node->attachment_count && node->attachments) {
        kson_array attachment_array = kson_array_create();
        for (u32 i = 0; i < node->attachment_count; ++i) {
            kasset_scene_node_attachment* attachment = &node->attachments[i];
            kson_object attachment_obj = kson_object_create();

            // Process attachment.
            if (!serialize_attachment(attachment, &attachment_obj)) {
                KERROR("Failed to serialize attachment of node '%s'.", node_name);
                kson_object_cleanup(&attachment_array);
                return false;
            }

            // Add it to the array.
            if (!kson_array_value_add_object(&attachment_array, attachment_obj)) {
                KERROR("Failed to add attachment to node '%s'.", node_name);
                kson_object_cleanup(&attachment_array);
                return false;
            }
        }

        // Add the attachments array to the parent node object.
        if (!kson_object_value_add_array(node_obj, "attachments", attachment_array)) {
            KERROR("Failed to add attachments array to node '%s'.", node_name);
            kson_object_cleanup(&attachment_array);
            return false;
        }
    }

    // Process children if there are any.
    if (node->child_count && node->children) {
        kson_array children_array = kson_array_create();
        for (u32 i = 0; i < node->child_count; ++i) {
            kasset_scene_node* child = &node->children[i];
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

static b8 deserialize_node(kasset* asset, kasset_scene_node* node, kson_object* node_obj) {

    // Get name, if defined. Not required.
    kson_object_property_value_get_string(node_obj, "name", &node->name);

    // Get Xform as a string, if it exists. Optional.
    kson_object_property_value_get_string(node_obj, "xform", &node->xform_source);

    // Process attachments if there are any. These are optional.
    kson_array attachment_obj_array = {0};
    if (kson_object_property_value_get_object(node_obj, "attachments", &attachment_obj_array)) {

        // Get the number of attachments.
        if (!kson_array_element_count_get(&attachment_obj_array, (u32*)(&node->attachment_count))) {
            KERROR("Failed to parse attachment count. Invalid format?");
            return false;
        }

        // Setup the attachment array and deserialize.
        node->attachments = kallocate(sizeof(kasset_scene_node_attachment) * node->attachment_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < node->attachment_count; ++i) {
            kasset_scene_node_attachment* attachment = &node->attachments[i];
            kson_object attachment_obj;
            if (!kson_array_element_value_get_object(&attachment_obj_array, i, &attachment_obj)) {
                KWARN("Unable to read attachment at index %u. Skipping.", i);
                continue;
            }

            // Deserialize attachment.
            if (!deserialize_attachment(asset, attachment, &attachment_obj)) {
                KERROR("Failed to deserialize attachment at index %u. Skipping.", i);
                continue;
            }
        }
    }

    // Process children if there are any. These are optional.
    kson_array children_obj_array = {0};
    if (kson_object_property_value_get_object(node_obj, "children", &children_obj_array)) {

        // Get the number of nodes.
        if (!kson_array_element_count_get(&children_obj_array, (u32*)(&node->child_count))) {
            KERROR("Failed to parse children count. Invalid format?");
            return false;
        }

        // Setup the child array and deserialize.
        node->children = kallocate(sizeof(kasset_scene_node) * node->child_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < node->child_count; ++i) {
            kasset_scene_node* child = &node->children[i];
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
static b8 deserialize_attachment(kasset* asset, kasset_scene_node_attachment* attachment, kson_object* attachment_obj) {

    // Name, if it exists. Optional.
    kson_object_property_value_get_string(attachment_obj, "name", &attachment->name);

    const char* attachment_name = attachment->name ? attachment->name : "unnamed-attachment";

    // Parse the type.
    const char* type_str = 0; // kasset_scene_node_attachment_type_strings[attachment->type];
    if (!kson_object_property_value_get_string(attachment_obj, "type", &type_str)) {
        KERROR("Failed to parse required 'type' property for attachment '%s'.", attachment_name);
        return false;
    }

    // Find the attachment type.
    b8 type_found = false;
    for (u32 i = 0; i < KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT; ++i) {
        if (strings_equali(kasset_scene_node_attachment_type_strings[i], type_str)) {
            attachment->type = i;
            type_found = true;
            break;
        }

        // Some things in version 1 were named differently. Try those as well if v1.
        if (asset->meta.version == 1) {
            // fallback types.
            if (i == KASSET_SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN) {
                if (strings_equali("terrain", type_str)) {
                    attachment->type = i;
                    type_found = true;
                    break;
                }
            }
        }
    }
    if (!type_found) {
        KERROR("Unrecognized attachment type '%s'. Attachment deserialization failed.", type_str);
        return false;
    }

    // Process based on attachment type.
    switch (attachment->type) {

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_SKYBOX: {
        kasset_scene_node_attachment_skybox* typed_attachment = (kasset_scene_node_attachment_skybox*)attachment;

        // Cubemap name
        if (!kson_object_property_value_get_string(attachment_obj, "cubemap_image_asset_name", &typed_attachment->cubemap_image_asset_name)) {
            // Try fallback name if v1.
            if (asset->meta.version == 1) {
                if (!kson_object_property_value_get_string(attachment_obj, "cubemap_name", &typed_attachment->cubemap_image_asset_name)) {
                    KERROR("Failed to add 'cubemap_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            } else {
                KERROR("Failed to add 'cubemap_image_asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT: {
        kasset_scene_node_attachment_directional_light* typed_attachment = (kasset_scene_node_attachment_directional_light*)attachment;

        // Colour
        if (!kson_object_property_value_get_vec4(attachment_obj, "colour", &typed_attachment->colour)) {
            KERROR("Failed to get 'colour' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Direction
        if (!kson_object_property_value_get_vec4(attachment_obj, "direction", &typed_attachment->direction)) {
            KERROR("Failed to get 'direction' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_distance
        if (!kson_object_property_value_get_float(attachment_obj, "shadow_distance", &typed_attachment->shadow_distance)) {
            KERROR("Failed to get 'shadow_distance' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_fade_distance
        if (!kson_object_property_value_get_float(attachment_obj, "shadow_fade_distance", &typed_attachment->shadow_fade_distance)) {
            KERROR("Failed to get 'shadow_fade_distance' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_split_mult
        if (!kson_object_property_value_get_float(attachment_obj, "shadow_split_mult", &typed_attachment->shadow_split_mult)) {
            KERROR("Failed to get 'shadow_split_mult' property for attachment '%s'.", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT: {
        kasset_scene_node_attachment_point_light* typed_attachment = (kasset_scene_node_attachment_point_light*)attachment;

        // Colour
        if (!kson_object_property_value_get_vec4(attachment_obj, "colour", &typed_attachment->colour)) {
            KERROR("Failed to get 'colour' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Position
        if (!kson_object_property_value_get_vec4(attachment_obj, "position", &typed_attachment->position)) {
            KERROR("Failed to get 'position' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Constant
        if (!kson_object_property_value_get_float(attachment_obj, "constant_f", &typed_attachment->constant_f)) {
            KERROR("Failed to get 'constant_f' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Linear
        if (!kson_object_property_value_get_float(attachment_obj, "linear", &typed_attachment->linear)) {
            KERROR("Failed to get 'linear' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Quadratic
        if (!kson_object_property_value_get_float(attachment_obj, "quadratic", &typed_attachment->quadratic)) {
            KERROR("Failed to get 'quadratic' property for attachment '%s'.", attachment_name);
            return false;
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH: {
        kasset_scene_node_attachment_static_mesh* typed_attachment = (kasset_scene_node_attachment_static_mesh*)attachment;

        // Asset name
        if (!kson_object_property_value_get_string(attachment_obj, "asset_name", &typed_attachment->asset_name)) {

            // Try fallback.
            if (asset->meta.version == 1) {
                if (!kson_object_property_value_get_string(attachment_obj, "resource_name", &typed_attachment->asset_name)) {
                    KERROR("Failed to get 'resource_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            } else {
                KERROR("Failed to get 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN: {
        kasset_scene_node_attachment_heightmap_terrain* typed_attachment = (kasset_scene_node_attachment_heightmap_terrain*)attachment;

        // Asset name
        if (!kson_object_property_value_get_string(attachment_obj, "asset_name", &typed_attachment->asset_name)) {

            // Try fallback.
            if (asset->meta.version == 1) {
                if (!kson_object_property_value_get_string(attachment_obj, "resource_name", &typed_attachment->asset_name)) {
                    KERROR("Failed to get 'resource_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            } else {
                KERROR("Failed to get 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        }
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE: {
        // NOTE: Intentionally blank until additional config is added to water planes.
    } break;

    case KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT:
        KERROR("Stop trying to serialize the count member of the enum, ya dingus!");
        return false;
    }

    return true;
}
