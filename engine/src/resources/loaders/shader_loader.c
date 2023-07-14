#include "shader_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "loader_utils.h"
#include "math/kmath.h"
#include "platform/filesystem.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

static b8 shader_loader_load(struct resource_loader* self, const char* name, void* params, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".shadercfg");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR("shader_loader_load - unable to open shader file for reading: '%s'.", full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    shader_config* resource_data = kallocate(sizeof(shader_config), MEMORY_TAG_RESOURCE);
    // Set some defaults, create arrays.
    resource_data->attribute_count = 0;
    resource_data->attributes = darray_create(shader_attribute_config);
    resource_data->uniform_count = 0;
    resource_data->uniforms = darray_create(shader_uniform_config);
    resource_data->stage_count = 0;
    resource_data->stages = darray_create(shader_stage);
    resource_data->cull_mode = FACE_CULL_MODE_BACK;
    resource_data->stage_count = 0;
    resource_data->stage_names = darray_create(char*);
    resource_data->stage_filenames = darray_create(char*);

    resource_data->name = 0;

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
        if (strings_equali(trimmed_var_name, "version")) {
            // TODO: version
        } else if (strings_equali(trimmed_var_name, "name")) {
            resource_data->name = string_duplicate(trimmed_value);
        } else if (strings_equali(trimmed_var_name, "renderpass")) {
            // resource_data->renderpass_name = string_duplicate(trimmed_value);
            // Ignore this now.
        } else if (strings_equali(trimmed_var_name, "stages")) {
            // Parse the stages
            char** stage_names = darray_create(char*);
            u32 count = string_split(trimmed_value, ',', &stage_names, true, true);
            resource_data->stage_names = stage_names;
            // Ensure stage name and stage file name count are the same, as they should align.
            if (resource_data->stage_count == 0) {
                resource_data->stage_count = count;
            } else if (resource_data->stage_count != count) {
                KERROR("shader_loader_load: Invalid file layout. Count mismatch between stage names and stage filenames.");
            }
            // Parse each stage and add the right type to the array.
            for (u8 i = 0; i < resource_data->stage_count; ++i) {
                if (strings_equali(stage_names[i], "frag") || strings_equali(stage_names[i], "fragment")) {
                    darray_push(resource_data->stages, SHADER_STAGE_FRAGMENT);
                } else if (strings_equali(stage_names[i], "vert") || strings_equali(stage_names[i], "vertex")) {
                    darray_push(resource_data->stages, SHADER_STAGE_VERTEX);
                } else if (strings_equali(stage_names[i], "geom") || strings_equali(stage_names[i], "geometry")) {
                    darray_push(resource_data->stages, SHADER_STAGE_GEOMETRY);
                } else if (strings_equali(stage_names[i], "comp") || strings_equali(stage_names[i], "compute")) {
                    darray_push(resource_data->stages, SHADER_STAGE_COMPUTE);
                } else {
                    KERROR("shader_loader_load: Invalid file layout. Unrecognized stage '%s'", stage_names[i]);
                }
            }
        } else if (strings_equali(trimmed_var_name, "stagefiles")) {
            // Parse the stage file names
            resource_data->stage_filenames = darray_create(char*);
            u32 count = string_split(trimmed_value, ',', &resource_data->stage_filenames, true, true);
            // Ensure stage name and stage file name count are the same, as they should align.
            if (resource_data->stage_count == 0) {
                resource_data->stage_count = count;
            } else if (resource_data->stage_count != count) {
                KERROR("shader_loader_load: Invalid file layout. Count mismatch between stage names and stage filenames.");
            }
        } else if (strings_equali(trimmed_var_name, "cull_mode")) {
            if (strings_equali(trimmed_value, "front")) {
                resource_data->cull_mode = FACE_CULL_MODE_FRONT;
            } else if (strings_equali(trimmed_value, "front_and_back")) {
                resource_data->cull_mode = FACE_CULL_MODE_FRONT_AND_BACK;
            } else if (strings_equali(trimmed_value, "none")) {
                resource_data->cull_mode = FACE_CULL_MODE_NONE;
            }
            // Any other value will use the default of BACK.
        } else if (strings_equali(trimmed_var_name, "depth_test")) {
            string_to_bool(trimmed_value, &resource_data->depth_test);
        } else if (strings_equali(trimmed_var_name, "depth_write")) {
            string_to_bool(trimmed_value, &resource_data->depth_write);
        } else if (strings_equali(trimmed_var_name, "attribute")) {
            // Parse attribute.
            char** fields = darray_create(char*);
            u32 field_count = string_split(trimmed_value, ',', &fields, true, true);
            if (field_count != 2) {
                KERROR("shader_loader_load: Invalid file layout. Attribute fields must be 'type,name'. Skipping.");
            } else {
                shader_attribute_config attribute;
                // Parse field type
                if (strings_equali(fields[0], "f32")) {
                    attribute.type = SHADER_ATTRIB_TYPE_FLOAT32;
                    attribute.size = 4;
                } else if (strings_equali(fields[0], "vec2")) {
                    attribute.type = SHADER_ATTRIB_TYPE_FLOAT32_2;
                    attribute.size = 8;
                } else if (strings_equali(fields[0], "vec3")) {
                    attribute.type = SHADER_ATTRIB_TYPE_FLOAT32_3;
                    attribute.size = 12;
                } else if (strings_equali(fields[0], "vec4")) {
                    attribute.type = SHADER_ATTRIB_TYPE_FLOAT32_4;
                    attribute.size = 16;
                } else if (strings_equali(fields[0], "u8")) {
                    attribute.type = SHADER_ATTRIB_TYPE_UINT8;
                    attribute.size = 1;
                } else if (strings_equali(fields[0], "u16")) {
                    attribute.type = SHADER_ATTRIB_TYPE_UINT16;
                    attribute.size = 2;
                } else if (strings_equali(fields[0], "u32")) {
                    attribute.type = SHADER_ATTRIB_TYPE_UINT32;
                    attribute.size = 4;
                } else if (strings_equali(fields[0], "i8")) {
                    attribute.type = SHADER_ATTRIB_TYPE_INT8;
                    attribute.size = 1;
                } else if (strings_equali(fields[0], "i16")) {
                    attribute.type = SHADER_ATTRIB_TYPE_INT16;
                    attribute.size = 2;
                } else if (strings_equali(fields[0], "i32")) {
                    attribute.type = SHADER_ATTRIB_TYPE_INT32;
                    attribute.size = 4;
                } else {
                    KERROR("shader_loader_load: Invalid file layout. Attribute type must be f32, vec2, vec3, vec4, i8, i16, i32, u8, u16, or u32.");
                    KWARN("Defaulting to f32.");
                    attribute.type = SHADER_ATTRIB_TYPE_FLOAT32;
                    attribute.size = 4;
                }

                // Take a copy of the attribute name.
                attribute.name_length = string_length(fields[1]);
                attribute.name = string_duplicate(fields[1]);

                // Add the attribute.
                darray_push(resource_data->attributes, attribute);
                resource_data->attribute_count++;
            }

            string_cleanup_split_array(fields);
            darray_destroy(fields);
        } else if (strings_equali(trimmed_var_name, "uniform")) {
            // Parse uniform.
            char** fields = darray_create(char*);
            u32 field_count = string_split(trimmed_value, ',', &fields, true, true);
            if (field_count != 3) {
                KERROR("shader_loader_load: Invalid file layout. Uniform fields must be 'type,scope,name'. Skipping.");
            } else {
                shader_uniform_config uniform;
                // Parse field type
                if (strings_equali(fields[0], "f32")) {
                    uniform.type = SHADER_UNIFORM_TYPE_FLOAT32;
                    uniform.size = 4;
                } else if (strings_equali(fields[0], "vec2")) {
                    uniform.type = SHADER_UNIFORM_TYPE_FLOAT32_2;
                    uniform.size = 8;
                } else if (strings_equali(fields[0], "vec3")) {
                    uniform.type = SHADER_UNIFORM_TYPE_FLOAT32_3;
                    uniform.size = 12;
                } else if (strings_equali(fields[0], "vec4")) {
                    uniform.type = SHADER_UNIFORM_TYPE_FLOAT32_4;
                    uniform.size = 16;
                } else if (strings_equali(fields[0], "u8")) {
                    uniform.type = SHADER_UNIFORM_TYPE_UINT8;
                    uniform.size = 1;
                } else if (strings_equali(fields[0], "u16")) {
                    uniform.type = SHADER_UNIFORM_TYPE_UINT16;
                    uniform.size = 2;
                } else if (strings_equali(fields[0], "u32")) {
                    uniform.type = SHADER_UNIFORM_TYPE_UINT32;
                    uniform.size = 4;
                } else if (strings_equali(fields[0], "i8")) {
                    uniform.type = SHADER_UNIFORM_TYPE_INT8;
                    uniform.size = 1;
                } else if (strings_equali(fields[0], "i16")) {
                    uniform.type = SHADER_UNIFORM_TYPE_INT16;
                    uniform.size = 2;
                } else if (strings_equali(fields[0], "i32")) {
                    uniform.type = SHADER_UNIFORM_TYPE_INT32;
                    uniform.size = 4;
                } else if (strings_equali(fields[0], "mat4")) {
                    uniform.type = SHADER_UNIFORM_TYPE_MATRIX_4;
                    uniform.size = 64;
                } else if (strings_equali(fields[0], "samp") || strings_equali(fields[0], "sampler")) {
                    uniform.type = SHADER_UNIFORM_TYPE_SAMPLER;
                    uniform.size = 0;  // Samplers don't have a size.
                } else if (strings_nequali(fields[0], "struct", 6)) {
                    u32 len = string_length(fields[0]);
                    if (len <= 6) {
                        KERROR("shader_loader_load: Invalid struct uniform, size is missing. Shader load aborted.");
                        return false;
                    }
                    // u32 diff = len - 6;
                    char struct_size_str[32] = {0};
                    string_mid(struct_size_str, fields[0], 6, -1);
                    u32 struct_size = 0;
                    if (!string_to_u32(struct_size_str, &struct_size)) {
                        KERROR("Unable to parse struct uniform size. Shader load aborted.");
                        return false;
                    }
                    uniform.type = SHADER_UNIFORM_TYPE_CUSTOM;
                    uniform.size = struct_size;
                    // uniform=struct28,1,dir_light
                    // uniform=struct40,1,p_light_0
                    // uniform=struct40,1,p_light_1
                } else {
                    KERROR("shader_loader_load: Invalid file layout. Uniform type must be f32, vec2, vec3, vec4, i8, i16, i32, u8, u16, u32 or mat4.");
                    KWARN("Defaulting to f32.");
                    uniform.type = SHADER_UNIFORM_TYPE_FLOAT32;
                    uniform.size = 4;
                }

                // Parse the scope
                if (strings_equal(fields[1], "0")) {
                    uniform.scope = SHADER_SCOPE_GLOBAL;
                } else if (strings_equal(fields[1], "1")) {
                    uniform.scope = SHADER_SCOPE_INSTANCE;
                } else if (strings_equal(fields[1], "2")) {
                    uniform.scope = SHADER_SCOPE_LOCAL;
                } else {
                    KERROR("shader_loader_load: Invalid file layout: Uniform scope must be 0 for global, 1 for instance or 2 for local.");
                    KWARN("Defaulting to global.");
                    uniform.scope = SHADER_SCOPE_GLOBAL;
                }

                // Take a copy of the attribute name.
                uniform.name_length = string_length(fields[2]);
                uniform.name = string_duplicate(fields[2]);

                // Add the attribute.
                darray_push(resource_data->uniforms, uniform);
                resource_data->uniform_count++;
            }

            string_cleanup_split_array(fields);
            darray_destroy(fields);
        }

        // TODO: more fields.

        // Clear the line buffer.
        kzero_memory(line_buf, sizeof(char) * 512);
        line_number++;
    }

    filesystem_close(&f);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(shader_config);

    return true;
}

static void shader_loader_unload(struct resource_loader* self, resource* resource) {
    shader_config* data = (shader_config*)resource->data;

    string_cleanup_split_array(data->stage_filenames);
    darray_destroy(data->stage_filenames);

    string_cleanup_split_array(data->stage_names);
    darray_destroy(data->stage_names);

    darray_destroy(data->stages);

    // Clean up attributes.
    u32 count = darray_length(data->attributes);
    for (u32 i = 0; i < count; ++i) {
        u32 len = string_length(data->attributes[i].name);
        kfree(data->attributes[i].name, sizeof(char) * (len + 1), MEMORY_TAG_STRING);
    }
    darray_destroy(data->attributes);

    // Clean up uniforms.
    count = darray_length(data->uniforms);
    for (u32 i = 0; i < count; ++i) {
        u32 len = string_length(data->uniforms[i].name);
        kfree(data->uniforms[i].name, sizeof(char) * (len + 1), MEMORY_TAG_STRING);
    }
    darray_destroy(data->uniforms);

    kfree(data->name, sizeof(char) * (string_length(data->name) + 1), MEMORY_TAG_STRING);
    kzero_memory(data, sizeof(shader_config));

    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("shader_loader_unload called with nullptr for self or resource.");
    }
}

resource_loader shader_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_SHADER;
    loader.custom_type = 0;
    loader.load = shader_loader_load;
    loader.unload = shader_loader_unload;
    loader.type_path = "shaders";

    return loader;
}
