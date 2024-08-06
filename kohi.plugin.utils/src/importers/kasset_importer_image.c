#include "kasset_importer_image.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/vfs.h>
#include <serializers/kasset_binary_image_serializer.h>

#define STB_IMAGE_IMPLEMENTATION
// Using our own filesystem.
#define STBI_NO_STDIO
#include "vendor/stb_image.h"

b8 kasset_importer_image_import(const struct kasset_importer* self, u64 data_size, const void* data, void* params, struct kasset* out_asset) {
    if (!self || !data_size || !data) {
        KERROR("kasset_importer_image_import requires valid pointers to self and data, as well as a nonzero data_size.");
        return false;
    }

    if (!params) {
        KERROR("kasset_importer_image_import requires parameters to be present.");
        return false;
    }

    kasset_image_import_options* options = (kasset_image_import_options*)params;
    kasset_image* typed_asset = (kasset_image*)out_asset;

    // Determine channel count.
    i32 required_channel_count = 0;
    u8 bits_per_channel = 0;
    switch (options->format) {
    case KASSET_IMAGE_FORMAT_RGBA8:
        required_channel_count = 4;
        bits_per_channel = 8;
        break;
    default:
        KWARN("Unrecognized image format requested - defaulting to 4 channels (RGBA)/8bpc");
        options->format = KASSET_IMAGE_FORMAT_RGBA8;
        required_channel_count = 4;
        bits_per_channel = 8;
        break;
    }

    u8* pixels = stbi_load_from_memory(data, data_size, (i32*)&typed_asset->width, (i32*)&typed_asset->height, (i32*)&typed_asset->channel_count, required_channel_count);
    if (!pixels) {
        KERROR("Image importer failed to import image '%s'.", out_asset->meta.source_file_path);
        return false;
    }

    u64 actual_size = (bits_per_channel / 8) * typed_asset->channel_count * typed_asset->width * typed_asset->height;
    typed_asset->pixel_array_size = actual_size;
    typed_asset->pixels = pixels;

    // NOTE: Querying is done below.
    /* i32 result = stbi_info_from_memory(data, data_size, (i32*)&typed_asset->width, (i32*)&typed_asset->height, (i32*)&typed_asset->channel_count);
    if (result == 0) {
        KERROR("Failed to query image data from memory.");
        goto image_loader_query_return;
    } */

    // The number of mip levels is calculated by first taking the largest dimension
    // (either width or height), figuring out how many times that number can be divided
    // by 2, taking the floor value (rounding down) and adding 1 to represent the
    // base level. This always leaves a value of at least 1.
    typed_asset->mip_levels = (u32)(kfloor(klog2(KMAX(typed_asset->width, typed_asset->height))) + 1);

    // Serialize and write to the VFS.
    struct vfs_state* vfs = engine_systems_get()->vfs_system_state;

    u64 serialized_block_size = 0;
    void* serialized_block = kasset_binary_image_serialize(out_asset, &serialized_block_size);
    if (!serialized_block) {
        KERROR("Binary image serialization failed, check logs.");
        return false;
    }

    b8 success = true;
    if (vfs_asset_write(vfs, out_asset, true, serialized_block_size, serialized_block)) {
        KERROR("Failed to write Binary Image asset data to VFS. See logs for details.");
        success = false;
    }

    if (serialized_block) {
        kfree(serialized_block, serialized_block_size, MEMORY_TAG_SERIALIZER);
    }

    return success;
}
