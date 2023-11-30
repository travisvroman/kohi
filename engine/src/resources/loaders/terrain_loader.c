#include "terrain_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "loader_utils.h"
#include "math/kmath.h"
#include "platform/filesystem.h"
#include "resources/resource_types.h"
#include "resources/terrain.h"
#include "systems/resource_system.h"

static b8 terrain_loader_load(struct resource_loader *self, const char *name,
                              void *params, resource *out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    // TODO: binary format
    char *format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(),
                  self->type_path, name, ".kterrain");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR(
            "terrain_loader_load - unable to open terrain file for reading: '%s'.",
            full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    terrain_config *resource_data =
        kallocate(sizeof(terrain_config), MEMORY_TAG_RESOURCE);
    // Set some defaults, create arrays.

    resource_data->material_count = 0;
    resource_data->material_names = kallocate(sizeof(char *) * TERRAIN_MAX_MATERIAL_COUNT, MEMORY_TAG_ARRAY);

    resource_data->name = 0;

    u32 version = 0;

    char *heightmap_file = 0;

    // Read each line of the file.
    char line_buf[512] = "";
    char *p = &line_buf[0];
    u64 line_length = 0;
    u32 line_number = 1;
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
            if (!string_to_u32(trimmed_value, &version)) {
                KWARN("Format error - invalid file version.");
            }
            // TODO: version
        } else if (strings_equali(trimmed_var_name, "name")) {
            resource_data->name = string_duplicate(trimmed_value);
        } else if (strings_equali(trimmed_var_name, "scale_x")) {
            if (!string_to_f32(trimmed_value, &resource_data->tile_scale_x)) {
                KWARN("Format error: failed to parse scale_x");
            }
        } else if (strings_equali(trimmed_var_name, "scale_y")) {
            if (!string_to_f32(trimmed_value, &resource_data->scale_y)) {
                KWARN("Format error: failed to parse scale_y");
            }
        } else if (strings_equali(trimmed_var_name, "scale_z")) {
            if (!string_to_f32(trimmed_value, &resource_data->tile_scale_z)) {
                KWARN("Format error: failed to parse scale_z");
            }
        } else if (strings_equali(trimmed_var_name, "heightmap_file")) {
            heightmap_file = string_duplicate(trimmed_value);
        } else if (strings_nequali(trimmed_var_name, "material", 8)) {
            u32 material_index = 0;
            if (!string_to_u32(trimmed_var_name + 8, &material_index)) {
                KWARN("Format error: Unable to parse material index.");
            }

            resource_data->material_names[material_index] = string_duplicate(trimmed_value);
            resource_data->material_count++;
        } else {
            // TODO: capture anything else
        }

        // Clear the line buffer.
        kzero_memory(line_buf, sizeof(char) * 512);
        line_number++;
    }

    filesystem_close(&f);

    // Load the heightmap if one is configured.
    if (heightmap_file) {
        image_resource_params params = {0};
        params.flip_y = false;

        resource heightmap_image_resource;
        if (!resource_system_load(heightmap_file, RESOURCE_TYPE_IMAGE, &params,
                                  &heightmap_image_resource)) {
            KERROR(
                "Unable to load heightmap file for terrain. Setting some "
                "reasonable defaults.");
            resource_data->tile_count_x = resource_data->tile_count_z = 100;
            resource_data->vertex_data_length = 100 * 100;
            resource_data->vertex_datas = darray_reserve(
                terrain_vertex_data, resource_data->vertex_data_length);
        }

        image_resource_data *image_data =
            (image_resource_data *)heightmap_image_resource.data;
        u32 pixel_count = image_data->width * image_data->height;
        resource_data->vertex_data_length = pixel_count;
        resource_data->vertex_datas =
            darray_reserve(terrain_vertex_data, resource_data->vertex_data_length);

        resource_data->tile_count_x = image_data->width;
        resource_data->tile_count_z = image_data->height;

        for (u32 i = 0; i < pixel_count; ++i) {
            u8 r = image_data->pixels[(i * 4) + 0];
            u8 g = image_data->pixels[(i * 4) + 1];
            u8 b = image_data->pixels[(i * 4) + 2];
            // Need to base height off combined RGB value.
            u32 colour_int = 0;
            rgbu_to_u32(r, g, b, &colour_int);
            f32 height = (f32)colour_int / 16777215;

            resource_data->vertex_datas[i].height = height;
        }

        resource_system_unload(&heightmap_image_resource);
    } else {
        // For now, heightmaps are the only way to import terrains.
        KWARN(
            "No heightmap was included, using reasonable defaults for terrain "
            "generation.");
        resource_data->tile_count_x = resource_data->tile_count_z = 100;
        resource_data->vertex_data_length = 100 * 100;
        resource_data->vertex_datas =
            darray_reserve(terrain_vertex_data, resource_data->vertex_data_length);
    }
    out_resource->data = resource_data;
    out_resource->data_size = sizeof(shader_config);

    return true;
}

static void terrain_loader_unload(struct resource_loader *self,
                                  resource *resource) {
    terrain_config *data = (terrain_config *)resource->data;

    darray_destroy(data->vertex_datas);
    if (data->name) {
        kfree(data->name, sizeof(char) * (string_length(data->name) + 1),
              MEMORY_TAG_STRING);
    }
    kzero_memory(data, sizeof(shader_config));

    if (data->material_names) {
        kfree(data->material_names, sizeof(char *) * TERRAIN_MAX_MATERIAL_COUNT, MEMORY_TAG_ARRAY);
        data->material_names = 0;
    }

    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("terrain_loader_unload called with nullptr for self or resource.");
    }
}

resource_loader terrain_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_TERRAIN;
    loader.custom_type = 0;
    loader.load = terrain_loader_load;
    loader.unload = terrain_loader_unload;
    loader.type_path = "terrains";

    return loader;
}
