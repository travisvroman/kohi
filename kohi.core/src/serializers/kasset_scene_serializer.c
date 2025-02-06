#include "kasset_scene_serializer.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "core_audio_types.h"
#include "core_physics_types.h"
#include "core_resource_types.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"
#include "strings/kstring.h"

// The current scene version.
#define SCENE_ASSET_CURRENT_VERSION 2

static b8 serialize_node(scene_node_config* node, kson_object* node_obj);

static b8 deserialize_node(kasset* asset, scene_node_config* node, kson_object* node_obj);
static b8 deserialize_attachment(kasset* asset, scene_node_config* node, kson_object* attachment_obj);

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

            if (typed_asset->base.meta.version > SCENE_ASSET_CURRENT_VERSION) {
                KERROR("Parsed scene version '%u' is beyond what the current version '%u' is. Check file format. Deserialization failed.", typed_asset->base.meta.version, SCENE_ASSET_CURRENT_VERSION);
                return false;
            }

            // Description comes from here, but is still optional.
            kson_object_property_value_get_string(&tree.root, "description", &typed_asset->description);
        }

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
                typed_asset->physics_gravity = (vec3){0, -9.8f, 0};
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

    // User-defined configs are output directly as-is, not processed here.
    if (node->user_defined_configs) {
        u32 length = darray_length(node->user_defined_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_user_defined_config* typed_attachment = &node->user_defined_configs[i];
            /* scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment; */

            // Deserialize config source to a temp KSON tree.
            kson_tree temp = {0};
            kson_tree_from_string(typed_attachment->config_source, &temp);

            // Add that tree's root as the attachment object.
            kson_array_value_add_object(&attachment_obj_array, temp.root);

            // Cleanup the temp tree.
            kson_tree_cleanup(&temp);
        }
    }

    if (node->skybox_configs) {
        u32 length = darray_length(node->skybox_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_skybox_config* typed_attachment = &node->skybox_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Cubemap name
            kname cubemap_name = typed_attachment->cubemap_image_asset_name ? typed_attachment->cubemap_image_asset_name : kname_create("default_skybox");
            if (!kson_object_value_add_kname_as_string(&attachment_obj, "cubemap_image_asset_name", cubemap_name)) {
                KERROR("Failed to add 'cubemap_image_asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Package name, if it exists.
            if (typed_attachment->cubemap_image_asset_package_name) {
                if (!kson_object_value_add_kname_as_string(&attachment_obj, "package_name", typed_attachment->cubemap_image_asset_package_name)) {
                    KERROR("Failed to add 'package_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->dir_light_configs) {
        u32 length = darray_length(node->dir_light_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_directional_light_config* typed_attachment = &node->dir_light_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Colour
            if (!kson_object_value_add_vec4(&attachment_obj, "colour", typed_attachment->colour)) {
                KERROR("Failed to add 'colour' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Direction
            if (!kson_object_value_add_vec4(&attachment_obj, "direction", typed_attachment->direction)) {
                KERROR("Failed to add 'direction' property for attachment '%s'.", attachment_name);
                return false;
            }

            // shadow_distance
            if (!kson_object_value_add_float(&attachment_obj, "shadow_distance", typed_attachment->shadow_distance)) {
                KERROR("Failed to add 'shadow_distance' property for attachment '%s'.", attachment_name);
                return false;
            }

            // shadow_fade_distance
            if (!kson_object_value_add_float(&attachment_obj, "shadow_fade_distance", typed_attachment->shadow_fade_distance)) {
                KERROR("Failed to add 'shadow_fade_distance' property for attachment '%s'.", attachment_name);
                return false;
            }

            // shadow_split_mult
            if (!kson_object_value_add_float(&attachment_obj, "shadow_split_mult", typed_attachment->shadow_split_mult)) {
                KERROR("Failed to add 'shadow_split_mult' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->point_light_configs) {
        u32 length = darray_length(node->point_light_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_point_light_config* typed_attachment = &node->point_light_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Colour
            if (!kson_object_value_add_vec4(&attachment_obj, "colour", typed_attachment->colour)) {
                KERROR("Failed to add 'colour' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Position
            if (!kson_object_value_add_vec4(&attachment_obj, "position", typed_attachment->position)) {
                KERROR("Failed to add 'position' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Constant
            if (!kson_object_value_add_float(&attachment_obj, "constant_f", typed_attachment->constant_f)) {
                KERROR("Failed to add 'constant_f' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Linear
            if (!kson_object_value_add_float(&attachment_obj, "linear", typed_attachment->linear)) {
                KERROR("Failed to add 'linear' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Quadratic
            if (!kson_object_value_add_float(&attachment_obj, "quadratic", typed_attachment->quadratic)) {
                KERROR("Failed to add 'quadratic' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->audio_emitter_configs) {
        u32 length = darray_length(node->audio_emitter_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_audio_emitter_config* typed_attachment = &node->audio_emitter_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // volume
            if (!kson_object_value_add_float(&attachment_obj, "volume", typed_attachment->volume)) {
                KERROR("Failed to add 'volume' property for attachment '%s'.", attachment_name);
                return false;
            }

            // is_looping
            if (!kson_object_value_add_boolean(&attachment_obj, "is_looping", typed_attachment->is_looping)) {
                KERROR("Failed to add 'is_looping' property for attachment '%s'.", attachment_name);
                return false;
            }

            // inner_radius
            if (!kson_object_value_add_float(&attachment_obj, "inner_radius", typed_attachment->inner_radius)) {
                KERROR("Failed to add 'inner_radius' property for attachment '%s'.", attachment_name);
                return false;
            }

            // outer_radius
            if (!kson_object_value_add_float(&attachment_obj, "outer_radius", typed_attachment->outer_radius)) {
                KERROR("Failed to add 'outer_radius' property for attachment '%s'.", attachment_name);
                return false;
            }

            // falloff
            if (!kson_object_value_add_float(&attachment_obj, "falloff", typed_attachment->falloff)) {
                KERROR("Failed to add 'falloff' property for attachment '%s'.", attachment_name);
                return false;
            }

            // is_streaming
            if (!kson_object_value_add_boolean(&attachment_obj, "is_streaming", typed_attachment->is_streaming)) {
                KERROR("Failed to add 'is_streaming' property for attachment '%s'.", attachment_name);
                return false;
            }

            // audio_resource_name
            if (!kson_object_value_add_kname_as_string(&attachment_obj, "audio_resource_name", typed_attachment->audio_resource_name)) {
                KERROR("Failed to add 'audio_resource_name' property for attachment '%s'.", attachment_name);
                return false;
            }

            // audio_resource_package_name
            if (!kson_object_value_add_kname_as_string(&attachment_obj, "audio_resource_package_name", typed_attachment->audio_resource_package_name)) {
                KERROR("Failed to add 'audio_resource_package_name' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->static_mesh_configs) {
        u32 length = darray_length(node->static_mesh_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_static_mesh_config* typed_attachment = &node->static_mesh_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Asset name
            kname cubemap_name = typed_attachment->asset_name ? typed_attachment->asset_name : kname_create("default_static_mesh");
            if (!kson_object_value_add_kname_as_string(&attachment_obj, "asset_name", cubemap_name)) {
                KERROR("Failed to add 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Package name, if it exists.
            if (typed_attachment->package_name) {
                if (!kson_object_value_add_kname_as_string(&attachment_obj, "package_name", typed_attachment->package_name)) {
                    KERROR("Failed to add 'package_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->heightmap_terrain_configs) {
        u32 length = darray_length(node->heightmap_terrain_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_heightmap_terrain_config* typed_attachment = &node->heightmap_terrain_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Asset name
            kname cubemap_name = typed_attachment->asset_name ? typed_attachment->asset_name : kname_create("default_terrain");
            if (!kson_object_value_add_kname_as_string(&attachment_obj, "asset_name", cubemap_name)) {
                KERROR("Failed to add 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }

            // Package name, if it exists.
            if (typed_attachment->package_name) {
                if (!kson_object_value_add_kname_as_string(&attachment_obj, "package_name", typed_attachment->package_name)) {
                    KERROR("Failed to add 'package_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->water_plane_configs) {
        u32 length = darray_length(node->water_plane_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_water_plane_config* typed_attachment = &node->water_plane_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // NOTE: No extra properties for now until additional config is added to water planes.

            // Add it to the attachments array.
            kson_array_value_add_object(&attachment_obj_array, attachment_obj);
        }
    }

    if (node->physics_body_configs) {
        u32 length = darray_length(node->physics_body_configs);
        for (u32 i = 0; i < length; ++i) {
            scene_node_attachment_physics_body_config* typed_attachment = &node->physics_body_configs[i];
            scene_node_attachment_config* attachment = (scene_node_attachment_config*)typed_attachment;
            kson_object attachment_obj = kson_object_create();
            const char* attachment_name = kname_string_get(attachment->name);

            // Base properties
            {
                // Name, if it exists.
                if (attachment->name) {
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "name", attachment->name)) {
                        KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                        return false;
                    }
                }

                // Add the type. Required.
                const char* type_str = scene_node_attachment_type_strings[attachment->type];
                if (!kson_object_value_add_string(&attachment_obj, "type", type_str)) {
                    KERROR("Failed to add 'name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Specific properties.
            // Mass
            {
                if (!kson_object_value_add_float(&attachment_obj, "mass", typed_attachment->mass)) {
                    KERROR("Failed to add 'mass' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Inertia
            {
                if (!kson_object_value_add_float(&attachment_obj, "inertia", typed_attachment->inertia)) {
                    KERROR("Failed to add 'inertia' property for attachment '%s'.", attachment_name);
                    return false;
                }
            }

            // Shape type
            {
                char* shape_type_str = 0;
                switch (typed_attachment->shape_type) {
                case KPHYSICS_SHAPE_TYPE_SPHERE:
                    shape_type_str = "sphere";
                    break;
                case KPHYSICS_SHAPE_TYPE_RECTANGLE:
                    shape_type_str = "rectangle";
                    break;
                case KPHYSICS_SHAPE_TYPE_MESH:
                    shape_type_str = "mesh";
                    break;
                }

                if (!kson_object_value_add_string(&attachment_obj, "body_type", shape_type_str)) {
                    KERROR("Failed to add 'body_type' property for attachment '%s'.", attachment_name);
                    return false;
                }

                // Only doing this a second time so these are added in a resonable order.
                switch (typed_attachment->shape_type) {
                case KPHYSICS_SHAPE_TYPE_SPHERE:
                    if (!kson_object_value_add_float(&attachment_obj, "radius", typed_attachment->radius)) {
                        KERROR("Failed to add property 'radius' to attachment '%s'.", attachment_name);
                        return false;
                    }
                    break;
                case KPHYSICS_SHAPE_TYPE_RECTANGLE:
                    if (!kson_object_value_add_vec3(&attachment_obj, "extents", typed_attachment->extents)) {
                        KERROR("Failed to add property 'radius' to attachment '%s'.", attachment_name);
                        return false;
                    }
                    break;
                case KPHYSICS_SHAPE_TYPE_MESH:
                    if (!kson_object_value_add_kname_as_string(&attachment_obj, "mesh_resource_name", typed_attachment->mesh_resource_name)) {
                        KERROR("Failed to add property 'mesh_resource_name' to attachment '%s'.", attachment_name);
                        return false;
                    }
                    break;
                }
            }
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
        u32 attachment_count;
        if (!kson_array_element_count_get(&attachment_obj_array, (u32*)(&attachment_count))) {
            KERROR("Failed to parse attachment count. Invalid format?");
            return false;
        }

        // Setup the attachment array and deserialize.
        for (u32 i = 0; i < attachment_count; ++i) {
            kson_object attachment_obj;
            if (!kson_array_element_value_get_object(&attachment_obj_array, i, &attachment_obj)) {
                KWARN("Unable to read attachment at index %u. Skipping.", i);
                continue;
            }

            // Deserialize attachment.
            if (!deserialize_attachment(asset, node, &attachment_obj)) {
                KERROR("Failed to deserialize attachment at index %u. Skipping.", i);
                continue;
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
static b8 deserialize_attachment(kasset* asset, scene_node_config* node, kson_object* attachment_obj) {

    // Name, if it exists. Optional.
    kname name = INVALID_KNAME;
    kson_object_property_value_get_string_as_kname(attachment_obj, "name", &name);

    kname attachment_name = name ? name : kname_create("unnamed-attachment");

    // Parse the type.
    const char* type_str = 0; // scene_node_attachment_type_strings[attachment->type];
    if (!kson_object_property_value_get_string(attachment_obj, "type", &type_str)) {
        KERROR("Failed to parse required 'type' property for attachment '%s'.", attachment_name);
        return false;
    }

    // Find the attachment type.
    scene_node_attachment_type type = SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN;
    for (u32 i = 0; i < SCENE_NODE_ATTACHMENT_TYPE_COUNT; ++i) {
        if (strings_equali(scene_node_attachment_type_strings[i], type_str)) {
            type = (scene_node_attachment_type)i;
            break;
        }

        // Some things in version 1 were named differently. Try those as well if v1.
        if (asset->meta.version == 1) {
            // fallback types.
            if (i == SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN) {
                if (strings_equali("terrain", type_str)) {
                    type = (scene_node_attachment_type)i;
                    break;
                }
            }
        }
    }
    if (type == SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN) {
        // Switch to user-defined if no match is found.
        type = SCENE_NODE_ATTACHMENT_TYPE_USER_DEFINED;
    }

    // Process based on attachment type.
    switch (type) {
    case SCENE_NODE_ATTACHMENT_TYPE_USER_DEFINED: {
        scene_node_attachment_user_defined_config typed_attachment = {0};

        // Store off the source as a string and deserialize later.
        kson_tree temp = {0};
        temp.root = *attachment_obj;
        typed_attachment.config_source = kson_tree_to_string(&temp);

        // Push to the appropriate array.
        if (!node->user_defined_configs) {
            node->user_defined_configs = darray_create(scene_node_attachment_user_defined_config);
        }
        darray_push(node->user_defined_configs, typed_attachment);
    } break;
    case SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN: {
        KERROR("Stop trying to deserialize the unknown member of the enum, ya dingus!");
        return false;
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_SKYBOX: {
        scene_node_attachment_skybox_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // Cubemap name
        if (!kson_object_property_value_get_string_as_kname(attachment_obj, "cubemap_image_asset_name", &typed_attachment.cubemap_image_asset_name)) {
            // Try fallback name if v1.
            if (asset->meta.version == 1) {
                if (!kson_object_property_value_get_string_as_kname(attachment_obj, "cubemap_name", &typed_attachment.cubemap_image_asset_name)) {
                    KERROR("Failed to add 'cubemap_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            } else {
                KERROR("Failed to add 'cubemap_image_asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        }

        // Package name. Optional.
        kson_object_property_value_get_string_as_kname(attachment_obj, "package_name", &typed_attachment.cubemap_image_asset_package_name);

        // Push to the appropriate array.
        if (!node->skybox_configs) {
            node->skybox_configs = darray_create(scene_node_attachment_skybox_config);
        }
        darray_push(node->skybox_configs, typed_attachment);

    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT: {
        scene_node_attachment_directional_light_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // Colour
        if (!kson_object_property_value_get_vec4(attachment_obj, "colour", &typed_attachment.colour)) {
            KERROR("Failed to get 'colour' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Direction
        if (!kson_object_property_value_get_vec4(attachment_obj, "direction", &typed_attachment.direction)) {
            KERROR("Failed to get 'direction' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_distance
        if (!kson_object_property_value_get_float(attachment_obj, "shadow_distance", &typed_attachment.shadow_distance)) {
            KERROR("Failed to get 'shadow_distance' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_fade_distance
        if (!kson_object_property_value_get_float(attachment_obj, "shadow_fade_distance", &typed_attachment.shadow_fade_distance)) {
            KERROR("Failed to get 'shadow_fade_distance' property for attachment '%s'.", attachment_name);
            return false;
        }

        // shadow_split_mult
        if (!kson_object_property_value_get_float(attachment_obj, "shadow_split_mult", &typed_attachment.shadow_split_mult)) {
            KERROR("Failed to get 'shadow_split_mult' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Push to the appropriate array.
        if (!node->dir_light_configs) {
            node->dir_light_configs = darray_create(scene_node_attachment_directional_light_config);
        }
        darray_push(node->dir_light_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT: {
        scene_node_attachment_point_light_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // Colour
        if (!kson_object_property_value_get_vec4(attachment_obj, "colour", &typed_attachment.colour)) {
            KERROR("Failed to get 'colour' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Position
        if (!kson_object_property_value_get_vec4(attachment_obj, "position", &typed_attachment.position)) {
            KERROR("Failed to get 'position' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Constant
        if (!kson_object_property_value_get_float(attachment_obj, "constant_f", &typed_attachment.constant_f)) {
            KERROR("Failed to get 'constant_f' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Linear
        if (!kson_object_property_value_get_float(attachment_obj, "linear", &typed_attachment.linear)) {
            KERROR("Failed to get 'linear' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Quadratic
        if (!kson_object_property_value_get_float(attachment_obj, "quadratic", &typed_attachment.quadratic)) {
            KERROR("Failed to get 'quadratic' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Push to the appropriate array.
        if (!node->point_light_configs) {
            node->point_light_configs = darray_create(scene_node_attachment_point_light_config);
        }
        darray_push(node->point_light_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_AUDIO_EMITTER: {
        scene_node_attachment_audio_emitter_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // volume - optional
        if (!kson_object_property_value_get_float(attachment_obj, "volume", &typed_attachment.volume)) {
            typed_attachment.volume = AUDIO_VOLUME_DEFAULT;
        }

        // is_looping - optional
        if (!kson_object_property_value_get_bool(attachment_obj, "is_looping", &typed_attachment.is_looping)) {
            // Emitters always default to true for looping, if not defined.
            typed_attachment.is_looping = true;
        }

        // inner_radius - optional
        if (!kson_object_property_value_get_float(attachment_obj, "inner_radius", &typed_attachment.inner_radius)) {
            typed_attachment.inner_radius = AUDIO_INNER_RADIUS_DEFAULT;
        }

        // outer_radius - optional
        if (!kson_object_property_value_get_float(attachment_obj, "outer_radius", &typed_attachment.outer_radius)) {
            typed_attachment.outer_radius = AUDIO_OUTER_RADIUS_DEFAULT;
        }

        // falloff - optional
        if (!kson_object_property_value_get_float(attachment_obj, "falloff", &typed_attachment.falloff)) {
            typed_attachment.falloff = AUDIO_FALLOFF_DEFAULT;
        }

        // is_streaming - optional - defaults to false
        if (!kson_object_property_value_get_bool(attachment_obj, "is_streaming", &typed_attachment.is_streaming)) {
            typed_attachment.is_streaming = false;
        }

        // audio_resource_name - required
        if (!kson_object_property_value_get_string_as_kname(attachment_obj, "audio_resource_name", &typed_attachment.audio_resource_name)) {
            KERROR("Failed to get 'audio_resource_name' property for attachment '%s'.", attachment_name);
            return false;
        }

        // audio_resource_package_name - required
        if (!kson_object_property_value_get_string_as_kname(attachment_obj, "audio_resource_package_name", &typed_attachment.audio_resource_package_name)) {
            KERROR("Failed to get 'audio_resource_package_name' property for attachment '%s'.", attachment_name);
            return false;
        }

        // Push to the appropriate array.
        if (!node->audio_emitter_configs) {
            node->audio_emitter_configs = darray_create(scene_node_attachment_audio_emitter_config);
        }
        darray_push(node->audio_emitter_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH: {
        scene_node_attachment_static_mesh_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // Asset name
        if (!kson_object_property_value_get_string_as_kname(attachment_obj, "asset_name", &typed_attachment.asset_name)) {

            // Try fallback.
            if (asset->meta.version == 1) {
                if (!kson_object_property_value_get_string_as_kname(attachment_obj, "resource_name", &typed_attachment.asset_name)) {
                    KERROR("Failed to get 'resource_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            } else {
                KERROR("Failed to get 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        }

        // Package name. Optional.
        kson_object_property_value_get_string_as_kname(attachment_obj, "package_name", &typed_attachment.package_name);

        // Push to the appropriate array.
        if (!node->static_mesh_configs) {
            node->static_mesh_configs = darray_create(scene_node_attachment_static_mesh_config);
        }
        darray_push(node->static_mesh_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN: {
        scene_node_attachment_heightmap_terrain_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // Asset name
        if (!kson_object_property_value_get_string_as_kname(attachment_obj, "asset_name", &typed_attachment.asset_name)) {

            // Try fallback.
            if (asset->meta.version == 1) {
                if (!kson_object_property_value_get_string_as_kname(attachment_obj, "resource_name", &typed_attachment.asset_name)) {
                    KERROR("Failed to get 'resource_name' property for attachment '%s'.", attachment_name);
                    return false;
                }
            } else {
                KERROR("Failed to get 'asset_name' property for attachment '%s'.", attachment_name);
                return false;
            }
        }

        // Package name. Optional.
        kson_object_property_value_get_string_as_kname(attachment_obj, "package_name", &typed_attachment.package_name);

        // Push to the appropriate array.
        if (!node->heightmap_terrain_configs) {
            node->heightmap_terrain_configs = darray_create(scene_node_attachment_heightmap_terrain_config);
        }
        darray_push(node->heightmap_terrain_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE: {
        scene_node_attachment_water_plane_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;
        // NOTE: Intentionally blank until additional config is added to water planes.

        // Push to the appropriate array.
        if (!node->water_plane_configs) {
            node->water_plane_configs = darray_create(scene_node_attachment_water_plane_config);
        }
        darray_push(node->water_plane_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_PHYSICS_BODY: {
        scene_node_attachment_physics_body_config typed_attachment = {0};
        typed_attachment.base.name = attachment_name;

        // Mass is optional and defaults to 0 (i.e. static body).
        {
            if (!kson_object_property_value_get_float(attachment_obj, "mass", &typed_attachment.mass)) {
                typed_attachment.mass = 0;
            }
        }

        // Inertia is optional and defaults to 0 (i.e. changes direction sharply).
        {
            if (!kson_object_property_value_get_float(attachment_obj, "inertia", &typed_attachment.inertia)) {
                typed_attachment.inertia = 0;
            }
        }

        // Shape type is required.
        {
            const char* shape_type_str = 0;
            if (!kson_object_property_value_get_string(attachment_obj, "shape_type", &shape_type_str)) {
                KERROR("Failed to get required 'shape_type' property for attachment '%s'.", attachment_name);
                return false;
            }
            if (strings_equali(shape_type_str, "sphere")) {
                typed_attachment.shape_type = KPHYSICS_SHAPE_TYPE_SPHERE;
            } else if (strings_equali(shape_type_str, "rect") || strings_equali(shape_type_str, "rectangle")) {
                typed_attachment.shape_type = KPHYSICS_SHAPE_TYPE_RECTANGLE;
            } else if (strings_equali(shape_type_str, "mesh")) {
                typed_attachment.shape_type = KPHYSICS_SHAPE_TYPE_MESH;
            } else {
                KERROR("Unrecognized physics shape type '%s'. Skipping.", shape_type_str);
                return false;
            }
        }

        // Extract required properties based on shape type.
        switch (typed_attachment.shape_type) {
        case KPHYSICS_SHAPE_TYPE_SPHERE: {
            // Sphere just requires radius
            if (!kson_object_property_value_get_float(attachment_obj, "radius", &typed_attachment.radius)) {
                KERROR("Attachment '%s', of type 'sphere' did not contain required property 'radius'.", attachment_name);
                return false;
            }
        } break;
        case KPHYSICS_SHAPE_TYPE_RECTANGLE: {
            // Rectangle just requires extents
            if (!kson_object_property_value_get_vec3(attachment_obj, "extents", &typed_attachment.extents)) {
                KERROR("Attachment '%s', of type 'rectangle' did not contain required property 'extents'.", attachment_name);
                return false;
            }
        } break;
        case KPHYSICS_SHAPE_TYPE_MESH: {
            // Mesh just requires the name of the mesh resource to load as collision.
            if (!kson_object_property_value_get_string_as_kname(attachment_obj, "mesh_resource_name", &typed_attachment.mesh_resource_name)) {
                KERROR("Attachment '%s', of type 'mesh' did not contain required property 'mesh_resource_name'.", attachment_name);
                return false;
            }
        } break;
        }

        // Push to the appropriate array.
        if (!node->physics_body_configs) {
            node->physics_body_configs = darray_create(scene_node_attachment_physics_body_config);
        }
        darray_push(node->physics_body_configs, typed_attachment);
    } break;

    case SCENE_NODE_ATTACHMENT_TYPE_COUNT:
        KERROR("Stop trying to serialize the count member of the enum, ya dingus!");
        return false;
    }

    return true;
}
