#include "oal_plugin.h"

#include <alloca.h>
#include <math/kmath.h>
#include <platform/platform.h>

#include "audio/audio_types.h"
#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "systems/job_system.h"

// OpenAL
#include <AL/al.h>
#include <AL/alc.h>

// Loading vorbis files.
#include "../vendor/stb_vorbis.h"
// Loading mp3 files.
#define MINIMP3_IMPLEMENTATION
#include <vendor/minimp3_ex.h>

// Sources are used to play sounds, potentially at a space in 3D.
typedef struct audio_plugin_source {
    // Internal OpenAL source.
    ALCuint id;
    // Effectively the volume.
    f32 gain;
    // Pitch, generally left at 1.
    f32 pitch;
    // Position of the sound.
    vec3 position;
    // Indicates if the source is looping.
    b8 looping;
    // Indicates if this souce is in use.
    b8 in_use;
} audio_plugin_source;

// Internal audio file data. This is for sound effects.
typedef struct audio_file_internal {
    // The current buffer being used to play the file.
    ALuint buffer;
    // The format (i.e. 16 bit stereo)
    u32 format;
    // The number of channels (i.e. 1 for mono or 2 for stereo)
    i32 channels;
    // The sample rate of the sound/music (i.e. 44100)
    u32 sample_rate;
    // The internal ogg vorbis file handle, if the file is ogg. Otherwise null.
    stb_vorbis* vorbis;
    // The internal mp3 file handle.
    mp3dec_file_info_t mp3_info;
} audio_file_internal;

// The number of buffers used for streaming music file data.
#define OAL_PLUGIN_MUSIC_BUFFER_COUNT 2

// Internal audio file data. This is for music.
typedef struct music_file_internal {
    // The internal buffers used for streaming music file data.
    ALuint buffers[OAL_PLUGIN_MUSIC_BUFFER_COUNT];
    // The format (i.e. 16 bit stereo)
    u32 format;
    // The number of channels (i.e. 1 for mono or 2 for stereo)
    i32 channels;
    // The sample rate of the sound/music (i.e. 44100)
    u32 sample_rate;
    // Indicates if the music file should loop.
    b8 is_looping;

    // The internal ogg vorbis file handle, if the file is ogg. Otherwise null.
    stb_vorbis* vorbis;

    // The internal mp3 file handle.
    mp3dec_file_info_t mp3_info;

    // Pulse-code modulation buffer, or raw data to be fed into a buffer.
    // Only used for some formats.
    ALshort* pcm;
} music_file_internal;

// An entry for the sound queue, used when no sources are available for immediate playback.
typedef struct sound_queue_entry {
    struct audio_sound* sound;
    f32 volume;
    b8 loop;
} sound_queue_entry;

// An entry for the music queue, used when no sources are available for immediate playback.
typedef struct music_queue_entry {
    struct audio_music* music;
    f32 volume;
    b8 loop;
} music_queue_entry;

// An entry for the emitter queue, used when no sources are available for immediate playback.
typedef struct emitter_queue_entry {
    struct audio_emitter* emitter;
    f32 master_volume;
} emitter_queue_entry;

// The internal state for this audio plugin.
typedef struct audio_plugin_state {
    // A copy of the configuration.
    audio_plugin_config config;
    // The selected audio device.
    ALCdevice* device;
    // The current audio context.
    ALCcontext* context;
    // A pool of buffers to be used for all kinds of audio/music playback.
    ALuint* buffers;
    // The total number of buffers available.
    ALsizei buffer_count;

    // The listener's current position in the world.
    vec3 listener_position;
    // The listener's current forward vector.
    vec3 listener_forward;
    // The listener's current up vector.
    vec3 listener_up;

    // A collection of available sources. config.max_sources has the count of this.
    audio_plugin_source* sources;

    // An array to keep free/available buffer ids.
    u32* free_buffers;

    // Queues for when there are no sources currently available for playback. All are darrays.

    // Queued sounds. darray.
    sound_queue_entry* queued_sounds;
    // Queued music. darray.
    music_queue_entry* queued_music;
    // Queued emitters. darray.
    emitter_queue_entry* queued_emitters;

    // MP3 decoder;
    mp3dec_t decoder;

} audio_plugin_state;

static b8 oal_plugin_check_error();
static b8 oal_plugin_source_create(struct audio_plugin* plugin, audio_plugin_source* out_source);
static void oal_plugin_source_destroy(struct audio_plugin* plugin, audio_plugin_source* source);
static u32 oal_plugin_find_free_buffer(struct audio_plugin* plugin);

b8 oal_plugin_initialize(struct audio_plugin* plugin, audio_plugin_config config) {
    if (plugin) {
        plugin->internal_state = kallocate(sizeof(audio_plugin_state), MEMORY_TAG_AUDIO);

        plugin->internal_state->config = config;
        if (plugin->internal_state->config.max_sources < 1) {
            KWARN("Audio plugin config.max_sources was configured as 0. Defaulting to 8.");
            plugin->internal_state->config.max_sources = 8;
        }
        if (plugin->internal_state->config.max_buffers < 20) {
            KWARN("Audio plugin config.max_buffers was configured to be less than 20, the recommended minimum. Defaulting to 20.");
            plugin->internal_state->config.max_buffers = 256;
        }
        plugin->internal_state->buffer_count = plugin->internal_state->config.max_buffers;

        plugin->internal_state->free_buffers = darray_create(u32);

        // Make sure all buffers are marked as free.
        for (u32 i = 0; i < plugin->internal_state->buffer_count; ++i) {
            darray_push(plugin->internal_state->free_buffers, i + 1);  // buffer ids are 1-indexed.
        }

        // Get the default device. TODO: enumerate devices and pick via ALC_ENUMERATION_EXT?
        plugin->internal_state->device = alcOpenDevice(0);
        if (!plugin->internal_state->device) {
            KERROR("Unable to obtain OpenAL device. Plugin initialize failed.");
            return false;
        } else {
            KINFO("OpenAL Device acquired.");
        }

        // Get context and make it current.
        plugin->internal_state->context = alcCreateContext(plugin->internal_state->device, 0);
        if (!alcMakeContextCurrent(plugin->internal_state->context)) {
            oal_plugin_check_error();
        }

        // Configure the listener with some defaults.
        oal_plugin_listener_position_set(plugin, vec3_zero());
        oal_plugin_listener_orientation_set(plugin, vec3_forward(), vec3_up());

        // NOTE: zeroing out velocity - not sure if we ever need to bother setting this.
        alListener3f(AL_VELOCITY, 0, 0, 0);
        oal_plugin_check_error();

        plugin->internal_state->sources = kallocate(sizeof(audio_plugin_source) * config.max_sources, MEMORY_TAG_AUDIO);
        // Create all sources.
        for (u32 i = 0; i < config.max_sources; ++i) {
            if (!oal_plugin_source_create(plugin, &plugin->internal_state->sources[i])) {
                KERROR("Unable to create audio source in OpenAL plugin.");
                return false;
            }
        }

        // Buffers
        plugin->internal_state->buffers = kallocate(sizeof(u32) * plugin->internal_state->buffer_count, MEMORY_TAG_ARRAY);
        alGenBuffers(plugin->internal_state->buffer_count, plugin->internal_state->buffers);
        oal_plugin_check_error();

        // Create queues.
        plugin->internal_state->queued_sounds = darray_create(sound_queue_entry);
        plugin->internal_state->queued_music = darray_create(music_queue_entry);
        plugin->internal_state->queued_emitters = darray_create(emitter_queue_entry);

        // Initialize mp3 decoder
        mp3dec_init(&plugin->internal_state->decoder);

        // NOTE: source generation, which is basically a sound emitter.
        KINFO("OpenAL plugin intialized");

        return true;
    }

    KERROR("oal_plugin_initialize requires a valid pointer to a plugin.");
    return false;
}

void oal_plugin_shutdown(struct audio_plugin* plugin) {
    if (plugin) {
        if (plugin->internal_state) {
            // Destroy sources.
            for (u32 i = 0; i < plugin->internal_state->config.max_sources; ++i) {
                oal_plugin_source_destroy(plugin, &plugin->internal_state->sources[i]);
            }
            if (plugin->internal_state->device) {
                alcCloseDevice(plugin->internal_state->device);
                plugin->internal_state->device = 0;
            }
            kfree(plugin->internal_state, sizeof(audio_plugin_state), MEMORY_TAG_AUDIO);
            plugin->internal_state = 0;
        }

        kzero_memory(plugin, sizeof(audio_plugin));
    }
}

b8 source_available(struct audio_plugin* plugin) {
    if (!plugin) {
        return false;
    }

    for (u32 i = 0; i < plugin->internal_state->config.max_sources; ++i) {
        if (!plugin->internal_state->sources[i].in_use) {
            return true;
        }
    }

    return false;
}

b8 oal_plugin_update(struct audio_plugin* plugin, struct frame_data* p_frame_data) {
    if (!plugin) {
        return false;
    }

    // Evaluate queued sounds, music and emitters, in that order. A max of one each per frame will be started,
    // provided the sources are available.
    // Start with sounds.
    if (source_available(plugin)) {
        u32 queued_sound_count = darray_length(plugin->internal_state->queued_sounds);
        if (queued_sound_count > 0) {
            KDEBUG("Source is available to play queued sound. Attempting to play.");
            sound_queue_entry entry;
            darray_pop_at(plugin->internal_state->queued_sounds, 0, &entry);
            oal_plugin_play_sound_with_volume(plugin, entry.sound, entry.volume, entry.loop);
        }
    }

    // Music.
    if (source_available(plugin)) {
        u32 queued_music_count = darray_length(plugin->internal_state->queued_music);
        if (queued_music_count > 0) {
            KDEBUG("Source is available to play queued music. Attempting to play.");
            music_queue_entry entry;
            darray_pop_at(plugin->internal_state->queued_music, 0, &entry);
            oal_plugin_play_music_with_volume(plugin, entry.music, entry.volume, entry.loop);
        }
    }

    // Emitter.
    if (source_available(plugin)) {
        u32 queued_emitter_count = darray_length(plugin->internal_state->queued_emitters);
        if (queued_emitter_count > 0) {
            KDEBUG("Source is available to play queued emitter. Attempting to play.");
            emitter_queue_entry entry;
            darray_pop_at(plugin->internal_state->queued_emitters, 0, &entry);
            oal_plugin_play_emitter(plugin, entry.master_volume, entry.emitter);
        }
    }
    return true;
}

b8 oal_plugin_listener_position_query(struct audio_plugin* plugin, vec3* out_position) {
    if (!plugin || !out_position) {
        KERROR("oal_plugin_listener_position_query requires valid pointers to a plugin and out_position.");
        return false;
    }

    *out_position = plugin->internal_state->listener_position;
    return true;
}

b8 oal_plugin_listener_position_set(struct audio_plugin* plugin, vec3 position) {
    if (!plugin) {
        KERROR("oal_plugin_listener_position_set requires a valid pointer to a plugin.");
        return false;
    }

    plugin->internal_state->listener_position = position;
    alListener3f(AL_POSITION, position.x, position.y, position.z);
    oal_plugin_check_error();

    return true;
}

b8 oal_plugin_listener_orientation_query(struct audio_plugin* plugin, vec3* out_forward, vec3* out_up) {
    if (!plugin || !out_forward || !out_up) {
        KERROR("oal_plugin_listener_orientation_query requires valid pointers to a plugin, out_forward and out_up.");
        return false;
    }

    *out_forward = plugin->internal_state->listener_forward;
    *out_up = plugin->internal_state->listener_up;
    return true;
}

b8 oal_plugin_listener_orientation_set(struct audio_plugin* plugin, vec3 forward, vec3 up) {
    if (!plugin) {
        KERROR("oal_plugin_listener_orientation_set requires a valid pointer to a plugin.");
        return false;
    }

    plugin->internal_state->listener_forward = forward;
    plugin->internal_state->listener_up = up;
    ALfloat listener_orientation[] = {forward.x, forward.y, forward.z, up.x, up.y, up.z};
    alListenerfv(AL_ORIENTATION, listener_orientation);
    return oal_plugin_check_error();
}

static b8 source_set_defaults(struct audio_plugin* plugin, audio_plugin_source* source, b8 reset_use) {
    // Mark it as not in use.
    if (reset_use) {
        source->in_use = false;
    }

    // Set some defaults.
    if (!oal_plugin_source_gain_set(plugin, source->id, 1.0f)) {
        KERROR("Failed to set source default gain.");
        return false;
    }
    if (!oal_plugin_source_pitch_set(plugin, source->id, 1.0f)) {
        KERROR("Failed to set source default pitch.");
        return false;
    }
    if (!oal_plugin_source_position_set(plugin, source->id, vec3_zero())) {
        KERROR("Failed to set source default position.");
        return false;
    }
    if (!oal_plugin_source_looping_set(plugin, source->id, false)) {
        KERROR("Failed to set source default looping.");
        return false;
    }

    return true;
}

static b8 oal_plugin_source_create(struct audio_plugin* plugin, audio_plugin_source* out_source) {
    if (!plugin || !out_source) {
        KERROR("oal_plugin_source_create requires valid pointers to a plugin and out_source.");
        return false;
    }

    alGenSources((ALuint)1, &out_source->id);
    if (!oal_plugin_check_error()) {
        KERROR("Failed to create source.");
        return false;
    }

    if (!source_set_defaults(plugin, out_source, true)) {
        KERROR("Failed to set source defaults, and thus failed to create source.");
    }

    return true;
}

static void oal_plugin_source_destroy(struct audio_plugin* plugin, audio_plugin_source* source) {
    if (plugin && source) {
        alDeleteSources(1, &source->id);
        kzero_memory(source, sizeof(audio_plugin_source));
        source->id = INVALID_ID;
    }
}

static void oal_plugin_find_playing_sources(struct audio_plugin* plugin, u32 playing[], u32* count) {
    if (!plugin || !count) {
        return;
    }

    ALint state = 0;
    for (u32 i = 0; i < plugin->internal_state->config.max_sources; ++i) {
        alGetSourcei(plugin->internal_state->sources[i - 1].id, AL_SOURCE_STATE, &state);
        if (state == AL_PLAYING) {
            playing[(*count)] = plugin->internal_state->sources[i - 1].id;
            (*count)++;
        }
    }
}

static void clear_buffer(struct audio_plugin* plugin, u32* buf_ptr, u32 amount) {
    if (plugin) {
        for (u32 a = 0; a < amount; ++a) {
            for (u32 i = 0; i < plugin->internal_state->buffer_count; ++i) {
                if (buf_ptr[a] == plugin->internal_state->buffers[i]) {
                    darray_push(plugin->internal_state->free_buffers, i);
                    return;
                }
            }
        }
    }
    KWARN("Buffer could not be cleared.");
}

static u32 oal_plugin_find_free_buffer(struct audio_plugin* plugin) {
    if (plugin) {
        u32 free_count = darray_length(plugin->internal_state->free_buffers);

        // If there are no free buffers, attempt to free one first.
        if (free_count == 0) {
            KINFO("oal_plugin_find_free_buffer() - no free buffers, attempting to free an existing one.");
            if (!oal_plugin_check_error()) {
                return false;
            }

            u32 playing_source_count = 0;
            u32* playing_sources = kallocate(sizeof(u32) * plugin->internal_state->config.max_sources, MEMORY_TAG_ARRAY);
            oal_plugin_find_playing_sources(plugin, playing_sources, &playing_source_count);
            // Avoid a crash when calling alGetSourcei while checking for freeable buffers. Resumed below.
            for (u32 i = 0; i < playing_source_count; ++i) {
                alSourcePause(plugin->internal_state->sources[i - 1].id);
                oal_plugin_check_error();
            }

            ALint to_be_freed = 0;
            ALuint buffers_freed = 0;
            for (u32 i = 0; i < plugin->internal_state->buffer_count; ++i) {
                // Get number of buffers to be freed for this source.
                alGetSourcei(plugin->internal_state->sources[i - 1].id, AL_BUFFERS_PROCESSED, &to_be_freed);
                oal_plugin_check_error();
                if (to_be_freed > 0) {
                    // If there are buffers to be freed, free them.
                    oal_plugin_check_error();
                    alSourceUnqueueBuffers(plugin->internal_state->sources[i - 1].id, to_be_freed, &buffers_freed);
                    oal_plugin_check_error();

                    clear_buffer(plugin, &buffers_freed, to_be_freed);
                    alSourcePlay(plugin->internal_state->sources[i - 1].id);
                }
            }

            // Resume the paused sources.
            for (u32 i = 0; i < playing_source_count; ++i) {
                alSourcePlay(plugin->internal_state->sources[i - 1].id);
                oal_plugin_check_error();
            }
            kfree(playing_sources, sizeof(u32) * plugin->internal_state->config.max_sources, MEMORY_TAG_ARRAY);
        }

        // Check free count again, this time there must be at least one or there is an error condition.
        free_count = darray_length(plugin->internal_state->free_buffers);
        if (free_count < 1) {
            KERROR("Could not find or clear a buffer. This means too many things are being played at once.");
            return INVALID_ID;
        }

        // Nab the first one off the top
        u32 out_buffer_id;
        darray_pop_at(plugin->internal_state->free_buffers, 0, &out_buffer_id);

        free_count = darray_length(plugin->internal_state->free_buffers);
        KTRACE("Found free buffer id %u", out_buffer_id);
        KDEBUG("There are now %u free buffers remaining.", free_count);
        return out_buffer_id;
    }

    return INVALID_ID;
}

struct audio_plugin_source* oal_plugin_find_free_source(struct audio_plugin* plugin) {
    if (!plugin) {
        KERROR("Valid pointers to plugin is required.");
        return false;
    }

    for (u32 i = 0; i < plugin->internal_state->config.max_sources; ++i) {
        if (!plugin->internal_state->sources[i].in_use) {
            KDEBUG("Source index %u is free.", i);
        }
    }

    // NOTE: Querying the state isn't reliable in situations where sounds are being rapid-fired.
    // Therefore a flag must be maintained and checked for this instead of querying the source itself.
    for (u32 i = 0; i < plugin->internal_state->config.max_sources; ++i) {
        if (!plugin->internal_state->sources[i].in_use) {
            KDEBUG("Selected source index %u.", i);
            plugin->internal_state->sources[i].in_use = true;
            return &plugin->internal_state->sources[i];
        }
    }

    return 0;
}

b8 oal_plugin_source_reset(struct audio_plugin* plugin, audio_plugin_source* source, b8 reset_use) {
    if (!plugin || !source) {
        return false;
    }
    // Stop, if playing.
    ALint state;
    alGetSourcei(source->id, AL_SOURCE_STATE, &state);
    oal_plugin_check_error();
    if (state == AL_PLAYING) {
        alSourceStop(source->id);
        oal_plugin_check_error();
    }

    // Detach all buffers.
    alSourcei(source->id, AL_BUFFER, 0);
    oal_plugin_check_error();

    // Clear any queued buffers.
    ALint queued_buffer_count;
    alGetSourcei(source->id, AL_BUFFERS_QUEUED, &queued_buffer_count);
    if (queued_buffer_count > 0) {
        KTRACE("Clearing %u queued buffers.", queued_buffer_count);
        ALuint* unqueued_buffers = alloca(sizeof(ALuint) * queued_buffer_count);
        alSourceUnqueueBuffers(source->id, queued_buffer_count, unqueued_buffers);
        oal_plugin_check_error();
    }

    // Clear any processed buffers.
    ALint processed_buffer_count;
    alGetSourcei(source->id, AL_BUFFERS_PROCESSED, &processed_buffer_count);
    if (processed_buffer_count > 0) {
        KTRACE("Clearing %u processed buffers.", processed_buffer_count);
        ALuint* unqueued_buffers = alloca(sizeof(ALuint) * queued_buffer_count);
        alSourceUnqueueBuffers(source->id, processed_buffer_count, unqueued_buffers);
        oal_plugin_check_error();
    }

    alSourceRewind(source->id);
    oal_plugin_check_error();

    KDEBUG("Resetting source index: %u.", source->id - 1);  // Account for 1-indexed source ids.

    if (!source_set_defaults(plugin, source, reset_use)) {
        KERROR("Failed to set source defaults, and thus failed to reset source.");
    }
    return true;
}

b8 oal_plugin_source_gain_query(struct audio_plugin* plugin, u32 source_id, f32* out_gain) {
    if (plugin && out_gain && source_id <= plugin->internal_state->config.max_sources) {
        *out_gain = plugin->internal_state->sources[source_id - 1].gain;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_gain_set(struct audio_plugin* plugin, u32 source_id, f32 gain) {
    if (plugin && source_id <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_id - 1];
        source->gain = gain;
        alSourcef(source->id, AL_GAIN, gain);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_pitch_query(struct audio_plugin* plugin, u32 source_id, f32* out_pitch) {
    if (plugin && out_pitch && source_id <= plugin->internal_state->config.max_sources) {
        *out_pitch = plugin->internal_state->sources[source_id - 1].pitch;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_pitch_set(struct audio_plugin* plugin, u32 source_id, f32 pitch) {
    if (plugin && source_id <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_id - 1];
        source->pitch = pitch;
        alSourcef(source->id, AL_PITCH, pitch);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_position_query(struct audio_plugin* plugin, u32 source_id, vec3* out_position) {
    if (plugin && out_position && source_id <= plugin->internal_state->config.max_sources) {
        *out_position = plugin->internal_state->sources[source_id - 1].position;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_position_set(struct audio_plugin* plugin, u32 source_id, vec3 position) {
    if (plugin && source_id <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_id - 1];
        source->position = position;
        alSource3f(source->id, AL_POSITION, position.x, position.y, position.z);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_looping_query(struct audio_plugin* plugin, u32 source_id, b8* out_looping) {
    if (plugin && out_looping && source_id <= plugin->internal_state->config.max_sources) {
        *out_looping = plugin->internal_state->sources[source_id - 1].looping;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

b8 oal_plugin_source_looping_set(struct audio_plugin* plugin, u32 source_id, b8 looping) {
    if (plugin && source_id <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_id - 1];
        source->looping = looping;
        alSourcei(source->id, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_id);
    return false;
}

static const char* oal_plugin_error_str(ALCenum err) {
    switch (err) {
        case AL_INVALID_VALUE:
            return "AL_INVALID_VALUE";
        case AL_INVALID_NAME:
            return "AL_INVALID_NAME or ALC_INVALID_DEVICE";
        case AL_INVALID_OPERATION:
            return "AL_INVALID_OPERATION";
        case AL_NO_ERROR:
            return "AL_NO_ERROR";
        case AL_OUT_OF_MEMORY:
            return "AL_OUT_OF_MEMORY or could not find audio device";
        default:
            return "Unknown/unhandled error";
    }
}

static b8 oal_plugin_check_error() {
    ALCenum error = alGetError();
    if (error != AL_NO_ERROR) {
        KERROR("OpenAL error %u: '%s'", error, oal_plugin_error_str(error));
        return false;
    }
    return true;
}

// HACK: This should be in a file loader and streamed, but the file system doesn't yet support this...

static b8 oal_plugin_stream_music_data(audio_plugin* plugin, ALuint buffer, music_file* file) {
    if (!plugin || !file) {
        return false;
    }

    // Figure out how many samples can be taken.
    u64 size = 0;
    if (file->internal_data->vorbis) {
        i64 samples = stb_vorbis_get_samples_short_interleaved(file->internal_data->vorbis, file->internal_data->channels, file->internal_data->pcm + size, plugin->internal_state->config.chunk_size - size);
        // Sample here does not include channels, so factor them in.
        size = samples * file->internal_data->channels;
    } else if (file->internal_data->mp3_info.buffer) {
        // samples count includes channels.
        size = KMIN(file->total_samples_left, plugin->internal_state->config.chunk_size);
    }

    // 0 means the end of the file has been reached, and either the stream stops or needs to start over.
    if (size == 0) {
        return false;
    }

    // Load the data into the buffer.
    if (file->internal_data->vorbis) {
        alBufferData(buffer, file->internal_data->format, file->internal_data->pcm, size * sizeof(ALshort), file->internal_data->sample_rate);
        oal_plugin_check_error();
    } else if (file->internal_data->mp3_info.buffer) {
        u64 pos = file->internal_data->mp3_info.samples - file->total_samples_left;
        alBufferData(buffer, file->internal_data->format, file->internal_data->mp3_info.buffer + pos, size * sizeof(ALshort), file->internal_data->sample_rate);
        oal_plugin_check_error();
    }

    // Update the samples remaining.
    file->total_samples_left -= size;

    return true;
}

static b8 oal_plugin_stream_update(audio_plugin* plugin, music_file* file, audio_plugin_source* source) {
    if (!plugin || !file) {
        return false;
    }

    // It's possible sometimes for this to not be playing, even with buffers queued up.
    // Make sure to handle this case.
    ALint source_state;
    alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
    if (source_state != AL_PLAYING) {
        alSourcePlay(source->id);
    }

    // Check for processed buffers that can be popped off.
    ALint processed_buffer_count = 0;
    alGetSourcei(source->id, AL_BUFFERS_PROCESSED, &processed_buffer_count);

    while (processed_buffer_count--) {
        ALuint buffer_id = 0;
        alSourceUnqueueBuffers(source->id, 1, &buffer_id);

        // If this returns false, there was nothing further to read (i.e at the end of the file).
        if (!oal_plugin_stream_music_data(plugin, buffer_id, file)) {
            b8 done = true;

            // If set to loop, start over at the beginning.
            if (file->internal_data->is_looping) {
                if (file->internal_data->vorbis) {
                    // Loop around.
                    stb_vorbis_seek_start(file->internal_data->vorbis);

                    // Reset sample counter.
                    file->total_samples_left = stb_vorbis_stream_length_in_samples(file->internal_data->vorbis) * file->internal_data->channels;
                } else if (file->internal_data->mp3_info.samples) {
                    // Reset sample counter.
                    file->total_samples_left = file->internal_data->mp3_info.samples;
                }
                done = !oal_plugin_stream_music_data(plugin, buffer_id, file);
            }

            // If not set to loop, the sound is done playing.
            if (done) {
                return false;
            }
        }
        // Queue up the next buffer.
        alSourceQueueBuffers(source->id, 1, &buffer_id);
    }

    return true;
}

static b8 oal_plugin_open_music_file(audio_plugin* plugin, const char* path, music_file* out_file) {
    if (!plugin || !path || !out_file) {
        KERROR("oal_plugin_open_music_file requires valid pointers to plugin, path and out_file");
        return false;
    }

    kzero_memory(out_file, sizeof(music_file));

    // Internal state.
    out_file->internal_data = kallocate(sizeof(music_file_internal), MEMORY_TAG_AUDIO);
    // Get some buffers to be used back to back.
    for (u32 i = 0; i < OAL_PLUGIN_MUSIC_BUFFER_COUNT; ++i) {
        out_file->internal_data->buffers[i] = oal_plugin_find_free_buffer(plugin);
        if (out_file->internal_data->buffers[i] == INVALID_ID) {
            KERROR("Unable to open music file due to no buffers being available.");
            return false;
        }
    }

    oal_plugin_check_error();
    if (string_index_of_str(".ogg", path) != -1) {
        KTRACE("Processing OGG music file '%s'...", path);

        i32 ogg_error = 0;
        // TODO: Use filesystem and stream from memory.
        out_file->internal_data->vorbis = stb_vorbis_open_filename(path, &ogg_error, 0);  // TODO: allocator
        if (!out_file->internal_data->vorbis) {
            enum STBVorbisError err = ogg_error;
            switch (err) {
                default:
                    break;
            }
            KERROR("Failed to load vorbis file with error: %u", ogg_error);
            return false;
        }
        stb_vorbis_info info = stb_vorbis_get_info(out_file->internal_data->vorbis);
        out_file->internal_data->channels = info.channels;
        out_file->internal_data->sample_rate = info.sample_rate;
        out_file->internal_data->format = AL_FORMAT_MONO16;
        if (info.channels == 2) {
            out_file->internal_data->format = AL_FORMAT_STEREO16;
        }

        // Samples including all channels.
        out_file->total_samples_left = stb_vorbis_stream_length_in_samples(out_file->internal_data->vorbis) * info.channels;

        // Need a buffer to extract sample data into;
        u64 buffer_length = plugin->internal_state->config.chunk_size * sizeof(ALshort);
        out_file->internal_data->pcm = kallocate(buffer_length, MEMORY_TAG_AUDIO);

        return true;
    } else if (string_index_of_str(".mp3", path) != -1) {
        KTRACE("Processing MP3 file '%s'...", path);

        mp3dec_load(&plugin->internal_state->decoder, path, &out_file->internal_data->mp3_info, 0, 0);
        mp3dec_file_info_t* info = &out_file->internal_data->mp3_info;
        KDEBUG("mp3 freq: %dHz, avg kbit/s rate: %u", info->hz, info->avg_bitrate_kbps);
        out_file->internal_data->channels = info->channels;
        out_file->internal_data->sample_rate = info->hz;
        out_file->internal_data->format = AL_FORMAT_MONO16;
        if (info->channels) {
            out_file->internal_data->format = AL_FORMAT_STEREO16;
        }

        out_file->total_samples_left = info->samples;

        return true;
    }

    KERROR("Unsupported audio format.");
    return false;
}
static b8 oal_plugin_open_sound_file(audio_plugin* plugin, const char* path, audio_file* out_file) {
    if (!plugin || !path || !out_file) {
        KERROR("oal_plugin_open_sound_file requires valid pointers to plugin, path and out_file");
        return false;
    }

    kzero_memory(out_file, sizeof(audio_file));

    // Internal state.
    out_file->internal_data = kallocate(sizeof(audio_file_internal), MEMORY_TAG_AUDIO);
    out_file->internal_data->buffer = oal_plugin_find_free_buffer(plugin);
    if (out_file->internal_data->buffer == INVALID_ID) {
        KERROR("Unable to open audio file due to no buffers being available.");
        return false;
    }
    oal_plugin_check_error();
    if (string_index_of_str(".ogg", path) != -1) {
        KTRACE("Processing OGG sound file '%s'...", path);

        i32 ogg_error = 0;
        // TODO: Use filestream and stream from memory.
        out_file->internal_data->vorbis = stb_vorbis_open_filename(path, &ogg_error, 0);  // TODO: allocator
        if (!out_file->internal_data->vorbis) {
            enum STBVorbisError err = ogg_error;
            switch (err) {
                default:
                    break;
            }
            KERROR("Failed to load vorbis file with error: %u", ogg_error);
            return false;
        }
        stb_vorbis_info info = stb_vorbis_get_info(out_file->internal_data->vorbis);
        out_file->internal_data->channels = info.channels;
        out_file->internal_data->sample_rate = info.sample_rate;
        out_file->internal_data->format = AL_FORMAT_MONO16;
        if (info.channels == 2) {
            out_file->internal_data->format = AL_FORMAT_STEREO16;
        }

        // Samples including all channels.
        u64 length_samples = stb_vorbis_stream_length_in_samples(out_file->internal_data->vorbis) * info.channels;

        // Load all the data into a buffer at once.
        u64 buffer_length = length_samples * sizeof(ALshort);
        // Since the whole thing is being read into a buffer at once, just use an inline array for the data.
        ALshort* pcm = kallocate(buffer_length, MEMORY_TAG_AUDIO);
        i32 read_samples = stb_vorbis_get_samples_short_interleaved(out_file->internal_data->vorbis, info.channels, pcm, length_samples);
        if (read_samples != length_samples) {
            KWARN("Read/length mismatch while reading ogg file. This might cause playback issues.");
        }
        // Make sure this is a multiple of 4. If not, loading into the buffer below can fail.
        length_samples += (length_samples % 4);

        if (read_samples > 0) {
            // Load the whole thing into the buffer.
            alBufferData(out_file->internal_data->buffer, out_file->internal_data->format, pcm, length_samples, info.sample_rate);
            oal_plugin_check_error();
        }
        // Clean up
        kfree(pcm, buffer_length, MEMORY_TAG_AUDIO);

        if (read_samples == 0) {
            return false;
        }
    } else if (string_index_of_str(".mp3", path) != -1) {
        KTRACE("Processing MP3 sound file '%s'...", path);

        mp3dec_load(&plugin->internal_state->decoder, path, &out_file->internal_data->mp3_info, 0, 0);
        mp3dec_file_info_t* info = &out_file->internal_data->mp3_info;
        KDEBUG("mp3 freq: %dHz, avg kbit/s rate: %u", info->hz, info->avg_bitrate_kbps);
        out_file->internal_data->channels = info->channels;
        out_file->internal_data->sample_rate = info->hz;
        out_file->internal_data->format = AL_FORMAT_MONO16;
        if (info->channels) {
            out_file->internal_data->format = AL_FORMAT_STEREO16;
        }

        // Load the data into the buffer.
        if (info->samples > 0) {
            alBufferData(out_file->internal_data->buffer, out_file->internal_data->format, info->buffer, info->samples * sizeof(mp3d_sample_t), info->hz);
        }
    } else {
        KERROR("Unsupported audio format.");
        return false;
    }

    return true;
}

static void oal_plugin_close_audio_file(audio_plugin* plugin, audio_file* file) {
    if (!plugin || !file) {
        KERROR("oal_plugin_close_audio_file requires valid pointers to plugin and file");
        return;
    }

    if (file->internal_data) {
        if (file->internal_data->vorbis) {
            stb_vorbis_close(file->internal_data->vorbis);
            file->internal_data->vorbis = 0;
        } else if (file->internal_data->mp3_info.buffer) {
            // TODO: dispose of mp3 data.
        }
        kfree(file->internal_data, sizeof(audio_file_internal), MEMORY_TAG_AUDIO);
        file->internal_data = 0;
    }

    if (file->file_path) {
        string_free(file->file_path);
        file->file_path = 0;
    }

    kzero_memory(file, sizeof(audio_file));
}

static void oal_plugin_close_music_file(audio_plugin* plugin, music_file* file) {
    if (!plugin || !file) {
        KERROR("oal_plugin_close_music_file requires valid pointers to plugin and file");
        return;
    }

    if (file->internal_data) {
        if (file->internal_data->vorbis) {
            stb_vorbis_close(file->internal_data->vorbis);
            file->internal_data->vorbis = 0;

            // Also free the internal pcm buffer.
            if (file->internal_data->pcm) {
                u64 buffer_length = plugin->internal_state->config.chunk_size * sizeof(ALshort);
                kfree(file->internal_data->pcm, buffer_length, MEMORY_TAG_AUDIO);
                file->internal_data->pcm = 0;
            }
        } else if (file->internal_data->mp3_info.buffer) {
            // TODO: dispose of mp3 data.
        }

        kfree(file->internal_data, sizeof(audio_file_internal), MEMORY_TAG_AUDIO);
        file->internal_data = 0;
    }

    if (file->file_path) {
        string_free(file->file_path);
        file->file_path = 0;
    }

    kzero_memory(file, sizeof(audio_file));
}

typedef struct audio_music {
    music_file file;
    b8 trigger_stop;
} audio_music;

typedef struct play_music_job_params {
    struct audio_plugin* plugin;
    audio_music* music;
    audio_plugin_source* source;
} play_music_job_params;

static void oal_plugin_play_music_job_success(void* result_data) {
    play_music_job_params* play_result = (play_music_job_params*)result_data;
    KTRACE("Music played successfully.");
    // Reset the source.
    oal_plugin_source_reset(play_result->plugin, play_result->source, true);

    // TODO: event or callback on done playing?
}
static void oal_plugin_play_music_job_fail(void* result_data) {
    play_music_job_params* play_result = (play_music_job_params*)result_data;
    KTRACE("Music failed to play.");
    // Reset the source.
    oal_plugin_source_reset(play_result->plugin, play_result->source, true);

    // TODO: event or callback on done playing?
}

static b8 oal_plugin_play_music_job_entry(void* params, void* result_data) {
    play_music_job_params* play_params = (play_music_job_params*)params;
    play_music_job_params* play_result = (play_music_job_params*)result_data;

    b8 result = true;

    music_file* file = &play_params->music->file;
    audio_plugin_source* source = play_params->source;

    // Load data into all buffers initially.
    for (u32 i = 0; i < OAL_PLUGIN_MUSIC_BUFFER_COUNT; ++i) {
        if (!oal_plugin_stream_music_data(play_params->plugin, file->internal_data->buffers[i], file)) {
            KERROR("Failed to stream data to buffer &u in music file. File load failed.", i);
            result = false;
            break;
        }
    }

    if (result) {
        // Line up the buffers to be played.
        alSourceQueueBuffers(source->id, OAL_PLUGIN_MUSIC_BUFFER_COUNT, file->internal_data->buffers);
        if (oal_plugin_check_error()) {
            alSourcePlay(source->id);

            // Spin until the source is done playing.
            // NOTE: This means that audio files that loop hold a thread for the entirety of the loop.
            while (true) {
                if (!file->internal_data || !source) {
                    break;
                }
                if (play_params->music->trigger_stop) {
                    alSourceStop(source->id);
                    // Make sure to turn the stop flag back off.
                    play_params->music->trigger_stop = false;
                    break;
                }

                // Also try updating the stream.
                oal_plugin_stream_update(play_params->plugin, file, source);
                platform_sleep(2);
            }
            KTRACE("Sound playing complete.");
        } else {
            result = false;
        }
    }

    // Copy over the params data to the result data.
    play_result->source = source;
    play_result->plugin = play_params->plugin;
    play_result->music = play_params->music;
    return result;
}

typedef struct audio_sound {
    audio_file file;
    b8 trigger_stop;
} audio_sound;

typedef struct play_sound_job_params {
    struct audio_plugin* plugin;
    audio_sound* sound;
    audio_plugin_source* source;
} play_sound_job_params;

static void oal_plugin_play_sound_job_success(void* result_data) {
    play_sound_job_params* play_result = (play_sound_job_params*)result_data;
    KTRACE("Sound played successfully.");
    // Reset the source.
    oal_plugin_source_reset(play_result->plugin, play_result->source, true);

    // TODO: event or callback on done playing?
}
static void oal_plugin_play_sound_job_fail(void* result_data) {
    play_sound_job_params* play_result = (play_sound_job_params*)result_data;
    KTRACE("Sound failed to play.");
    // Reset the source.
    oal_plugin_source_reset(play_result->plugin, play_result->source, true);

    // TODO: event or callback on done playing?
}

static b8 oal_plugin_play_sound_job_entry(void* params, void* result_data) {
    play_sound_job_params* play_params = (play_sound_job_params*)params;
    play_sound_job_params* play_result = (play_sound_job_params*)result_data;

    b8 result = true;

    audio_file* file = &play_params->sound->file;
    audio_plugin_source* source = play_params->source;

    // oal_plugin_source_reset(play_params->plugin, source, false);

    alSourceQueueBuffers(source->id, 1, &file->internal_data->buffer);
    if (oal_plugin_check_error()) {
        alSourcePlay(source->id);

        // Spin until the source is done playing.
        // NOTE: This means that audio files that loop hold a thread for the entirety of the loop.
        ALint source_state;
        alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
        while (source_state == AL_PLAYING) {
            if (!file->internal_data || !source) {
                break;
            }
            if (play_params->sound->trigger_stop) {
                alSourceStop(source->id);
                // Make sure to turn the stop flag back off.
                play_params->sound->trigger_stop = false;
                break;
            }
            alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
            platform_sleep(2);
        }

        KTRACE("Sound playing complete, source index: %u.", source->id - 1);  // 1-indexed source ids.
    } else {
        result = false;
    }

    // Copy over the params data to the result data.
    play_result->source = source;
    play_result->plugin = play_params->plugin;
    play_result->sound = play_params->sound;
    return result;
}

struct audio_music* oal_plugin_load_music(struct audio_plugin* plugin, const char* path) {
    if (!plugin) {
        return 0;
    }

    // Load up the music file. This also loads the data into a buffer.
    audio_music* music = kallocate(sizeof(audio_music), MEMORY_TAG_AUDIO);
    if (!oal_plugin_open_music_file(plugin, path, &music->file)) {
        KERROR("Error opening file. Nothing to do.");
        kfree(music, sizeof(audio_music), MEMORY_TAG_AUDIO);
        return 0;
    }

    music->file.internal_data->is_looping = true;

    return music;
}

struct audio_sound* oal_plugin_load_sound(struct audio_plugin* plugin, const char* path) {
    if (!plugin) {
        return 0;
    }

    // Load up the sound file. This also loads the data into a buffer.
    audio_sound* sound = kallocate(sizeof(audio_sound), MEMORY_TAG_AUDIO);
    if (!oal_plugin_open_sound_file(plugin, path, &sound->file)) {
        KERROR("Error opening file. Nothing to do.");
        kfree(sound, sizeof(audio_sound), MEMORY_TAG_AUDIO);
        return 0;
    }

    return sound;
}

void oal_plugin_sound_close(struct audio_plugin* plugin, struct audio_sound* sound) {
    if (plugin && sound) {
        oal_plugin_close_audio_file(plugin, &sound->file);
        clear_buffer(plugin, &sound->file.internal_data->buffer, 1);
    }
}
void oal_plugin_music_close(struct audio_plugin* plugin, struct audio_music* music) {
    if (plugin && music) {
        oal_plugin_close_music_file(plugin, &music->file);
        clear_buffer(plugin, music->file.internal_data->buffers, 2);
    }
}

b8 oal_plugin_play_sound_with_volume(struct audio_plugin* plugin, struct audio_sound* sound, f32 volume, b8 loop) {
    // NOTE: This needs to be done on the main thread to avoid having to synchronize.
    audio_plugin_source* source = oal_plugin_find_free_source(plugin);
    if (!source) {
        KWARN("No free source could be found, adding to queue.");
        sound_queue_entry entry;
        entry.volume = volume;
        entry.sound = sound;
        entry.loop = loop;
        darray_push(plugin->internal_state->queued_sounds, entry);
        return true;
    }

    // Make sure the source is reset.
    oal_plugin_source_reset(plugin, source, false);

    // Spin this off to a job.
    play_sound_job_params params = {0};
    params.plugin = plugin;
    params.sound = sound;
    params.source = source;

    // Set the volume.
    oal_plugin_source_gain_set(plugin, source->id, volume);
    // Effectively disable "3d" sound by placing the source at the same position as the listener.
    oal_plugin_source_position_set(plugin, source->id, plugin->internal_state->listener_position);

    oal_plugin_source_looping_set(plugin, source->id, loop);

    job_info job = job_create(oal_plugin_play_sound_job_entry, oal_plugin_play_sound_job_success, oal_plugin_play_sound_job_fail, &params, sizeof(play_sound_job_params), sizeof(play_sound_job_params));
    job_system_submit(job);

    return true;
}

b8 oal_plugin_play_music_with_volume(struct audio_plugin* plugin, struct audio_music* music, f32 volume, b8 loop) {
    // NOTE: This needs to be done on the main thread to avoid having to synchronize.
    audio_plugin_source* source = oal_plugin_find_free_source(plugin);
    if (!source) {
        KWARN("No free source could be found, adding to queue.");
        music_queue_entry entry;
        entry.volume = volume;
        entry.music = music;
        entry.loop = loop;
        darray_push(plugin->internal_state->queued_music, entry);
        return true;
    }

    // Make sure the source is reset.
    oal_plugin_source_reset(plugin, source, false);

    // Spin this off to a job.
    play_music_job_params params = {0};
    params.plugin = plugin;
    params.music = music;
    params.source = source;

    // Set looping for music.
    params.music->file.internal_data->is_looping = loop;
    // Ensure looping is _off_ on the source, as this is handled differently for streamed resources (i.e. music).
    oal_plugin_source_looping_set(plugin, source->id, false);

    job_info job = job_create(oal_plugin_play_music_job_entry, oal_plugin_play_music_job_success, oal_plugin_play_music_job_fail, &params, sizeof(play_sound_job_params), sizeof(play_sound_job_params));

    // Set the volume.
    if (!oal_plugin_source_gain_set(plugin, source->id, volume)) {
        KERROR("Failed to set gain on source for music playback.");
    }
    // Effectively disable "3d" sound by placing the source at the same position as the listener.
    if (!oal_plugin_source_position_set(plugin, source->id, plugin->internal_state->listener_position)) {
        KERROR("Failed to set position on source for music playback.");
    }

    job_system_submit(job);

    return true;
}
b8 oal_plugin_play_emitter(struct audio_plugin* plugin, f32 master_volume, struct audio_emitter* emitter) {
    if (!plugin || !emitter) {
        return false;
    }

    if (!emitter->music && !emitter->sound) {
        KERROR("Emitter must have music or sound assigned.");
        return false;
    }

    if (emitter->music && emitter->sound) {
        KERROR("Emitter cannot have both music and sound assigned.");
        return false;
    }

    // Calculate the volume by multiplyingthe master volume against the emitter volume.
    f32 volume = master_volume * emitter->volume;

    // NOTE: This needs to be done on the main thread to avoid having to synchronize.
    audio_plugin_source* source = oal_plugin_find_free_source(plugin);
    if (!source) {
        KWARN("No free source could be found, adding to queue.");
        emitter_queue_entry entry;
        entry.master_volume = master_volume;
        entry.emitter = emitter;
        darray_push(plugin->internal_state->queued_emitters, entry);
        return true;
    }

    // Make sure the source is reset.
    oal_plugin_source_reset(plugin, source, false);

    // Spin this off to a job.
    job_info job;
    if (emitter->sound) {
        play_sound_job_params params = {0};
        params.plugin = plugin;
        params.sound = emitter->sound;
        params.source = source;

        // Set looping. Note that looping for music is handled differently.
        oal_plugin_source_looping_set(plugin, source->id, emitter->looping);

        job = job_create(oal_plugin_play_sound_job_entry, oal_plugin_play_sound_job_success, oal_plugin_play_sound_job_fail, &params, sizeof(play_sound_job_params), sizeof(play_sound_job_params));
    } else if (emitter->music) {
        play_music_job_params params = {0};
        params.plugin = plugin;
        params.music = emitter->music;
        params.source = source;

        // Set looping for music.
        emitter->music->file.internal_data->is_looping = emitter->looping;

        job = job_create(oal_plugin_play_music_job_entry, oal_plugin_play_music_job_success, oal_plugin_play_music_job_fail, &params, sizeof(play_music_job_params), sizeof(play_sound_job_params));
    } else {
        KERROR("Cannot play emitter that has no sound or music.");
        return false;
    }

    emitter->source_id = source->id;

    // Set the volume.
    oal_plugin_source_gain_set(plugin, source->id, volume);
    // Set the position.
    oal_plugin_source_position_set(plugin, source->id, emitter->position);

    // Kick off the job.
    job_system_submit(job);
    return true;
}

b8 oal_plugin_update_emitter(struct audio_plugin* plugin, f32 master_volume, struct audio_emitter* emitter) {
    if (!plugin || !emitter) {
        return false;
    }

    if (emitter->source_id != INVALID_ID && emitter->source_id > 0) {
        // Calculate the volume by multiplying the master volume against the emitter volume.
        f32 volume = master_volume * emitter->volume;

        // Set the volume.
        oal_plugin_source_gain_set(plugin, emitter->source_id, volume);
        // Set the position.
        oal_plugin_source_position_set(plugin, emitter->source_id, emitter->position);
        // Set looping on source if sound.
        if (emitter->sound) {
            oal_plugin_source_looping_set(plugin, emitter->source_id, emitter->looping);
        } else {
            // Set looping for music.
            emitter->music->file.internal_data->is_looping = emitter->looping;
        }
        return true;
    }

    return false;
}

b8 oal_plugin_stop_emitter(struct audio_plugin* plugin, struct audio_emitter* emitter) {
    if (!plugin || !emitter) {
        return false;
    }

    // Disassociate the source. A new one will be picked up on the next play.
    emitter->source_id = INVALID_ID;

    if (emitter->sound) {
        emitter->sound->trigger_stop = true;
    } else if (emitter->music) {
        emitter->music->trigger_stop = true;
    } else {
        KERROR("Unable to stop emitter with no sound or music.");
        return false;
    }

    return true;
}
