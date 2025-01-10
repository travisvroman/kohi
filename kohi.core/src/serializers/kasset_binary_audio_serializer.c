#include "kasset_binary_audio_serializer.h"

#include "assets/kasset_types.h"
#include "assets/kasset_utils.h"
#include "logger.h"
#include "memory/kmemory.h"

typedef struct binary_audio_header {
    // The base binary asset header. Must always be the first member.
    binary_asset_header base;
    // The number of channels (i.e. 1 for mono or 2 for stereo)
    i32 channels;
    // The sample rate of the audio/music (i.e. 44100)
    u32 sample_rate;
    u32 total_sample_count;
    u64 pcm_data_size;
} binary_audio_header;

KAPI void* kasset_binary_audio_serialize(const kasset* asset, u64* out_size) {
    if (!asset) {
        KERROR("Cannot serialize without an asset, ya dingus!");
        return 0;
    }

    if (asset->type != KASSET_TYPE_AUDIO) {
        KERROR("Cannot serialize a non-audio asset using the audio serializer.");
        return 0;
    }

    kasset_audio* typed_asset = (kasset_audio*)asset;

    binary_audio_header header = {0};
    // Base attributes.
    header.base.magic = ASSET_MAGIC;
    header.base.type = (u32)asset->type;
    header.base.data_block_size = typed_asset->pcm_data_size;
    // Always write the most current version.
    header.base.version = 1;

    header.pcm_data_size = typed_asset->pcm_data_size;
    header.sample_rate = typed_asset->sample_rate;
    header.channels = typed_asset->channels;
    header.total_sample_count = typed_asset->total_sample_count;

    *out_size = sizeof(binary_audio_header) + typed_asset->pcm_data_size;

    void* block = kallocate(*out_size, MEMORY_TAG_SERIALIZER);
    kcopy_memory(block, &header, sizeof(binary_audio_header));
    kcopy_memory(((u8*)block) + sizeof(binary_audio_header), typed_asset->pcm_data, typed_asset->pcm_data_size);

    return block;
}

KAPI b8 kasset_binary_audio_deserialize(u64 size, const void* block, kasset* out_asset) {
    if (!size || !block || !out_asset) {
        KERROR("Cannot deserialize without a nonzero size, block of memory and an asset to write to.");
        return false;
    }

    const binary_audio_header* header = block;
    if (header->base.magic != ASSET_MAGIC) {
        KERROR("Memory is not a Kohi binary asset.");
        return false;
    }

    kasset_type type = (kasset_type)header->base.type;
    if (type != KASSET_TYPE_AUDIO) {
        KERROR("Memory is not a Kohi audio asset.");
        return false;
    }

    u64 expected_size = header->base.data_block_size + sizeof(binary_audio_header);
    if (expected_size != size) {
        KERROR("Deserialization failure: Expected block size/block size mismatch: %llu/%llu.", expected_size, size);
        return false;
    }

    kasset_audio* out_audio = (kasset_audio*)out_asset;

    out_audio->base.type = type;
    out_audio->base.meta.version = header->base.version;
    out_audio->channels = header->channels;
    out_audio->total_sample_count = header->total_sample_count;
    out_audio->sample_rate = header->sample_rate;
    out_audio->pcm_data_size = header->pcm_data_size;

    // Copy the actual audio data block.
    out_audio->pcm_data = kallocate(out_audio->pcm_data_size, MEMORY_TAG_ASSET);
    kcopy_memory(out_audio->pcm_data, ((u8*)block) + sizeof(binary_audio_header), header->base.data_block_size);

    return true;
}
