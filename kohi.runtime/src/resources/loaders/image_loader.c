#include "image_loader.h"

#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "logger.h"
#include "loader_utils.h"
#include "math/kmath.h"
#include "platform/filesystem.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

#define STB_IMAGE_IMPLEMENTATION
// Use our own filesystem.
#define STBI_NO_STDIO
#include "vendor/stb_image.h"

#define IMAGE_EXTENSION_COUNT 4
static char *supported_extensions[IMAGE_EXTENSION_COUNT] = {".tga", ".png", ".jpg", ".bmp"};

static b8 image_loader_load(struct resource_loader *self, const char *name,
                            void *params, resource *out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    image_resource_params *typed_params = (image_resource_params *)params;

    char *format_str = "%s/%s/%s%s";
    const i32 required_channel_count = 4;
    stbi_set_flip_vertically_on_load_thread(typed_params->flip_y);
    char full_file_path[512];

    // Try different extensions
    b8 found = false;
    for (u32 i = 0; i < IMAGE_EXTENSION_COUNT; ++i) {
        string_format_unsafe(full_file_path, format_str, resource_system_base_path(),
                      self->type_path, name, supported_extensions[i]);
        if (filesystem_exists(full_file_path)) {
            found = true;
            break;
        }
    }

    // Take a copy of the resource full path and name first.
    out_resource->full_path = string_duplicate(full_file_path);
    out_resource->name = name;

    if (!found) {
        KERROR(
            "Image resource loader failed find file '%s' or with any supported "
            "extension.",
            full_file_path);
        return false;
    }

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, true, &f)) {
        KERROR("Unable to read file: %s.", full_file_path);
        filesystem_close(&f);
        return false;
    }

    u64 file_size = 0;
    if (!filesystem_size(&f, &file_size)) {
        KERROR("Unable to get size of file: %s.", full_file_path);
        filesystem_close(&f);
        return false;
    }

    i32 width;
    i32 height;
    i32 channel_count;
    // The final result of all operations from here down.
    b8 final_result = false;

    u8 *raw_data = kallocate(file_size, MEMORY_TAG_TEXTURE);
    if (!raw_data) {
        KERROR("Unable to read file '%s'.", full_file_path);
        filesystem_close(&f);
        goto image_loader_load_return;
    }

    u64 bytes_read = 0;
    b8 read_result = filesystem_read_all_bytes(&f, raw_data, &bytes_read);
    filesystem_close(&f);

    if (!read_result) {
        KERROR("Unable to read file: '%s'", full_file_path);
        goto image_loader_load_return;
    }

    if (bytes_read != file_size) {
        KERROR("File size if %llu does not match expected: %llu", bytes_read, file_size);
        goto image_loader_load_return;
    }

    u8 *data = stbi_load_from_memory(raw_data, file_size, &width, &height,
                                     &channel_count, required_channel_count);
    if (!data) {
        KERROR("Image resource loader failed to load file '%s'.", full_file_path);
        goto image_loader_load_return;
    }

    image_resource_data *resource_data = kallocate(sizeof(image_resource_data), MEMORY_TAG_RESOURCE);
    resource_data->pixels = data;
    resource_data->width = width;
    resource_data->height = height;
    resource_data->channel_count = required_channel_count;
    // The number of mip levels is calculated by first taking the largest dimension
    // (either width or height), figuring out how many times that number can be divided
    // by 2, taking the floor value (rounding down) and adding 1 to represent the
    // base level. This always leaves a value of at least 1.
    resource_data->mip_levels = (u32)(kfloor(klog2(KMAX(width, height))) + 1);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(image_resource_data);
    final_result = true;

    // No matter the result, clean up and return.
image_loader_load_return:
    if (raw_data) {
        kfree(raw_data, file_size, MEMORY_TAG_TEXTURE);
    }
    return final_result;
}

static void image_loader_unload(struct resource_loader *self, resource *resource) {
    stbi_image_free(((image_resource_data *)resource->data)->pixels);
    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("image_loader_unload called with nullptr for self or resource.");
    }
}

b8 image_loader_query_properties(const char *image_name, i32 *out_width, i32 *out_height, i32 *out_channels, u32 *out_mip_levels) {
    // Query the resource system for the "base path by resource type" and pass image.
    const char *image_base_path = resource_system_base_path_for_type(RESOURCE_TYPE_IMAGE);
    if (!image_base_path) {
        KERROR("Unable to query image base path. Cannot query image properties as a result");
        return false;
    }

    char *format_str = "%s/%s%s";
    stbi_set_flip_vertically_on_load_thread(true);
    char full_file_path[512];

    // Try each supported extension to see if the image file exists.
    b8 found = false;
    for (u32 i = 0; i < IMAGE_EXTENSION_COUNT; ++i) {
        string_format_unsafe(full_file_path, format_str, image_base_path, image_name, supported_extensions[i]);
        if (filesystem_exists(full_file_path)) {
            found = true;
            break;
        }
    }
    string_free((char *)image_base_path);

    if (!found) {
        KERROR("Unable to find image named '%s'.", image_name);
        return false;
    }

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, true, &f)) {
        KERROR("Unable to read file: %s.", full_file_path);
        filesystem_close(&f);
        return false;
    }

    u64 file_size = 0;
    if (!filesystem_size(&f, &file_size)) {
        KERROR("Unable to get size of file: %s.", full_file_path);
        filesystem_close(&f);
        return false;
    }

    // The final result of all operations from here down.
    b8 final_result = true;

    u8 *raw_data = kallocate(file_size, MEMORY_TAG_TEXTURE);
    if (!raw_data) {
        KERROR("Unable to read file '%s'.", full_file_path);
        filesystem_close(&f);
        final_result = false;
        goto image_loader_query_return;
    }

    u64 bytes_read = 0;
    b8 read_result = filesystem_read_all_bytes(&f, raw_data, &bytes_read);
    filesystem_close(&f);

    if (!read_result) {
        KERROR("Unable to read file: '%s'", full_file_path);
        final_result = false;
        goto image_loader_query_return;
    }

    i32 result = stbi_info_from_memory(raw_data, bytes_read, out_width, out_height, out_channels);
    if (result == 0) {
        KERROR("Failed to query image data from memory.");
        final_result = false;
        goto image_loader_query_return;
    }

    // The number of mip levels is calculated by first taking the largest dimension
    // (either width or height), figuring out how many times that number can be divided
    // by 2, taking the floor value (rounding down) and adding 1 to represent the
    // base level. This always leaves a value of at least 1.
    *out_mip_levels = (u32)(kfloor(klog2(KMAX(*out_width, *out_height))) + 1);

    // No matter the result, clean up and return.
image_loader_query_return:
    if (raw_data) {
        kfree(raw_data, file_size, MEMORY_TAG_TEXTURE);
    }
    return final_result;
}

resource_loader image_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_IMAGE;
    loader.custom_type = 0;
    loader.load = image_loader_load;
    loader.unload = image_loader_unload;
    loader.type_path = "textures";  // FIXME: Shouldn't make assumptions about
                                    // this, should be passed as a param.

    return loader;
}
