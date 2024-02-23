#include "scene_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "platform/filesystem.h"
#include "resources/loaders/loader_utils.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

#define SHADOW_DISTANCE_DEFAULT 200.0f
#define SHADOW_FADE_DISTANCE_DEFAULT 25.0f
#define SHADOW_SPLIT_MULT_DEFAULT 0.95f;

static b8 scene_loader_load(struct resource_loader* self, const char* name, void* params, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".kss");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR("scene_loader_load - unable to open simple scene file for reading: '%s'.", full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    scene_config* resource_data = kallocate(sizeof(scene_config), MEMORY_TAG_RESOURCE);
    kzero_memory(resource_data, sizeof(scene_config));

    /*
    // Set some defaults, create arrays.
    resource_data->directional_light_config.shadow_fade_distance = SHADOW_FADE_DISTANCE_DEFAULT;
    resource_data->directional_light_config.shadow_distance = SHADOW_DISTANCE_DEFAULT;
    resource_data->directional_light_config.shadow_split_mult = SHADOW_SPLIT_MULT_DEFAULT;
    resource_data->description = 0;
    resource_data->name = string_duplicate(name);
    resource_data->point_lights = darray_create(point_light_scene_config);
    resource_data->meshes = darray_create(mesh_scene_config);
    resource_data->terrains = darray_create(terrain_scene_config);

    u32 version = 0;
    scene_parse_mode mode = scene_PARSE_MODE_ROOT;

    // Buffer objects that get populated when in corresponding mode, and pushed to list when
    // leaving said mode.
    point_light_scene_config current_point_light_config = {0};
    mesh_scene_config current_mesh_config = {0};
    terrain_scene_config current_terrain_config = {0};

    // Read each line of the file.
    char line_buf[512] = "";
    char* p = &line_buf[0];
    u64 line_length = 0;
    u32 line_number = 1;
    */

    // TODO: Pass to kson parser.

    filesystem_close(&f);

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
