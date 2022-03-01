#include "shader_loader.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "math/kmath.h"
#include "loader_utils.h"
#include "containers/darray.h"

#include "platform/filesystem.h"

b8 shader_loader_load(struct resource_loader* self, const char* name, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".kmt");

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
    resource_data->stages = 0;
    resource_data->use_instances = false;
    resource_data->use_local = false;
    resource_data->renderpass_id = INVALID_ID_U8;
    resource_data->stage_count =0 ;
    resource_data->stage_names = darray_create(char*);
    resource_data->renderpass_name = 0;
    
    string_ncopy(resource_data->name, name, 255);

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
            string_ncopy(resource_data->name, trimmed_value, shader_NAME_MAX_LENGTH);
        } else if (strings_equali(trimmed_var_name, "diffuse_map_name")) {
            string_ncopy(resource_data->diffuse_map_name, trimmed_value, TEXTURE_NAME_MAX_LENGTH);
        } else if (strings_equali(trimmed_var_name, "diffuse_colour")) {
            // Parse the colour
            if (!string_to_vec4(trimmed_value, &resource_data->diffuse_colour)) {
                KWARN("Error parsing diffuse_colour in file '%s'. Using default of white instead.", full_file_path);
                // NOTE: already assigned above, no need to have it here.
            }
        } else if (strings_equali(trimmed_var_name, "type")) {
            // TODO: other shader types.
            if (strings_equali(trimmed_value, "ui")) {
                resource_data->type = shader_TYPE_UI;
            }
        }

        // TODO: more fields.

        // Clear the line buffer.
        kzero_memory(line_buf, sizeof(char) * 512);
        line_number++;
    }

    filesystem_close(&f);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(shader_config);
    out_resource->name = name;

    return true;
}

void shader_loader_unload(struct resource_loader* self, resource* resource) {
    if (!resource_unload(self, resource, MEMORY_TAG_shader_INSTANCE)) {
        KWARN("shader_loader_unload called with nullptr for self or resource.");
    }
}

resource_loader shader_resource_loader_create() {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_shader;
    loader.custom_type = 0;
    loader.load = shader_loader_load;
    loader.unload = shader_loader_unload;
    loader.type_path = "shaders";

    return loader;
}