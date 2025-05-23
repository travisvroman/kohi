#include "kasset_importer_image.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <core_render_types.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <serializers/kasset_image_serializer.h>
#include <strings/kstring.h>
#include <utils/render_type_utils.h>

/* #define STB_IMAGE_IMPLEMENTATION
// Using our own filesystem.
#define STBI_NO_STDIO */
// NOTE: defined in tools_main.c
#include "vendor/stb_image.h"

b8 kasset_image_import(const char* output_directory, const char* output_filename, u64 data_size, const void* data, b8 flip_y, kpixel_format output_format) {
    if (!data_size || !data) {
        KERROR("%s requires valid pointer to data as well as a nonzero data_size.", __FUNCTION__);
        return false;
    }

    // Determine channel count.
    i32 required_channel_count = 0;
    u8 bits_per_channel = 0;
    switch (output_format) {
    case KPIXEL_FORMAT_RGBA8:
        required_channel_count = 4;
        bits_per_channel = 8;
        break;
    case KPIXEL_FORMAT_RGB8:
        required_channel_count = 3;
        bits_per_channel = 8;
        break;
    case KPIXEL_FORMAT_RG8:
        required_channel_count = 2;
        bits_per_channel = 8;
        break;
    case KPIXEL_FORMAT_R8:
        required_channel_count = 1;
        bits_per_channel = 8;
        break;

    case KPIXEL_FORMAT_RGBA16:
        required_channel_count = 4;
        bits_per_channel = 16;
        break;
    case KPIXEL_FORMAT_RGB16:
        required_channel_count = 3;
        bits_per_channel = 16;
        break;
    case KPIXEL_FORMAT_RG16:
        required_channel_count = 2;
        bits_per_channel = 16;
        break;
    case KPIXEL_FORMAT_R16:
        required_channel_count = 1;
        bits_per_channel = 16;
        break;

    case KPIXEL_FORMAT_RGBA32:
        required_channel_count = 4;
        bits_per_channel = 32;
        break;
    case KPIXEL_FORMAT_RGB32:
        required_channel_count = 3;
        bits_per_channel = 32;
        break;
    case KPIXEL_FORMAT_RG32:
        required_channel_count = 2;
        bits_per_channel = 32;
        break;
    case KPIXEL_FORMAT_R32:
        required_channel_count = 1;
        bits_per_channel = 32;
        break;
    default:
    case KPIXEL_FORMAT_UNKNOWN:
        KWARN("%s - Unrecognized image format requested - defaulting to 4 channels (RGBA)/8bpc", __FUNCTION__);
        output_format = KPIXEL_FORMAT_RGBA8;
        required_channel_count = 4;
        bits_per_channel = 8;
        break;
    }

    // Set the "flip" as described in the options.
    stbi_set_flip_vertically_on_load_thread(flip_y);

    // Load the image.
    kasset_image asset = {0};
    u8* pixels = stbi_load_from_memory(data, data_size, (i32*)&asset.width, (i32*)&asset.height, (i32*)&asset.channel_count, required_channel_count);
    if (!pixels) {
        KERROR("Image importer failed to import image.");
        return false;
    }

    asset.pixel_array_size = (bits_per_channel / 8) * asset.channel_count * asset.width * asset.height;
    asset.pixels = pixels;
    asset.mip_levels = calculate_mip_levels_from_dimension(asset.width, asset.height);

    // Serialize and write to file.
    u64 serialized_block_size = 0;
    void* serialized_block = kasset_image_serialize(&asset, &serialized_block_size);
    if (!serialized_block) {
        KERROR("Binary image serialization failed, check logs.");
        return false;
    }

    const char* out_path = string_format("%s/%s.%s", output_directory, output_filename, "kbi");
    b8 success = true;
    if (!filesystem_write_entire_binary_file(out_path, serialized_block_size, serialized_block)) {
        KERROR("Failed to write Binary Image asset data to disk. See logs for details.");
        success = false;
    }

    if (serialized_block) {
        kfree(serialized_block, serialized_block_size, MEMORY_TAG_SERIALIZER);
    }

    return success;
}

b8 kasset_image_import_from_path(const char* output_directory, const char* output_filename, const char* path, b8 flip_y, kpixel_format output_format) {
    u64 size = 0;
    const void* bytes = filesystem_read_entire_binary_file(path, &size);
    if (!bytes) {
        KERROR("%s - Failed to import image from path - see logs for details.", __FUNCTION__);
        return false;
    }

    b8 result = kasset_image_import(output_directory, output_filename, size, bytes, flip_y, output_format);
    if (size && bytes) {
        kfree((void*)bytes, size, MEMORY_TAG_ARRAY);
    }

    return result;
}
