#include "material_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "loader_utils.h"
#include "math/kmath.h"
#include "platform/filesystem.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

typedef enum material_parse_mode {
    MATERIAL_PARSE_MODE_GLOBAL,
    MATERIAL_PARSE_MODE_MAP,
    MATERIAL_PARSE_MODE_PROPERTY
} material_parse_mode;

#define MATERIAL_PARSE_VERIFY_MODE(expected_mode, actual_mode, var_name, expected_mode_str)                                   \
    if (actual_mode != expected_mode) {                                                                                       \
        KERROR("Format error: unexpected variable '%s', should only exist inside a '%s' node.", var_name, expected_mode_str); \
        return false;                                                                                                         \
    }

static b8 material_parse_filter(const char *trimmed_value, const char *trimmed_var_name, material_parse_mode parse_mode, texture_filter *filter) {
    MATERIAL_PARSE_VERIFY_MODE(MATERIAL_PARSE_MODE_MAP, parse_mode, trimmed_var_name, "map");
    if (strings_equali(trimmed_value, "linear")) {
        *filter = TEXTURE_FILTER_MODE_LINEAR;
    } else if (strings_equali(trimmed_value, "nearest")) {
        *filter = TEXTURE_FILTER_MODE_NEAREST;
    } else {
        KERROR("Format error, unknown filter mode %s, defaulting to linear.", trimmed_value);
        *filter = TEXTURE_FILTER_MODE_LINEAR;
    }
    return true;
}

static b8 material_parse_repeat(const char *trimmed_value, const char *trimmed_var_name, material_parse_mode parse_mode, texture_repeat *repeat) {
    MATERIAL_PARSE_VERIFY_MODE(MATERIAL_PARSE_MODE_MAP, parse_mode, trimmed_var_name, "map");
    if (strings_equali(trimmed_value, "repeat")) {
        *repeat = TEXTURE_REPEAT_REPEAT;
    } else if (strings_equali(trimmed_value, "clamp_to_edge")) {
        *repeat = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    } else if (strings_equali(trimmed_value, "clamp_to_border")) {
        *repeat = TEXTURE_REPEAT_CLAMP_TO_BORDER;
    } else if (strings_equali(trimmed_value, "mirrored_repeat")) {
        *repeat = TEXTURE_REPEAT_MIRRORED_REPEAT;
    } else {
        KERROR("Format error, unknown repeat mode %s, defaulting to REPEAT.", trimmed_value);
        *repeat = TEXTURE_REPEAT_REPEAT;
    }
    return true;
}

static material_map material_map_create_default(const char *name,
                                                const char *texture_name) {
    material_map new_map = {};
    // Set reasonable defaults for old material types.
    new_map.name = string_duplicate(name);
    new_map.repeat_u = new_map.repeat_v = new_map.repeat_w =
        TEXTURE_REPEAT_REPEAT;
    new_map.filter_min = new_map.filter_mag = TEXTURE_FILTER_MODE_LINEAR;
    new_map.texture_name = string_duplicate(texture_name);
    return new_map;
}

static void material_prop_assign_value(material_config_prop *prop, const char *value) {
    switch (prop->type) {
        case SHADER_UNIFORM_TYPE_FLOAT32:
            string_to_f32(value, &prop->value_f32);
            break;
        case SHADER_UNIFORM_TYPE_FLOAT32_2:
            string_to_vec2(value, &prop->value_v2);
            break;
        case SHADER_UNIFORM_TYPE_FLOAT32_3:
            string_to_vec3(value, &prop->value_v3);
            break;
        case SHADER_UNIFORM_TYPE_FLOAT32_4:
            string_to_vec4(value, &prop->value_v4);
            break;
        case SHADER_UNIFORM_TYPE_INT8:
            string_to_i8(value, &prop->value_i8);
            break;
        case SHADER_UNIFORM_TYPE_UINT8:
            string_to_u8(value, &prop->value_u8);
            break;
        case SHADER_UNIFORM_TYPE_INT16:
            string_to_i16(value, &prop->value_i16);
            break;
        case SHADER_UNIFORM_TYPE_UINT16:
            string_to_u16(value, &prop->value_u16);
            break;
        case SHADER_UNIFORM_TYPE_INT32:
            string_to_i32(value, &prop->value_i32);
            break;
        case SHADER_UNIFORM_TYPE_UINT32:
            string_to_u32(value, &prop->value_u32);
            break;
        case SHADER_UNIFORM_TYPE_MATRIX_4:
            string_to_mat4(value, &prop->value_mat4);
            break;
        case SHADER_UNIFORM_TYPE_SAMPLER:
        case SHADER_UNIFORM_TYPE_CUSTOM:
        default:
            KERROR("Unsupported material property type.");
            break;
    }
}

static material_config_prop material_config_prop_create(const char *name, shader_uniform_type type, const char *value) {
    material_config_prop prop = {};
    prop.name = string_duplicate(name);
    prop.type = type;
    material_prop_assign_value(&prop, value);
    return prop;
}

static shader_uniform_type material_parse_prop_type(const char *strval) {
    if (strings_equali(strval, "f32") || strings_equali(strval, "vec1")) {
        return SHADER_UNIFORM_TYPE_FLOAT32;
    } else if (strings_equali(strval, "vec2")) {
        return SHADER_UNIFORM_TYPE_FLOAT32_2;
    } else if (strings_equali(strval, "vec3")) {
        return SHADER_UNIFORM_TYPE_FLOAT32_3;
    } else if (strings_equali(strval, "vec4")) {
        return SHADER_UNIFORM_TYPE_FLOAT32_4;
    } else if (strings_equali(strval, "i8")) {
        return SHADER_UNIFORM_TYPE_INT8;
    } else if (strings_equali(strval, "i16")) {
        return SHADER_UNIFORM_TYPE_INT16;
    } else if (strings_equali(strval, "i32")) {
        return SHADER_UNIFORM_TYPE_INT32;
    } else if (strings_equali(strval, "u8")) {
        return SHADER_UNIFORM_TYPE_UINT8;
    } else if (strings_equali(strval, "u16")) {
        return SHADER_UNIFORM_TYPE_UINT16;
    } else if (strings_equali(strval, "u32")) {
        return SHADER_UNIFORM_TYPE_UINT32;
    } else if (strings_equali(strval, "mat4")) {
        return SHADER_UNIFORM_TYPE_MATRIX_4;
    } else {
        KERROR("Unexpected type: '%s'. Defaulting to i32", strval);
        return SHADER_UNIFORM_TYPE_INT32;
    }
}

static b8 material_loader_load(struct resource_loader *self, const char *name,
                               void *params, resource *out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char *format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(),
                  self->type_path, name, ".kmt");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR("material_loader_load - unable to open material file for reading: '%s'.", full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    material_config *resource_data = kallocate(sizeof(material_config), MEMORY_TAG_RESOURCE);
    // Set some defaults.
    resource_data->shader_name = "Shader.Builtin.Material";  // Default material.
    resource_data->auto_release = true;
    resource_data->maps = darray_create(material_map);
    resource_data->properties = darray_create(material_config_prop);
    resource_data->name = string_duplicate(name);
    resource_data->type = MATERIAL_TYPE_UNKNOWN;
    // NOTE: Defaulting to version 1 since that version didn't require a "version" tag in the file to denote it.
    resource_data->version = 1;
    // Read each line of the file.
    char line_buf[512] = "";
    char *p = &line_buf[0];
    u64 line_length = 0;
    u32 line_number = 1;
    // Begin in global parse mode.
    material_parse_mode parse_mode = MATERIAL_PARSE_MODE_GLOBAL;
    material_map current_map = {0};
    material_config_prop current_prop = {0};
    while (filesystem_read_line(&f, 511, &p, &line_length)) {
        // Trim the string.
        char *trimmed = string_trim(line_buf);

        // Get the trimmed length.
        line_length = string_length(trimmed);

        // Skip blank lines and comments.
        if (line_length < 1 || trimmed[0] == '#') {
            line_number++;
            continue;
        }

        // Parse section tags
        if (trimmed[0] == '[') {
            // Section tag - determine if opening or closing.
            if (trimmed[1] == '/') {
                // Closing tag.
                switch (parse_mode) {
                    default:
                    case MATERIAL_PARSE_MODE_GLOBAL:
                        KERROR("Unexpected token '/' at line %d, Format error: closing tag found while in global scope.", line_number);
                        return false;
                    case MATERIAL_PARSE_MODE_MAP:
                        darray_push(resource_data->maps, current_map);
                        kzero_memory(&current_map, sizeof(material_map));
                        parse_mode = MATERIAL_PARSE_MODE_GLOBAL;
                        continue;
                    case MATERIAL_PARSE_MODE_PROPERTY:
                        darray_push(resource_data->properties, current_prop);
                        kzero_memory(&current_prop, sizeof(material_config_prop));
                        parse_mode = MATERIAL_PARSE_MODE_GLOBAL;
                        continue;
                }
            } else {
                // Opening tag
                if (parse_mode == MATERIAL_PARSE_MODE_GLOBAL) {
                    if (strings_nequali(trimmed, "[map]", 10)) {
                        parse_mode = MATERIAL_PARSE_MODE_MAP;
                    } else if (strings_nequali(trimmed, "[prop]", 10)) {
                        parse_mode = MATERIAL_PARSE_MODE_PROPERTY;
                    }
                    continue;
                } else {
                    KERROR("Format error: Unexpected opening tag %s at line %d.", trimmed, line_number);
                    return false;
                }
            }
        }
        // Split into var/value
        i32 equal_index = string_index_of(trimmed, '=');
        if (equal_index == -1) {
            KWARN(
                "Potential formatting issue found in file '%s': '=' token not "
                "found. Skipping line %ui.",
                full_file_path, line_number);
            line_number++;
            continue;
        }

        // Assume a max of 64 characters for the variable name.
        char raw_var_name[64];
        kzero_memory(raw_var_name, sizeof(char) * 64);
        string_mid(raw_var_name, trimmed, 0, equal_index);
        char *trimmed_var_name = string_trim(raw_var_name);

        // Assume a max of 511-65 (446) for the max length of the value to account
        // for the variable name and the '='.
        char raw_value[446];
        kzero_memory(raw_value, sizeof(char) * 446);
        string_mid(raw_value, trimmed, equal_index + 1,
                   -1);  // Read the rest of the line
        char *trimmed_value = string_trim(raw_value);

        // Process the variable.
        if (strings_equali(trimmed_var_name, "version")) {
            if (!string_to_u8(trimmed_value, &resource_data->version)) {
                KERROR("Format error: failed to parse version. Aborting.");
                return false;  // TODO: cleanup memory.
            }
        } else if (strings_equali(trimmed_var_name, "name")) {
            switch (parse_mode) {
                default:
                case MATERIAL_PARSE_MODE_GLOBAL:
                    if (resource_data->name) {
                        u32 len = string_length(resource_data->name);
                        kfree(resource_data->name, len + 1, MEMORY_TAG_STRING);
                        resource_data->name = 0;
                    }
                    resource_data->name = string_duplicate(trimmed_value);
                    // string_ncopy(resource_data->name, trimmed_value, MATERIAL_NAME_MAX_LENGTH);
                    break;
                case MATERIAL_PARSE_MODE_MAP:
                    current_map.name = string_duplicate(trimmed_value);
                    break;
                case MATERIAL_PARSE_MODE_PROPERTY:
                    current_prop.name = string_duplicate(trimmed_value);
                    break;
            }
        } else if (strings_equali(trimmed_var_name, "diffuse_map_name")) {
            if (resource_data->version == 1) {
                material_map new_map = material_map_create_default("diffuse", trimmed_value);
                darray_push(resource_data->maps, new_map);
            } else {
                KERROR(
                    "Format error: unexpected variable 'diffuse_map_name', this "
                    "should only exist for version 1 materials. Ignored.");
            }
        } else if (strings_equali(trimmed_var_name, "specular_map_name")) {
            if (resource_data->version == 1) {
                material_map new_map = material_map_create_default("specular", trimmed_value);
                darray_push(resource_data->maps, new_map);
            } else {
                KERROR(
                    "Format error: unexpected variable 'diffuse_map_name', this "
                    "should only exist for version 1 materials. Ignored.");
            }
        } else if (strings_equali(trimmed_var_name, "normal_map_name")) {
            if (resource_data->version == 1) {
                material_map new_map = material_map_create_default("normal", trimmed_value);
                darray_push(resource_data->maps, new_map);
            } else {
                KERROR(
                    "Format error: unexpected variable 'diffuse_map_name', this "
                    "should only exist for version 1 materials. Ignored.");
            }
        } else if (strings_equali(trimmed_var_name, "diffuse_colour")) {
            if (resource_data->version == 1) {
                material_config_prop new_prop = material_config_prop_create(
                    "diffuse_colour", SHADER_UNIFORM_TYPE_FLOAT32_4, trimmed_value);

                darray_push(resource_data->properties, new_prop);
            } else {
                KERROR(
                    "Format error: unexpected variable 'diffuse_colour', this "
                    "should only exist for version 1 materials. Ignored.");
            }
        } else if (strings_equali(trimmed_var_name, "shader")) {
            // Take a copy of the material name.
            resource_data->shader_name = string_duplicate(trimmed_value);
        } else if (strings_equali(trimmed_var_name, "shininess")) {
            if (resource_data->version == 1) {
                material_config_prop new_prop = material_config_prop_create(
                    "shininess", SHADER_UNIFORM_TYPE_FLOAT32, trimmed_value);
                darray_push(resource_data->properties, new_prop);
            } else {
                KERROR(
                    "Format error: unexpected variable 'shininess', this "
                    "should only exist for version 1 materials. Ignored.");
            }
        } else if (strings_equali(trimmed_var_name, "type")) {
            if (resource_data->version >= 2) {
                if (parse_mode == MATERIAL_PARSE_MODE_GLOBAL) {
                    if (strings_equali(trimmed_value, "phong")) {
                        resource_data->type = MATERIAL_TYPE_PHONG;
                    } else if (strings_equali(trimmed_value, "pbr")) {
                        resource_data->type = MATERIAL_TYPE_PBR;
                    } else if (strings_equali(trimmed_value, "ui")) {
                        resource_data->type = MATERIAL_TYPE_UI;
                    } else if (strings_equali(trimmed_value, "custom")) {
                        resource_data->type = MATERIAL_TYPE_CUSTOM;
                    } else {
                        KERROR("Format error: Unexpected material type '%s' (Material='%s')", trimmed_value, resource_data->name);
                    }
                } else if (parse_mode == MATERIAL_PARSE_MODE_PROPERTY) {
                    current_prop.type = material_parse_prop_type(trimmed_value);
                } else {
                    KERROR("Format error: Unexpected variable 'type' in mode.");
                }

            } else {
                KERROR(
                    "Format error: Unexpected variable 'type', this should only "
                    "exist for version 2+ materials.");
            }
        } else if (strings_equali(trimmed_var_name, "filter_min")) {
            if (!material_parse_filter(trimmed_value, trimmed_var_name, parse_mode, &current_map.filter_min)) {
                // NOTE: Handled gracefully with a default.
            }
        } else if (strings_equali(trimmed_var_name, "filter_mag")) {
            if (!material_parse_filter(trimmed_value, trimmed_var_name, parse_mode, &current_map.filter_mag)) {
                // NOTE: Handled gracefully with a default.
            }
        } else if (strings_equali(trimmed_var_name, "repeat_u")) {
            if (!material_parse_repeat(trimmed_value, trimmed_var_name, parse_mode, &current_map.repeat_u)) {
                // NOTE: Handled gracefully with a default.
            }
        } else if (strings_equali(trimmed_var_name, "repeat_v")) {
            if (!material_parse_repeat(trimmed_value, trimmed_var_name, parse_mode, &current_map.repeat_v)) {
                // NOTE: Handled gracefully with a default.
            }
        } else if (strings_equali(trimmed_var_name, "repeat_w")) {
            if (!material_parse_repeat(trimmed_value, trimmed_var_name, parse_mode, &current_map.repeat_w)) {
                // NOTE: Handled gracefully with a default.
            }
        } else if (strings_equali(trimmed_var_name, "texture_name")) {
            MATERIAL_PARSE_VERIFY_MODE(MATERIAL_PARSE_MODE_MAP, parse_mode, trimmed_var_name, "map");
            current_map.texture_name = string_duplicate(trimmed_value);
        } else if (strings_equali(trimmed_var_name, "value")) {
            MATERIAL_PARSE_VERIFY_MODE(MATERIAL_PARSE_MODE_PROPERTY, parse_mode, trimmed_var_name, "prop");
            material_prop_assign_value(&current_prop, trimmed_value);
        }

        // TODO: more fields.

        // Clear the line buffer.
        kzero_memory(line_buf, sizeof(char) * 512);
        line_number++;
    }

    // If version 1 and unknown material type, default to "phong"
    if (resource_data->version == 1 && resource_data->type == MATERIAL_TYPE_UNKNOWN) {
        resource_data->type = MATERIAL_TYPE_PHONG;
    }

    filesystem_close(&f);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(material_config);
    out_resource->name = name;

    return true;
}

static void material_loader_unload(struct resource_loader *self,
                                   resource *resource) {
    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("material_loader_unload called with nullptr for self or resource.");
    }
}

resource_loader material_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_MATERIAL;
    loader.custom_type = 0;
    loader.load = material_loader_load;
    loader.unload = material_loader_unload;
    loader.type_path = "materials";

    return loader;
}
