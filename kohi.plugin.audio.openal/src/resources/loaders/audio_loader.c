
#include "audio_loader.h"

#include <audio/audio_types.h>
#include <platform/filesystem.h>
#include <resources/loaders/loader_utils.h>

#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "logger.h"
#include "defines.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

// Loading vorbis files.
#include "vendor/stb_vorbis.h"
// Loading mp3 files.
#define MINIMP3_IMPLEMENTATION
#include "vendor/minimp3_ex.h"

// MP3 decoder;
static mp3dec_t decoder;

typedef struct audio_file_internal {
    // The internal ogg vorbis file handle, if the file is ogg. Otherwise null.
    stb_vorbis* vorbis;
    // The internal mp3 file handle.
    mp3dec_file_info_t mp3_info;
    // Pulse-code modulation buffer, or raw data to be fed into a buffer.
    // Only used for some formats.
    i16* pcm;  // ALshort
    u64 pcm_size;
} audio_file_internal;

static u64 audio_file_load_samples(struct audio_file* audio, u32 chunk_size, i32 count) {
    if (audio->internal_data->vorbis) {
        i64 samples = stb_vorbis_get_samples_short_interleaved(audio->internal_data->vorbis, audio->channels, audio->internal_data->pcm, chunk_size);
        // Sample here does not include channels, so factor them in.
        return samples * audio->channels;
    } else if (audio->internal_data->mp3_info.buffer) {
        // samples count includes channels.
        return KMIN(audio->total_samples_left, chunk_size);
    }
    KERROR("Error loading samples: Unknown file type.");
    return INVALID_ID_U64;
}
static void* audio_file_stream_buffer_data(struct audio_file* audio) {
    if (audio->internal_data->vorbis) {
        return audio->internal_data->pcm;
    } else if (audio->internal_data->mp3_info.buffer) {
        u64 pos = audio->internal_data->mp3_info.samples - audio->total_samples_left;
        return audio->internal_data->mp3_info.buffer + pos;
    } else {
        KERROR("Error streaming audio dta: Unknown file type. Null is returned.")
        return 0;
    }
}
void audio_file_rewind(struct audio_file* audio) {
    if (audio) {
        if (audio->internal_data->vorbis) {
            stb_vorbis_seek_start(audio->internal_data->vorbis);
            // Reset sample counter.
            audio->total_samples_left = stb_vorbis_stream_length_in_samples(audio->internal_data->vorbis) * audio->channels;
        } else if (audio->internal_data->mp3_info.buffer) {
            // Reset sample counter.
            audio->total_samples_left = audio->internal_data->mp3_info.samples;
        } else {
            KERROR("Error rewinding audio file: unknown type.");
            return;
        }
    }
}

static b8 audio_loader_load(struct resource_loader* self, const char* name, void* params, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    audio_resource_loader_params* typed_params = params;

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format_unsafe(full_file_path, format_str, resource_system_base_path(), self->type_path, name, "");

    out_resource->full_path = string_duplicate(full_file_path);

    audio_file* resource_data = kallocate(sizeof(audio_file), MEMORY_TAG_RESOURCE);
    // Keep a pointer to this to free it later.
    resource_data->audio_resource = out_resource;
    resource_data->type = typed_params->type;
    resource_data->internal_data = kallocate(sizeof(audio_file_internal), MEMORY_TAG_RESOURCE);
    if (string_index_of_str(".ogg", full_file_path) != -1) {
        KTRACE("Processing OGG music file '%s'...", full_file_path);
        i32 ogg_error = 0;
        resource_data->internal_data->vorbis = stb_vorbis_open_filename(full_file_path, &ogg_error, 0);
        if (!resource_data->internal_data->vorbis) {
            enum STBVorbisError err = ogg_error;
            // TODO: error reporting.
            switch (err) {
                default:
                    break;
            }
            KERROR("Failed to load vorbis file with error: %u", ogg_error);
            return false;
        }
        stb_vorbis_info info = stb_vorbis_get_info(resource_data->internal_data->vorbis);
        resource_data->channels = info.channels;
        resource_data->sample_rate = info.sample_rate;
        // Samples including all channels.
        resource_data->total_samples_left = stb_vorbis_stream_length_in_samples(resource_data->internal_data->vorbis) * info.channels;

        if (resource_data->type == AUDIO_FILE_TYPE_MUSIC_STREAM) {
            // Need a buffer to extract sample data into.
            u64 buffer_length = typed_params->chunk_size * sizeof(i16);
            resource_data->internal_data->pcm = kallocate(buffer_length, MEMORY_TAG_AUDIO);
            resource_data->internal_data->pcm_size = buffer_length;
        } else {
            // Still need a buffer, but one to hold all the data. Stream it all in at once.

            // Samples including all channels.
            u64 length_samples = stb_vorbis_stream_length_in_samples(resource_data->internal_data->vorbis) * info.channels;

            // Load all the data into a buffer at once.
            u64 buffer_length = length_samples * sizeof(i16);
            // Since the whole thing is being read into a buffer at once, just use an inline array for the data.
            resource_data->internal_data->pcm = kallocate(buffer_length, MEMORY_TAG_AUDIO);
            resource_data->internal_data->pcm_size = buffer_length;
            i32 read_samples = stb_vorbis_get_samples_short_interleaved(resource_data->internal_data->vorbis, info.channels, resource_data->internal_data->pcm, length_samples);
            if (read_samples != (i64)length_samples) {
                KWARN("Read/length mismatch while reading ogg file. This might cause playback issues.");
            }
            // Make sure this is a multiple of 4. If not, loading into the buffer below can fail.
            length_samples += (length_samples % 4);
            resource_data->total_samples_left = length_samples;
        }

    } else if (string_index_of_str(".mp3", full_file_path) != -1) {
        KTRACE("Processing MP3 file '%s'...", full_file_path);

        mp3dec_load(&decoder, full_file_path, &resource_data->internal_data->mp3_info, 0, 0);
        mp3dec_file_info_t* info = &resource_data->internal_data->mp3_info;
        KDEBUG("mp3 freq: %dHz, avg kbit/s rate: %u", info->hz, info->avg_bitrate_kbps);
        resource_data->channels = info->channels;
        resource_data->sample_rate = info->hz;

        // Samples here include channels.
        resource_data->total_samples_left = info->samples;
    } else {
        KERROR("Unsupported audio file type.");
        return false;
    }

    // pfns
    resource_data->load_samples = audio_file_load_samples;
    resource_data->stream_buffer_data = audio_file_stream_buffer_data;
    resource_data->rewind = audio_file_rewind;

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(audio_file);
    out_resource->name = name;

    return true;
}

static void audio_loader_unload(struct resource_loader* self, resource* resource) {
    if (resource->data) {
        audio_file* resource_data = resource->data;

        if (resource_data->internal_data) {
            if (resource_data->internal_data->vorbis) {
                stb_vorbis_close(resource_data->internal_data->vorbis);
            } else if (resource_data->internal_data->mp3_info.buffer) {
                // TODO: dispose of mp3 data.
            }
            kfree(resource_data->internal_data, sizeof(audio_file_internal), MEMORY_TAG_RESOURCE);

            if (resource_data->internal_data->pcm) {
                kfree(resource_data->internal_data->pcm, resource_data->internal_data->pcm_size, MEMORY_TAG_AUDIO);
                resource_data->internal_data->pcm_size = 0;
            }
        }
    }

    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("binary_loader_unload called with nullptr for self or resource.");
    }
}

resource_loader audio_resource_loader_create(void) {
    // Initialize mp3 decoder
    mp3dec_init(&decoder);

    resource_loader loader;
    loader.type = RESOURCE_TYPE_AUDIO;
    loader.custom_type = 0;
    loader.load = audio_loader_load;
    loader.unload = audio_loader_unload;
    loader.type_path = "sounds";

    return loader;
}
