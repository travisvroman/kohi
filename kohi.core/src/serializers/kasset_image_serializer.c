#include "kasset_image_serializer.h"

#include "assets/kasset_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "utils/render_type_utils.h"

#define IMAGE_ASSET_CURRENT_VERSION 1

typedef struct binary_image_header {
    // The base binary asset header. Must always be the first member.
    binary_asset_header base;
    // The image format. cast to kpixel_format
    u32 format;
    // The image width in pixels.
    u32 width;
    // The image height in pixels.
    u32 height;
    // The number of mip levels for the asset.
    u8 mip_levels;
    // Padding used to keep the structure size 32-bit aligned.
    u8 padding[3];
} binary_image_header;

KAPI void* kasset_image_serialize(const kasset_image* asset, u64* out_size) {
    if (!asset) {
        KERROR("Cannot serialize without an asset, ya dingus!");
        return 0;
    }

    binary_image_header header = {0};
    // Base attributes.
    header.base.magic = ASSET_MAGIC;
    header.base.type = (u32)KASSET_TYPE_IMAGE;
    header.base.data_block_size = asset->pixel_array_size;
    // Always write the most current version.
    header.base.version = 1;

    header.height = asset->height;
    header.width = asset->width;
    header.mip_levels = asset->mip_levels;
    header.format = (u32)asset->format;

    *out_size = sizeof(binary_image_header) + asset->pixel_array_size;

    void* block = kallocate(*out_size, MEMORY_TAG_SERIALIZER);
    kcopy_memory(block, &header, sizeof(binary_image_header));
    kcopy_memory(((u8*)block) + sizeof(binary_image_header), asset->pixels, asset->pixel_array_size);

    return block;
}

KAPI b8 kasset_image_deserialize(u64 size, const void* block, kasset_image* out_asset) {
    if (!size || !block || !out_asset) {
        KERROR("Cannot deserialize image without a nonzero size, block of memory and an asset to write to.");
        return false;
    }

    const binary_image_header* header = block;
    if (header->base.magic != ASSET_MAGIC) {
        KERROR("Memory is not a Kohi binary asset.");
        return false;
    }

    kasset_type type = (kasset_type)header->base.type;
    if (type != KASSET_TYPE_IMAGE) {
        KERROR("Memory is not a Kohi image asset.");
        return false;
    }

    u64 expected_size = header->base.data_block_size + sizeof(binary_image_header);
    if (expected_size != size) {
        KERROR("Deserialization failure: Expected block size/block size mismatch: %llu/%llu.", expected_size, size);
        return false;
    }

    kasset_image* out_image = (kasset_image*)out_asset;

    out_image->height = header->height;
    out_image->width = header->width;
    out_image->mip_levels = header->mip_levels;
    out_image->format = header->format;
    // Default to RGBA8 if no format is included (legacy image format used 0 instead)
    if (header->format == 0) {
        out_image->format = KPIXEL_FORMAT_RGBA8;
    }
    out_image->pixel_array_size = header->base.data_block_size;
    u8 version = (u8)header->base.version;
    if (version > IMAGE_ASSET_CURRENT_VERSION) {
        KERROR("Invalid image asset version - version %u is higher than the current version, ya dingus!");
        return false;
    }
    out_image->channel_count = channel_count_from_pixel_format(out_image->format);

    // Copy the actual image data block.
    out_image->pixels = kallocate(out_image->pixel_array_size, MEMORY_TAG_ASSET);
    kcopy_memory(out_image->pixels, ((u8*)block) + sizeof(binary_image_header), header->base.data_block_size);

    return true;
}
