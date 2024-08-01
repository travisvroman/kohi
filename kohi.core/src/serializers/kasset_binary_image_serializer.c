#include "kasset_binary_image_serializer.h"

#include "assets/kasset_types.h"
#include "logger.h"
#include "memory/kmemory.h"

typedef struct binary_image_header {
    u32 magic;
    u32 version;
    u32 width;
    u32 height;
    u8 mip_levels;
    kasset_image_format format;
    u64 data_size;
} binary_image_header;

KAPI void* kasset_binary_image_serialize(const kasset_image* asset, u64* out_size) {
    if (!asset) {
        KERROR("Cannot serialize without an asset, ya dingus!");
        return 0;
    }

    binary_image_header header = {0};
    header.magic = ASSET_MAGIC;
    // Always write the most current version.
    header.version = 1;
    header.height = asset->height;
    header.width = asset->width;
    header.mip_levels = asset->mip_levels;
    header.format = asset->format;
    header.data_size = asset->base.data_size;

    *out_size = sizeof(binary_image_header) + asset->base.data_size;

    void* block = kallocate(*out_size, MEMORY_TAG_SERIALIZER);
    kcopy_memory(block, &header, sizeof(binary_image_header));
    kcopy_memory(((u8*)block) + sizeof(binary_image_header), asset->base.bytes, asset->base.data_size);

    return block;
}

KAPI b8 kasset_binary_image_deserialize(u64 size, void* block, kasset_image* out_image) {
    if (!size || !block || !out_image) {
        KERROR("Cannot deserialize without a nonzero size, block of memory and an image to write to.");
        return false;
    }

    binary_image_header* header = block;
    if (header->magic != ASSET_MAGIC) {
        KERROR("Memory is not a Kohi binary asset.");
        return false;
    }

    u64 expected_size = header->data_size + sizeof(binary_image_header);
    if (expected_size != size) {
        KERROR("Deserialization failure: Expected block size/block size mismatch: %llu/%llu.", expected_size, size);
        return false;
    }
    out_image->height = header->height;
    out_image->width = header->width;
    out_image->mip_levels = header->mip_levels;
    out_image->format = header->format;
    out_image->base.data_size = header->data_size;
    out_image->base.meta.version = header->version;

    // Copy the actual image data block.
    out_image->base.bytes = kallocate(header->data_size, MEMORY_TAG_ASSET);
    kcopy_memory(out_image->base.bytes, ((u8*)block) + sizeof(binary_image_header), header->data_size);

    return true;
}
