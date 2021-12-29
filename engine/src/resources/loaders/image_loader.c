#include "image_loader.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

// TODO: resource loader.
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

b8 image_loader_load(struct resource_loader* self, const char* name, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    const i32 required_channel_count = 4;
    stbi_set_flip_vertically_on_load(true);
    char full_file_path[512];

    // TODO: try different extensions
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".png");

    i32 width;
    i32 height;
    i32 channel_count;

    // For now, assume 8 bits per channel, 4 channels.
    // TODO: extend this to make it configurable.
    u8* data = stbi_load(
        full_file_path,
        &width,
        &height,
        &channel_count,
        required_channel_count);

    // Check for a failure reason. If there is one, abort, clear memory if allocated, return false.
    const char* fail_reason = stbi_failure_reason();
    if (fail_reason) {
        KERROR("Image resource loader failed to load file '%s': %s", full_file_path, fail_reason);
        // Clear the error so the next load doesn't fail.
        stbi__err(0, 0);

        if (data) {
            stbi_image_free(data);
        }
        return false;
    }

    if (!data) {
        KERROR("Image resource loader failed to load file '%s'.", full_file_path);
        return false;
    }

    // TODO: Should be using an allocator here.
    out_resource->full_path = string_duplicate(full_file_path);

    // TODO: Should be using an allocator here.
    image_resource_data* resource_data = kallocate(sizeof(image_resource_data), MEMORY_TAG_TEXTURE);
    resource_data->pixels = data;
    resource_data->width = width;
    resource_data->height = height;
    resource_data->channel_count = required_channel_count;

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(image_resource_data);
    out_resource->name = name;

    return true;
}

void image_loader_unload(struct resource_loader* self, resource* resource) {
    if (!self || !resource) {
        KWARN("image_loader_unload called with nullptr for self or resource.");
        return;
    }

    u32 path_length = string_length(resource->full_path);
    if (path_length) {
        kfree(resource->full_path, sizeof(char) * path_length + 1, MEMORY_TAG_STRING);
    }

    if (resource->data) {
        kfree(resource->data, resource->data_size, MEMORY_TAG_TEXTURE);
        resource->data = 0;
        resource->data_size = 0;
        resource->loader_id = INVALID_ID;
    }
}

resource_loader image_resource_loader_create() {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_IMAGE;
    loader.custom_type = 0;
    loader.load = image_loader_load;
    loader.unload = image_loader_unload;
    loader.type_path = "textures";

    return loader;
}