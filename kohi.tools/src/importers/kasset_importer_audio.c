#include "kasset_importer_audio.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <serializers/kasset_audio_serializer.h>
#include <stdlib.h>
#include <strings/kstring.h>

// Loading vorbis files.
#include "vendor/stb_vorbis.h"
// Loading mp3 files.
#define MINIMP3_IMPLEMENTATION
// #define MINIMP3_NO_STDIO
#include "vendor/minimp3_ex.h"

b8 kasset_audio_import(const char* source_path, const char* target_path) {
    if (!source_path || !target_path) {
        KERROR("%s requires valid source_path and target_path.", __FUNCTION__);
        return false;
    }

    const char* source_extension = string_extension_from_path(source_path, true);
    if (!source_extension) {
        return false;
    }

    b8 success = false;
    u64 serialized_block_size = 0;
    void* serialized_block = 0;

    u64 data_size = 0;
    const void* data = filesystem_read_entire_binary_file(source_path, &data_size);
    if (!data || !data_size) {
        KERROR("Error reading audio file (%s) for import.", source_path);
        goto kasset_importer_audio_cleanup;
    }

    kasset_audio asset = {0};

    if (strings_equali(source_extension, ".mp3")) {
        // MP3 import
        KTRACE("Importing asset '%s' as MP3...", source_path);
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
            goto kasset_importer_audio_cleanup;
        }

        KINFO("Decoded %llu samples successfully.", file_info.samples);

        asset.total_sample_count = file_info.samples;
        asset.channels = file_info.channels;
        asset.sample_rate = file_info.hz;
        asset.pcm_data_size = file_info.samples * sizeof(mp3d_sample_t);
        asset.pcm_data = kallocate(asset.pcm_data_size, MEMORY_TAG_ASSET);
        KDEBUG("Decoded mp3 - channels: %d, samples: %llu, sample_rate/freq: %dHz, avg kbit/s rate: %d, size: %llu", file_info.channels, file_info.samples, file_info.hz, file_info.avg_bitrate_kbps, asset.pcm_data_size);
        kcopy_memory(asset.pcm_data, file_info.buffer, asset.pcm_data_size);

    } else if (strings_equali(source_extension, ".ogg")) {
        // Ogg import
        KTRACE("Importing asset '%s' as OGG Vorbis...", source_path);

        i16* decoded_pcm_data = 0;
        i32 total_samples = stb_vorbis_decode_memory(data, data_size, &asset.channels, (i32*)&asset.sample_rate, &decoded_pcm_data);
        if (!decoded_pcm_data || total_samples < 0) {
            KERROR("Failed to import OGG Vorbis file.");
            goto kasset_importer_audio_cleanup;
        }
        // Make sure this is a multiple of 4. If not, loading into the buffer can fail.
        total_samples += (total_samples % 4);

        asset.total_sample_count = total_samples;
        asset.pcm_data_size = asset.total_sample_count * sizeof(i16);
        asset.pcm_data = kallocate(asset.pcm_data_size, MEMORY_TAG_ASSET);
        kcopy_memory(asset.pcm_data, decoded_pcm_data, asset.pcm_data_size);
        free(decoded_pcm_data);

    } else if (strings_equali(source_extension, ".wav")) {
        KTRACE("Importing asset '%s' as WAV...", source_path);
        // FIXME: support wav
        KFATAL("wav not yet supported.");
        goto kasset_importer_audio_cleanup;
    } else {
        KFATAL("Unsupported audio source file format '%s', ya dingus.", source_extension);
        goto kasset_importer_audio_cleanup;
    }

    serialized_block_size = 0;
    serialized_block = kasset_audio_serialize(&asset, &serialized_block_size);
    if (!serialized_block) {
        KERROR("Binary audio serialization failed, check logs.");
        goto kasset_importer_audio_cleanup;
    }

    // Write out .kaf file.
    if (!filesystem_write_entire_binary_file(target_path, serialized_block_size, serialized_block)) {
        KERROR("Failed to write Binary Audio asset (.kaf) file. See logs for details.");
        goto kasset_importer_audio_cleanup;
    }

    success = true;
kasset_importer_audio_cleanup:

    if (serialized_block) {
        kfree(serialized_block, serialized_block_size, MEMORY_TAG_SERIALIZER);
    }

    return success;
}
