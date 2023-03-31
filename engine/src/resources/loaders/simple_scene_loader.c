#include "simple_scene_loader.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "loader_utils.h"
#include "containers/darray.h"

#include "platform/filesystem.h"

typedef enum simple_scene_parse_mode {
    SIMPLE_SCENE_PARSE_MODE_ROOT,
    SIMPLE_SCENE_PARSE_MODE_SCENE,
    SIMPLE_SCENE_PARSE_MODE_SKYBOX,
    SIMPLE_SCENE_PARSE_MODE_DIRECTIONAL_LIGHT,
    SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT,
    SIMPLE_SCENE_PARSE_MODE_MESH
} simple_scene_parse_mode;

static b8 try_change_mode(const char* value, simple_scene_parse_mode* current, simple_scene_parse_mode expected_current, simple_scene_parse_mode target);

b8 simple_scene_loader_load(struct resource_loader* self, const char* name, void* params, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".kss");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR("simple_scene_loader_load - unable to open simple scene file for reading: '%s'.", full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    simple_scene_config* resource_data = kallocate(sizeof(simple_scene_config), MEMORY_TAG_RESOURCE);
    kzero_memory(resource_data, sizeof(simple_scene_config));

    // Set some defaults, create arrays.
    resource_data->description = 0;
    resource_data->name = string_duplicate(name);
    resource_data->point_lights = darray_create(point_light_simple_scene_config);
    resource_data->meshes = darray_create(mesh_simple_scene_config);

    u32 version = 0;
    simple_scene_parse_mode mode = SIMPLE_SCENE_PARSE_MODE_ROOT;

    // Buffer objects that get populated when in corresponding mode, and pushed to list when
    // leaving said mode.
    point_light_simple_scene_config current_point_light_config = {0};
    mesh_simple_scene_config current_mesh_config = {0};

    // Read each line of the file.
    char line_buf[512] = "";
    char* p = &line_buf[0];
    u64 line_length = 0;
    u32 line_number = 1;
    while (filesystem_read_line(&f, 511, &p, &line_length)) {
        // Trim the string.
        char* trimmed = string_trim(line_buf);

        // Get the trimmed length.
        line_length = string_length(trimmed);

        // Skip blank lines and comments.
        if (line_length < 1 || trimmed[0] == '#') {
            line_number++;
            continue;
        }

        if (trimmed[0] == '[') {
            if (version == 0) {
                KERROR("Error loading simple scene file, !version was not set before attempting to change modes.");
                return false;
            }

            // Change modes
            if (strings_equali(trimmed, "[Scene]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_ROOT, SIMPLE_SCENE_PARSE_MODE_SCENE)) {
                    return false;
                }
            } else if (strings_equali(trimmed, "[/Scene]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_SCENE, SIMPLE_SCENE_PARSE_MODE_ROOT)) {
                    return false;
                }
            } else if (strings_equali(trimmed, "[Skybox]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_ROOT, SIMPLE_SCENE_PARSE_MODE_SKYBOX)) {
                    return false;
                }
            } else if (strings_equali(trimmed, "[/Skybox]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_SKYBOX, SIMPLE_SCENE_PARSE_MODE_ROOT)) {
                    return false;
                }
            } else if (strings_equali(trimmed, "[DirectionalLight]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_ROOT, SIMPLE_SCENE_PARSE_MODE_DIRECTIONAL_LIGHT)) {
                    return false;
                }
            } else if (strings_equali(trimmed, "[/DirectionalLight]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_DIRECTIONAL_LIGHT, SIMPLE_SCENE_PARSE_MODE_ROOT)) {
                    return false;
                }
            } else if (strings_equali(trimmed, "[PointLight]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_ROOT, SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT)) {
                    return false;
                }
                kzero_memory(&current_point_light_config, sizeof(point_light_simple_scene_config));
            } else if (strings_equali(trimmed, "[/PointLight]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT, SIMPLE_SCENE_PARSE_MODE_ROOT)) {
                    return false;
                }
                // Push into the array, then cleanup.
                darray_push(resource_data->point_lights, current_point_light_config);
                kzero_memory(&current_point_light_config, sizeof(point_light_simple_scene_config));
            } else if (strings_equali(trimmed, "[Mesh]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_ROOT, SIMPLE_SCENE_PARSE_MODE_MESH)) {
                    return false;
                }
                kzero_memory(&current_mesh_config, sizeof(mesh_simple_scene_config));
                // Also setup a default transform.
                current_mesh_config.transform = transform_create();
            } else if (strings_equali(trimmed, "[/Mesh]")) {
                if (!try_change_mode(trimmed, &mode, SIMPLE_SCENE_PARSE_MODE_MESH, SIMPLE_SCENE_PARSE_MODE_ROOT)) {
                    return false;
                }
                if (!current_mesh_config.name || !current_mesh_config.resource_name) {
                    KWARN("Format error: meshes require both name and resource name. Mesh not added.");
                    continue;
                }
                // Push into the array, then cleanup.
                darray_push(resource_data->meshes, current_mesh_config);
                kzero_memory(&current_mesh_config, sizeof(mesh_simple_scene_config));
            } else {
                KERROR("Error loading simple scene file: format error. Unexpected object type '%s'", trimmed);
                return false;
            }
        } else {
            // Split into var/value
            i32 equal_index = string_index_of(trimmed, '=');
            if (equal_index == -1) {
                KWARN("Potential formatting issue found in file '%s': '=' token not found. Skipping line %ui.", full_file_path, line_number);
                line_number++;
                continue;
            }

            // Assume a max of 64 characters for the variable name.
            char raw_var_name[64];
            kzero_memory(raw_var_name, sizeof(char) * 64);
            string_mid(raw_var_name, trimmed, 0, equal_index);
            char* trimmed_var_name = string_trim(raw_var_name);

            // Assume a max of 511-65 (446) for the max length of the value to account for the variable name and the '='.
            char raw_value[446];
            kzero_memory(raw_value, sizeof(char) * 446);
            string_mid(raw_value, trimmed, equal_index + 1, -1);  // Read the rest of the line
            char* trimmed_value = string_trim(raw_value);

            // Process the variable.
            if (strings_equali(trimmed_var_name, "!version")) {
                if (mode != SIMPLE_SCENE_PARSE_MODE_ROOT) {
                    KERROR("Attempting to set version inside of non-root mode.");
                    return false;
                }
                if (!string_to_u32(trimmed_value, &version)) {
                    KERROR("Invalid value for version: %s", trimmed_value);
                    // TODO: cleanup config
                    return false;
                }
            } else if (strings_equali(trimmed_var_name, "name")) {
                switch (mode) {
                    default:
                    case SIMPLE_SCENE_PARSE_MODE_ROOT:
                        KWARN("Format warning: Cannot process name in root node.");
                        break;
                    case SIMPLE_SCENE_PARSE_MODE_SCENE:
                        resource_data->name = string_duplicate(trimmed_value);
                        break;
                    case SIMPLE_SCENE_PARSE_MODE_DIRECTIONAL_LIGHT:
                        resource_data->directional_light_config.name = string_duplicate(trimmed_value);
                        break;
                    case SIMPLE_SCENE_PARSE_MODE_SKYBOX:
                        resource_data->skybox_config.name = string_duplicate(trimmed_value);
                        break;
                    case SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT: {
                        current_point_light_config.name = string_duplicate(trimmed_value);
                    } break;
                    case SIMPLE_SCENE_PARSE_MODE_MESH:
                        current_mesh_config.name = string_duplicate(trimmed_value);
                        break;
                }
            } else if (strings_equali(trimmed_var_name, "colour")) {
                switch (mode) {
                    default:
                        KWARN("Format warning: Cannot process name in the current node.");
                        break;
                    case SIMPLE_SCENE_PARSE_MODE_DIRECTIONAL_LIGHT:
                        if (!string_to_vec4(trimmed_value, &resource_data->directional_light_config.colour)) {
                            KWARN("Format error parsing colour. Default value used.");
                            resource_data->directional_light_config.colour = vec4_one();
                        }
                        break;
                    case SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT:
                        if (!string_to_vec4(trimmed_value, &current_point_light_config.colour)) {
                            KWARN("Format error parsing colour. Default value used.");
                            current_point_light_config.colour = vec4_one();
                        }
                        break;
                }
            } else if (strings_equali(trimmed_var_name, "description")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_SCENE) {
                    resource_data->description = string_duplicate(trimmed_value);
                } else {
                    KWARN("Format warning: Cannot process description in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "cubemap_name")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_SKYBOX) {
                    resource_data->skybox_config.cubemap_name = string_duplicate(trimmed_value);
                } else {
                    KWARN("Format warning: Cannot process cubemap_name in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "resource_name")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_MESH) {
                    current_mesh_config.resource_name = string_duplicate(trimmed_value);
                } else {
                    KWARN("Format warning: Cannot process resource_name in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "parent")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_MESH) {
                    current_mesh_config.parent_name = string_duplicate(trimmed_value);
                } else {
                    KWARN("Format warning: Cannot process resource_name in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "direction")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_DIRECTIONAL_LIGHT) {
                    if (!string_to_vec4(trimmed_value, &resource_data->directional_light_config.direction)) {
                        KWARN("Error parsing directional light direction as vec4. Using default value");
                        resource_data->directional_light_config.direction = (vec4){-0.57735f, -0.57735f, -0.57735f, 0.0f};
                    }
                } else {
                    KWARN("Format warning: Cannot process direction in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "position")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT) {
                    if (!string_to_vec4(trimmed_value, &current_point_light_config.position)) {
                        KWARN("Error parsing point light position as vec4. Using default value");
                        current_point_light_config.position = vec4_zero();
                    }
                } else {
                    KWARN("Format warning: Cannot process direction in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "transform")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_MESH) {
                    if (!string_to_transform(trimmed_value, &current_mesh_config.transform)) {
                        KWARN("Error parsing mesh transform. Using default value.");
                    }
                } else {
                    KWARN("Format warning: Cannot process transform in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "constant_f")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT) {
                    if (!string_to_f32(trimmed_value, &current_point_light_config.constant_f)) {
                        KWARN("Error parsing point light constant_f. Using default value.");
                        current_point_light_config.constant_f = 1.0f;
                    }
                } else {
                    KWARN("Format warning: Cannot process constant in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "linear")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT) {
                    if (!string_to_f32(trimmed_value, &current_point_light_config.linear)) {
                        KWARN("Error parsing point light linear. Using default value.");
                        current_point_light_config.linear = 0.35f;
                    }
                } else {
                    KWARN("Format warning: Cannot process linear in the current node.");
                }
            } else if (strings_equali(trimmed_var_name, "quadratic")) {
                if (mode == SIMPLE_SCENE_PARSE_MODE_POINT_LIGHT) {
                    if (!string_to_f32(trimmed_value, &current_point_light_config.quadratic)) {
                        KWARN("Error parsing point light quadratic. Using default value.");
                        current_point_light_config.quadratic = 0.44f;
                    }
                } else {
                    KWARN("Format warning: Cannot process quadratic in the current node.");
                }
            }
        }

        // TODO: more fields.

        // Clear the line buffer.
        kzero_memory(line_buf, sizeof(char) * 512);
        line_number++;
    }

    filesystem_close(&f);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(simple_scene_config);

    return true;
}

void simple_scene_loader_unload(struct resource_loader* self, resource* resource) {
    simple_scene_config* data = (simple_scene_config*)resource->data;

    if (data->meshes) {
        u32 length = darray_length(data->meshes);
        for (u32 i = 0; i < length; ++i) {
            if (data->meshes[i].name) {
                kfree(data->meshes[i].name, string_length(data->meshes[i].name) + 1, MEMORY_TAG_STRING);
            }
            if (data->meshes[i].parent_name) {
                kfree(data->meshes[i].parent_name, string_length(data->meshes[i].parent_name) + 1, MEMORY_TAG_STRING);
            }
            if (data->meshes[i].resource_name) {
                kfree(data->meshes[i].resource_name, string_length(data->meshes[i].resource_name) + 1, MEMORY_TAG_STRING);
            }
        }
        darray_destroy(data->meshes);
    }
    if (data->point_lights) {
        u32 length = darray_length(data->point_lights);
        for (u32 i = 0; i < length; ++i) {
            if (data->point_lights[i].name) {
                kfree(data->point_lights[i].name, string_length(data->point_lights[i].name) + 1, MEMORY_TAG_STRING);
            }
        }
        darray_destroy(data->point_lights);
    }

    if (data->directional_light_config.name) {
        kfree(data->directional_light_config.name, string_length(data->directional_light_config.name) + 1, MEMORY_TAG_STRING);
    }

    if (data->skybox_config.name) {
        kfree(data->skybox_config.name, string_length(data->skybox_config.name) + 1, MEMORY_TAG_STRING);
    }
    if (data->skybox_config.cubemap_name) {
        kfree(data->skybox_config.cubemap_name, string_length(data->skybox_config.cubemap_name) + 1, MEMORY_TAG_STRING);
    }

    if (data->name) {
        kfree(data->name, string_length(data->name) + 1, MEMORY_TAG_STRING);
    }

    if (data->description) {
        kfree(data->description, string_length(data->description) + 1, MEMORY_TAG_STRING);
    }

    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("simple_scene_loader_unload called with nullptr for self or resource.");
    }
}

resource_loader simple_scene_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_SIMPLE_SCENE;
    loader.custom_type = 0;
    loader.load = simple_scene_loader_load;
    loader.unload = simple_scene_loader_unload;
    loader.type_path = "scenes";

    return loader;
}

static b8 try_change_mode(const char* value, simple_scene_parse_mode* current, simple_scene_parse_mode expected_current, simple_scene_parse_mode target) {
    if (*current != expected_current) {
        KERROR("Error loading simple scene: format error. Unexpected token '%'", value);
        return false;
    } else {
        *current = target;
        return true;
    }
}