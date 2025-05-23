#include "kasset_importer_audio.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/vfs.h>
#include <serializers/kasset_audio_serializer.h>
#include <stdlib.h>

// Loading vorbis files.
#include "strings/kname.h"
#include "strings/kstring.h"
#include "vendor/stb_vorbis.h"
// Loading mp3 files.
#define MINIMP3_IMPLEMENTATION
// #define MINIMP3_NO_STDIO
#include "vendor/minimp3_ex.h"

b8 kasset_audio_import(kname output_asset_name, kname output_package_name, u64 data_size, const void* data, const char* extension) {
    if (!data_size || !data) {
        KERROR("%s requires a valid pointer to data, as well as a nonzero data_size.", __FUNCTION__);
        return false;
    }

    kasset_audio asset = {0};

    if (strings_equali(extension, ".mp3")) {
        // MP3 import
        KTRACE("Importing MP3 asset '%s'...", kname_string_get(output_asset_name));
        asset.pcm_data_size = 0;
        asset.pcm_data = 0;
        asset.total_sample_count = 0;

        // Initialize the decoder.
        mp3dec_t mp3_decoder;
        mp3dec_init(&mp3_decoder);
        mp3dec_file_info_t file_info;
        i32 err = mp3dec_load_buf(&mp3_decoder, (u8*)data, data_size, &file_info, 0, 0);
        if (err < 0) {
            KERROR("Error decoding MP3.");
            return false;
        }

        KINFO("Decoded %llu samples successfully.", file_info.samples);

        asset.total_sample_count = file_info.samples;
        asset.channels = file_info.channels;
        asset.sample_rate = file_info.hz;
        asset.pcm_data_size = file_info.samples * sizeof(mp3d_sample_t);
        asset.pcm_data = kallocate(asset.pcm_data_size, MEMORY_TAG_ASSET);
        KDEBUG("Decoded mp3 - channels: %d, samples: %llu, sample_rate/freq: %dHz, avg kbit/s rate: %d, size: %llu", file_info.channels, file_info.samples, file_info.hz, file_info.avg_bitrate_kbps, asset.pcm_data_size);
        kcopy_memory(asset.pcm_data, file_info.buffer, asset.pcm_data_size);

    } else if (strings_equali(extension, ".ogg")) {
        // Ogg import
        KTRACE("Importing OGG Vorbis asset '%s'...", kname_string_get(output_asset_name));

        i16* decoded_pcm_data = 0;
        i32 total_samples = stb_vorbis_decode_memory(data, data_size, &asset.channels, (i32*)&asset.sample_rate, &decoded_pcm_data);
        if (!decoded_pcm_data || total_samples < 0) {
            KERROR("Failed to import OGG Vorbis file.");
            return false;
        }
        // Make sure this is a multiple of 4. If not, loading into the buffer can fail.
        total_samples += (total_samples % 4);

        asset.total_sample_count = total_samples;
        asset.pcm_data_size = asset.total_sample_count * sizeof(i16);
        asset.pcm_data = kallocate(asset.pcm_data_size, MEMORY_TAG_ASSET);
        kcopy_memory(asset.pcm_data, decoded_pcm_data, asset.pcm_data_size);
        free(decoded_pcm_data);

    } else if (strings_equali(extension, ".wav")) {
        KTRACE("Importing WAV Vorbis asset '%s'...", kname_string_get(output_asset_name));
        // FIXME: support wav
        KFATAL("wav not yet supported.");
    } else {
        KFATAL("Unsupported audio source file format '%s', ya dingus.", extension);
    }

    // Serialize and write to the VFS.
    struct vfs_state* vfs = engine_systems_get()->vfs_system_state;

    u64 serialized_block_size = 0;
    void* serialized_block = kasset_audio_serialize(&asset, &serialized_block_size);
    if (!serialized_block) {
        KERROR("Binary audio serialization failed, check logs.");
        return false;
    }

    b8 success = true;
    if (!vfs_asset_write_binary(vfs, output_asset_name, output_package_name, serialized_block_size, serialized_block)) {
        KERROR("Failed to write Binary Audio asset data to VFS. See logs for details.");
        success = false;
    }

    if (serialized_block) {
        kfree(serialized_block, serialized_block_size, MEMORY_TAG_SERIALIZER);
    }

    return success;
}
