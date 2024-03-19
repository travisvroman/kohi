#include "scene_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "parsers/kson_parser.h"
#include "platform/filesystem.h"
#include "resources/loaders/loader_utils.h"
#include "resources/resource_types.h"
#include "resources/scene.h"
#include "systems/resource_system.h"

#define SHADOW_DISTANCE_DEFAULT 200.0f
#define SHADOW_FADE_DISTANCE_DEFAULT 25.0f
#define SHADOW_SPLIT_MULT_DEFAULT 0.95f;

static b8 deserialize_scene_directional_light_attachment(const kson_object* attachment_object, scene_node_attachment_directional_light* attachment) {
    if (!attachment_object || !attachment) {
        return false;
    }

    // NOTE: all properties are optional, with defaults provided.

    // Colour
    attachment->colour = vec4_create(50, 50, 50, 1);  // default to white.
    const char* colour_string = 0;
    if (kson_object_property_value_get_string(attachment_object, "colour", &colour_string)) {
        string_to_vec4(colour_string, &attachment->colour);
    }

    // Direction
    attachment->direction = vec4_create(0, -1, 0, 1);  // default to down.
    const char* direction_string = 0;
    if (kson_object_property_value_get_string(attachment_object, "direction", &direction_string)) {
        string_to_vec4(direction_string, &attachment->direction);
    }

    // shadow distance
    attachment->shadow_distance = 200.0f;
    kson_object_property_value_get_float(attachment_object, "shadow_distance", &attachment->shadow_distance);

    // shadow fade distance
    attachment->shadow_fade_distance = 25.0f;
    kson_object_property_value_get_float(attachment_object, "shadow_fade_distance", &attachment->shadow_fade_distance);

    // shadow split multiplier
    attachment->shadow_split_mult = 0.44f;
    kson_object_property_value_get_float(attachment_object, "shadow_split_mult", &attachment->shadow_split_mult);

    return true;
}

static b8 deserialize_scene_point_light_attachment(const kson_object* attachment_object, scene_node_attachment_point_light* attachment) {
    if (!attachment_object || !attachment) {
        return false;
    }

    // NOTE: all properties are optional, with defaults provided.

    // Colour
    attachment->colour = vec4_create(50, 50, 50, 1);  // default to white.
    const char* colour_string = 0;
    if (kson_object_property_value_get_string(attachment_object, "colour", &colour_string)) {
        string_to_vec4(colour_string, &attachment->colour);
    }

    // Position
    attachment->position = vec4_zero();  // default to origin.
    const char* position_string = 0;
    if (kson_object_property_value_get_string(attachment_object, "position", &position_string)) {
        string_to_vec4(position_string, &attachment->position);
    }

    // constant_f
    attachment->constant_f = 1.0f;
    kson_object_property_value_get_float(attachment_object, "constant_f", &attachment->constant_f);

    // linear
    attachment->linear = 0.35f;
    kson_object_property_value_get_float(attachment_object, "linear", &attachment->linear);

    // quadratic
    attachment->quadratic = 0.44f;
    kson_object_property_value_get_float(attachment_object, "quadratic", &attachment->quadratic);

    return true;
}

static b8 deserialize_scene_static_mesh_attachment(const kson_object* attachment_object, scene_node_attachment_static_mesh* attachment) {
    if (!attachment_object || !attachment) {
        return false;
    }

    const char* resource_name_str = 0;
    if (!kson_object_property_value_get_string(attachment_object, "resource_name", &resource_name_str)) {
        KERROR("Static mesh attachment config requires a valid 'resource_name'. Deserialization failed.");
        return false;
    }
    attachment->resource_name = string_duplicate(resource_name_str);

    return true;
}

static b8 deserialize_scene_terrain_attachment(const kson_object* attachment_object, scene_node_attachment_terrain* attachment) {
    if (!attachment_object || !attachment) {
        return false;
    }

    const char* name_str = 0;
    if (!kson_object_property_value_get_string(attachment_object, "name", &name_str)) {
        KERROR("Terrain attachment config requires a valid 'name'. Deserialization failed.");
        return false;
    }
    attachment->name = string_duplicate(name_str);

    const char* resource_name_str = 0;
    if (!kson_object_property_value_get_string(attachment_object, "resource_name", &resource_name_str)) {
        KERROR("Terrain attachment config requires a valid 'resource_name'. Deserialization failed.");
        return false;
    }
    attachment->resource_name = string_duplicate(resource_name_str);

    return true;
}

static b8 deserialize_scene_skybox_attachment(const kson_object* attachment_object, scene_node_attachment_skybox* attachment) {
    if (!attachment_object || !attachment) {
        return false;
    }

    const char* cubemap_name_str = 0;
    if (!kson_object_property_value_get_string(attachment_object, "cubemap_name", &cubemap_name_str)) {
        KERROR("Static mesh attachment config requires a valid 'cubemap_name'. Deserialization failed.");
        return false;
    }
    attachment->cubemap_name = string_duplicate(cubemap_name_str);

    return true;
}

static scene_node_attachment_type scene_attachment_type_from_string(const char* str) {
    if (!str) {
        return SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN;
    }

    if (strings_equali(str, "static_mesh")) {
        return SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    } else if (strings_equali(str, "terrain")) {
        return SCENE_NODE_ATTACHMENT_TYPE_TERRAIN;
    } else if (strings_equali(str, "skybox")) {
        return SCENE_NODE_ATTACHMENT_TYPE_SKYBOX;
    } else if (strings_equali(str, "directional_light")) {
        return SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT;
    } else if (strings_equali(str, "point_light")) {
        return SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT;
    } else {
        return SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN;
    }
}

b8 scene_node_config_deserialize_kson(const kson_object* node_object, scene_node_config* out_node_config) {
    if (!node_object) {
        KERROR("scene_node_config_deserialize_kson requires a valid pointer to node_object.");
        return false;
    }

    if (!out_node_config) {
        KERROR("scene_node_config_deserialize_kson requires a valid pointer to out_node_config.");
        return false;
    }

    if (node_object->type != KSON_OBJECT_TYPE_OBJECT) {
        KERROR("Unexpected property type. Skipping.");
        return false;
    }

    // Name
    const char* node_name = 0;
    if (!kson_object_property_value_get_string(node_object, "name", &node_name)) {
        node_name = "";
    }
    out_node_config->name = string_duplicate(node_name);

    // xform, if there is one.
    const char* xform_string = 0;
    if (kson_object_property_value_get_string(node_object, "xform", &xform_string)) {
        // Found an xform, deserialize it into config.
        out_node_config->xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
        string_to_xform_config(xform_string, out_node_config->xform);
    } else {
        out_node_config->xform = 0;
    }

    // Process attachments, if any.
    kson_object attachments_array = {0};
    if (kson_object_property_value_get_object(node_object, "attachments", &attachments_array)) {
        // Make sure it is actually an array.
        if (attachments_array.type == KSON_OBJECT_TYPE_ARRAY) {
            u32 attachment_count = 0;
            kson_array_element_count_get(&attachments_array, &attachment_count);

            // Each attachment
            for (u32 attachment_index = 0; attachment_index < attachment_count; ++attachment_index) {
                // Get the object.
                kson_object attachment_object = {0};
                if (!kson_array_element_value_get_object(&attachments_array, attachment_index, &attachment_object)) {
                    KERROR("Unable to get attachment object at index %u.", attachment_index);
                    continue;
                }

                // Confirm it is an object, not an array.
                if (attachment_object.type != KSON_OBJECT_TYPE_OBJECT) {
                    KERROR("Expected object type of object for attachment. Skipping.");
                    continue;
                }

                // Attachment type.
                const char* attachment_type_str = 0;
                if (!kson_object_property_value_get_string(&attachment_object, "type", &attachment_type_str)) {
                    KERROR("Unable to determine attachment type. Skipping.");
                    continue;
                }
                scene_node_attachment_type attachment_type = scene_attachment_type_from_string(attachment_type_str);

                scene_node_attachment_config new_attachment = {0};
                new_attachment.type = attachment_type;

                // Deserialize the attachment.
                switch (attachment_type) {
                    case SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH: {
                        new_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
                        if (!deserialize_scene_static_mesh_attachment(&attachment_object, new_attachment.attachment_data)) {
                            KERROR("Failed to deserialize attachment. Skipping.");
                            kfree(new_attachment.attachment_data, sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
                            continue;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_TERRAIN: {
                        new_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_terrain), MEMORY_TAG_SCENE);
                        if (!deserialize_scene_terrain_attachment(&attachment_object, new_attachment.attachment_data)) {
                            KERROR("Failed to deserialize attachment. Skipping.");
                            kfree(new_attachment.attachment_data, sizeof(scene_node_attachment_terrain), MEMORY_TAG_SCENE);
                            continue;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_SKYBOX: {
                        new_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_skybox), MEMORY_TAG_SCENE);
                        if (!deserialize_scene_skybox_attachment(&attachment_object, new_attachment.attachment_data)) {
                            KERROR("Failed to deserialize attachment. Skipping.");
                            kfree(new_attachment.attachment_data, sizeof(scene_node_attachment_skybox), MEMORY_TAG_SCENE);
                            continue;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT: {
                        new_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_directional_light), MEMORY_TAG_SCENE);
                        if (!deserialize_scene_directional_light_attachment(&attachment_object, new_attachment.attachment_data)) {
                            KERROR("Failed to deserialize attachment. Skipping.");
                            kfree(new_attachment.attachment_data, sizeof(scene_node_attachment_directional_light), MEMORY_TAG_SCENE);
                            continue;
                        }
                    } break;
                    case SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT: {
                        new_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_point_light), MEMORY_TAG_SCENE);
                        if (!deserialize_scene_point_light_attachment(&attachment_object, new_attachment.attachment_data)) {
                            KERROR("Failed to deserialize attachment. Skipping.");
                            kfree(new_attachment.attachment_data, sizeof(scene_node_attachment_point_light), MEMORY_TAG_SCENE);
                            continue;
                        }
                    }

                    break;
                    default:
                    case SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN:
                        KERROR("Attachment type is unknown. Skipping.");
                        continue;
                }

                // Push the attachment to the node's config.
                if (!out_node_config->attachments) {
                    out_node_config->attachments = darray_create(scene_attachment);
                }
                darray_push(out_node_config->attachments, new_attachment);
            }
        }
    }

    // Process children, if any.
    kson_object children_array = {0};
    if (kson_object_property_value_get_object(node_object, "children", &children_array)) {
        // Make sure it is actually an array.
        if (children_array.type == KSON_OBJECT_TYPE_ARRAY) {
            u32 child_count = 0;
            kson_array_element_count_get(&children_array, &child_count);

            // Each child
            for (u32 child_index = 0; child_index < child_count; ++child_index) {
                // Get the object.
                kson_object child_object = {0};
                if (!kson_array_element_value_get_object(&children_array, child_index, &child_object)) {
                    KERROR("Unable to get child object at index %u.", child_index);
                    continue;
                }

                scene_node_config new_child = {0};

                // Deserialize the child node and push to the array if successful.
                if (scene_node_config_deserialize_kson(&child_object, &new_child)) {
                    // Push the child to the node's children.
                    if (!out_node_config->children) {
                        out_node_config->children = darray_create(scene_attachment);
                    }
                    darray_push(out_node_config->children, new_child);
                }
            }
        }
    }

    return true;
}

b8 scene_config_deserialize_kson(const kson_tree* source_tree, scene_config* scene) {
    // Extract scene properties.
    kson_object scene_properties_obj;
    if (!kson_object_property_value_get_object(&source_tree->root, "properties", &scene_properties_obj)) {
        KERROR("Global scene properties missing. Using defaults.");
        scene->name = "Untitled Scene";
        scene->description = "Default description.";
    } else {
        // Extract name.
        const char* name = 0;
        if (kson_object_property_value_get_string(&scene_properties_obj, "name", &name)) {
            scene->name = string_duplicate(name);
        } else {
            // Use default
            scene->name = "Untitled Scene";
        }

        // Extract description.
        const char* description = 0;
        if (kson_object_property_value_get_string(&scene_properties_obj, "description", &description)) {
            scene->description = string_duplicate(description);
        } else {
            // Use default
            scene->description = "Default description.";
        }
    }

    // Process nodes.
    scene->nodes = darray_create(scene_node_config);

    // Extract and process nodes.
    kson_object scene_nodes_array;
    if (kson_object_property_value_get_object(&source_tree->root, "nodes", &scene_nodes_array)) {
        // Only process if array.
        if (scene_nodes_array.type != KSON_OBJECT_TYPE_ARRAY) {
            KERROR("Unexpected object named 'nodes' found. Expected array instead. Section will be skipped.");
        } else {
            u32 node_count = 0;
            kson_array_element_count_get(&scene_nodes_array, &node_count);
            for (u32 node_index = 0; node_index < node_count; ++node_index) {
                // Setup a new node.
                scene_node_config node_config = {0};

                // Get the node object.
                kson_object node_object = {0};
                if (!kson_array_element_value_get_object(&scene_nodes_array, node_index, &node_object)) {
                    KERROR("Failed to get node object at index %u.", node_index);
                    continue;
                }

                // Deserialize the node and push to the array of root nodes if successful.
                if (scene_node_config_deserialize_kson(&node_object, &node_config)) {
                    darray_push(scene->nodes, node_config);
                }
            }
        }
    }

    return true;

    // LEFTOFF: convert the below to deserialization per type of node/attachment, etc.
    // HACK: temporarily construct a scene hierarchy, will read from file later.

    // scene->name = "test_scene2";
    // scene->description = "A hardcoded test scene.";

    // sponza
    /* scene_node_config sponza = {0};
    sponza.name = "sponza";

    sponza.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    sponza.xform->scale = vec3_create(0.01f, 0.01f, 0.01f);
    sponza.xform->position = vec3_create(0, -1, 0);
    sponza.xform->rotation = quat_identity();

    sponza.attachments = darray_create(scene_node_attachment_config);
    scene_node_attachment_config sponza_mesh_attachment = {0};
    sponza_mesh_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    sponza_mesh_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
    scene_node_attachment_static_mesh* typed_mesh_attachment = sponza_mesh_attachment.attachment_data;
    typed_mesh_attachment->resource_name = "sponza";
    darray_push(sponza.attachments, sponza_mesh_attachment);

    // Create children.
    sponza.children = darray_create(scene_node_config);

    // Tree, a child of sponza
    scene_node_config tree = {0};
    tree.name = "tree";

    tree.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    // Large scale/position to compensate for small scale of parent.
    tree.xform->scale = vec3_create(200.0f, 200.0f, 200.0f);
    tree.xform->position = vec3_create(700.4f, 80.0f, 1400.0f);
    tree.xform->rotation = quat_identity();

    tree.attachments = darray_create(scene_node_attachment_config);
    scene_node_attachment_config tree_mesh_attachment = {0};
    tree_mesh_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    tree_mesh_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
    scene_node_attachment_static_mesh* typed_tree_mesh_attachment = tree_mesh_attachment.attachment_data;
    typed_tree_mesh_attachment->resource_name = "Tree";
    darray_push(tree.attachments, tree_mesh_attachment);

    darray_push(sponza.children, tree);

    // Add to global nodes array.
    darray_push(resource_data->nodes, sponza);

    // falcon
    scene_node_config falcon = {0};
    falcon.name = "falcon";

    falcon.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    falcon.xform->scale = vec3_create(0.35f, 0.35f, 0.35f);
    falcon.xform->position = vec3_create(9.4f, 0.8f, 14.0f);
    falcon.xform->rotation = quat_identity();

    falcon.attachments = darray_create(scene_node_attachment_config);

    scene_node_attachment_config falcon_mesh_attachment = {0};
    falcon_mesh_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    falcon_mesh_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
    scene_node_attachment_static_mesh* falcon_typed_mesh_attachment = falcon_mesh_attachment.attachment_data;
    falcon_typed_mesh_attachment->resource_name = "falcon";
    darray_push(falcon.attachments, falcon_mesh_attachment);

    scene_node_attachment_config falcon_red_light_attachment = {0};
    falcon_red_light_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT;
    falcon_red_light_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_point_light), MEMORY_TAG_SCENE);
    scene_node_attachment_point_light* falcon_red_light_typed_attachment = falcon_red_light_attachment.attachment_data;
    falcon_red_light_typed_attachment->colour = vec4_create(100.0f, 0.0f, 0.0f, 1.0f);
    falcon_red_light_typed_attachment->constant_f = 1.0f;
    falcon_red_light_typed_attachment->linear = 0.35f;
    falcon_red_light_typed_attachment->quadratic = 0.44f;
    falcon_red_light_typed_attachment->position = vec4_create(2.5f, 1.25f, -8.0f, 0.0f);
    darray_push(falcon.attachments, falcon_red_light_attachment);

    // Add to global nodes array.
    darray_push(resource_data->nodes, falcon);

    // terrain
    scene_node_config terrain = {0};
    terrain.name = "falcon";

    terrain.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    terrain.xform->scale = vec3_one();
    terrain.xform->position = vec3_create(-50.0f, -3.9f, -50.0f);
    terrain.xform->rotation = quat_identity();

    terrain.attachments = darray_create(scene_node_attachment_config);
    scene_node_attachment_config terrain_attachment = {0};
    terrain_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_TERRAIN;
    terrain_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_terrain), MEMORY_TAG_SCENE);
    scene_node_attachment_terrain* terrain_typed_mesh_attachment = terrain_attachment.attachment_data;
    terrain_typed_mesh_attachment->resource_name = "test_terrain";
    terrain_typed_mesh_attachment->name = "test_terrain";
    darray_push(terrain.attachments, terrain_attachment);

    // Add to global nodes array.
    darray_push(resource_data->nodes, terrain);

    // Environment
    scene_node_config environment = {0};
    environment.name = "environment";

    environment.attachments = darray_create(scene_node_attachment_config);

    scene_node_attachment_config skybox_attachment = {0};
    skybox_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_SKYBOX;
    skybox_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_skybox), MEMORY_TAG_SCENE);
    scene_node_attachment_skybox* skybox_typed_mesh_attachment = skybox_attachment.attachment_data;
    skybox_typed_mesh_attachment->cubemap_name = "skybox";
    darray_push(environment.attachments, skybox_attachment);

    scene_node_attachment_config dir_light_attachment = {0};
    dir_light_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT;
    dir_light_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_directional_light), MEMORY_TAG_SCENE);
    scene_node_attachment_directional_light* dir_light_typed_mesh_attachment = dir_light_attachment.attachment_data;
    dir_light_typed_mesh_attachment->colour = vec4_create(80.0f, 80.0f, 70.0f, 1.0);
    dir_light_typed_mesh_attachment->direction = vec4_create(0.1f, -1.0f, 0.1f, 1.0f);
    dir_light_typed_mesh_attachment->shadow_distance = 100.0f;
    dir_light_typed_mesh_attachment->shadow_fade_distance = 5.0f;
    dir_light_typed_mesh_attachment->shadow_split_mult = 0.75f;
    darray_push(environment.attachments, dir_light_attachment);

    // Add to global nodes array.
    darray_push(resource_data->nodes, environment); */
}

static b8 scene_loader_load(struct resource_loader* self, const char* name, void* params, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".ksn");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR("scene_loader_load - unable to open scene file for reading: '%s'.", full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    u64 file_size = 0;
    if (!filesystem_size(&f, &file_size)) {
        KERROR("Failed to check size of scene file.");
        return false;
    }

    u64 bytes_read = 0;
    char* file_content = kallocate(file_size + 1, MEMORY_TAG_RESOURCE);
    if (!filesystem_read_all_text(&f, file_content, &bytes_read)) {
        KERROR("Failed to read all text of scene file.");
        return false;
    }

    filesystem_close(&f);

    // Verify that we read the whole file.
    if (bytes_read != file_size) {
        KWARN("File size/bytes read mismatch: %llu / %llu", file_size, bytes_read);
    }

    // Parse the file.
    kson_tree source_tree = {0};
    if (!kson_tree_from_string(file_content, &source_tree)) {
        KERROR("Failed to parse scene file. See logs for details.");
        return false;
    }

    scene_config* resource_data = kallocate(sizeof(scene_config), MEMORY_TAG_RESOURCE);

    // Deserialize the scene.
    if (!scene_config_deserialize_kson(&source_tree, resource_data)) {
        KERROR("Failed to deserialize kson to scene config");
        kson_tree_cleanup(&source_tree);
        return false;
    }

    // Destroy the tree.
    kson_tree_cleanup(&source_tree);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(scene_config);

    return true;
}

static void scene_loader_unload(struct resource_loader* self, resource* resource) {
    scene_config* data = (scene_config*)resource->data;

    // TODO: properly destroy nodes, attachments, etc.

    if (data->name) {
        kfree(data->name, string_length(data->name) + 1, MEMORY_TAG_STRING);
    }

    if (data->description) {
        kfree(data->description, string_length(data->description) + 1, MEMORY_TAG_STRING);
    }

    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("scene_loader_unload called with nullptr for self or resource.");
    }
}

static b8 scene_loader_write(struct resource_loader* self, resource* r) {
    if (!self || !r) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, r->name, ".kss");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_WRITE, false, &f)) {
        KERROR("scene_loader_write - unable to open simple scene file for writing: '%s'.", full_file_path);
        return false;
    }

    scene_config* resource_data = r->data;
    if (resource_data) {
        // TODO: Send to kson parser to be written to string.
    }
    b8 result = true;

    if (!result) {
        KERROR("Failed to write scene file.");
    }
    return result;
}

resource_loader scene_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_scene;
    loader.custom_type = 0;
    loader.load = scene_loader_load;
    loader.unload = scene_loader_unload;
    loader.write = scene_loader_write;
    loader.type_path = "scenes";

    return loader;
}
