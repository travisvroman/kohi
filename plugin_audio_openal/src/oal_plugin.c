#include "oal_plugin.h"

#include <alloca.h>
#include <core/kmutex.h>
#include <core/kthread.h>
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

    // Worker thread for this source.
    kthread thread;

    kmutex data_mutex;  // everything from here down should be accessed/changed during lock.
    struct audio_sound* current_sound;
    struct audio_music* current_music;
    b8 trigger_play;
    b8 trigger_exit;
} audio_plugin_source;

// Internal audio file data. This is for sound effects.
typedef struct sound_file_internal {
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
} sound_file_internal;

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
    u32 total_samples_left;
} music_file_internal;


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

    // MP3 decoder;
    mp3dec_t decoder;

} audio_plugin_state;

typedef struct audio_music {
    music_file file;
    b8 trigger_stop;
} audio_music;

typedef struct audio_sound {
    sound_file file;
    b8 trigger_stop;
} audio_sound;

static b8 oal_plugin_check_error();
static b8 oal_plugin_source_create(struct audio_plugin* plugin, audio_plugin_source* out_source);
static void oal_plugin_source_destroy(struct audio_plugin* plugin, audio_plugin_source* source);
static u32 oal_plugin_find_free_buffer(struct audio_plugin* plugin);

// HACK: This should be in a file loader and streamed, but the file system doesn't yet support this...
static b8 oal_plugin_stream_music_data(audio_plugin* plugin, ALuint buffer, music_file_internal* internal_data) {
    if (!plugin || !internal_data) {
        return false;
    }

    // Figure out how many samples can be taken.
    u64 size = 0;
    if (internal_data->vorbis) {
        i64 samples = stb_vorbis_get_samples_short_interleaved(internal_data->vorbis, internal_data->channels, internal_data->pcm + size, plugin->internal_state->config.chunk_size - size);
        // Sample here does not include channels, so factor them in.
        size = samples * internal_data->channels;
    } else if (internal_data->mp3_info.buffer) {
        // samples count includes channels.
        size = KMIN(internal_data->total_samples_left, plugin->internal_state->config.chunk_size);
    }

    // 0 means the end of the file has been reached, and either the stream stops or needs to start over.
    if (size == 0) {
        return false;
    }

    // Load the data into the buffer.
    if (internal_data->vorbis) {
        alBufferData(buffer, internal_data->format, internal_data->pcm, size * sizeof(ALshort), internal_data->sample_rate);
        oal_plugin_check_error();
    } else if (internal_data->mp3_info.buffer) {
        u64 pos = internal_data->mp3_info.samples - internal_data->total_samples_left;
        alBufferData(buffer, internal_data->format, internal_data->mp3_info.buffer + pos, size * sizeof(ALshort), internal_data->sample_rate);
        oal_plugin_check_error();
    }

    // Update the samples remaining.
    internal_data->total_samples_left -= size;

    return true;
}
static b8 oal_plugin_stream_update(audio_plugin* plugin, music_file_internal* internal_data, audio_plugin_source* source) {
    if (!plugin || !internal_data) {
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
        if (!oal_plugin_stream_music_data(plugin, buffer_id, internal_data)) {
            b8 done = true;

            // If set to loop, start over at the beginning.
            if (internal_data->is_looping) {
                if (internal_data->vorbis) {
                    // Loop around.
                    stb_vorbis_seek_start(internal_data->vorbis);

                    // Reset sample counter.
                    internal_data->total_samples_left = stb_vorbis_stream_length_in_samples(internal_data->vorbis) * internal_data->channels;
                } else if (internal_data->mp3_info.samples) {
                    // Reset sample counter.
                    internal_data->total_samples_left = internal_data->mp3_info.samples;
                }
                done = !oal_plugin_stream_music_data(plugin, buffer_id, internal_data);
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

typedef struct source_work_thread_params {
    struct audio_plugin* plugin;
    audio_plugin_source* source;
} source_work_thread_params;

static u32 source_work_thread(void* params) {
    source_work_thread_params* typed_params = params;
    audio_plugin* plugin = typed_params->plugin;
    audio_plugin_source* source = typed_params->source;

    // Release this right away since it's no longer needed.
    kfree(params, sizeof(source_work_thread_params), MEMORY_TAG_AUDIO);

    KDEBUG("Audio source thread starting...");

    b8 do_break = false;
    while (!do_break) {
        if(!source->data_mutex.internal_data) {
            // This can happen during unexpected shutdown, and if so kill the thread.
            return 0;
        }
        kmutex_lock(&source->data_mutex);
        if (source->trigger_exit) {
            do_break = true;
        }
        if (source->trigger_play) {
            alSourcePlay(source->id);
            source->trigger_play = false;
        }
        kmutex_unlock(&source->data_mutex);

        if (source->current_music) {
            // If currently playing music, try updating the stream.
            oal_plugin_stream_update(plugin, source->current_music->file.internal_data, source);
        }

        platform_sleep(2);
    }

    KDEBUG("Audio source thread shutting down.");
    return 0;
}

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
    if (!oal_plugin_source_gain_set(plugin, source->id - 1, 1.0f)) {
        KERROR("Failed to set source default gain.");
        return false;
    }
    if (!oal_plugin_source_pitch_set(plugin, source->id - 1, 1.0f)) {
        KERROR("Failed to set source default pitch.");
        return false;
    }
    if (!oal_plugin_source_position_set(plugin, source->id - 1, vec3_zero())) {
        KERROR("Failed to set source default position.");
        return false;
    }
    if (!oal_plugin_source_looping_set(plugin, source->id - 1, false)) {
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

    // Create the source worker thread's mutex.
    kmutex_create(&out_source->data_mutex);

    // Also create the worker thread itself for this source.
    source_work_thread_params* params = kallocate(sizeof(source_work_thread_params), MEMORY_TAG_AUDIO);
    params->source = out_source;
    params->plugin = plugin;
    kthread_create(source_work_thread, params, true, &out_source->thread);

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

b8 oal_plugin_source_gain_query(struct audio_plugin* plugin, u32 source_index, f32* out_gain) {
    if (plugin && out_gain && source_index <= plugin->internal_state->config.max_sources) {
        *out_gain = plugin->internal_state->sources[source_index].gain;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_gain_set(struct audio_plugin* plugin, u32 source_index, f32 gain) {
    if (plugin && source_index <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_index];
        source->gain = gain;
        alSourcef(source->id, AL_GAIN, gain);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_pitch_query(struct audio_plugin* plugin, u32 source_index, f32* out_pitch) {
    if (plugin && out_pitch && source_index <= plugin->internal_state->config.max_sources) {
        *out_pitch = plugin->internal_state->sources[source_index].pitch;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_pitch_set(struct audio_plugin* plugin, u32 source_index, f32 pitch) {
    if (plugin && source_index <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_index];
        source->pitch = pitch;
        alSourcef(source->id, AL_PITCH, pitch);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_position_query(struct audio_plugin* plugin, u32 source_index, vec3* out_position) {
    if (plugin && out_position && source_index <= plugin->internal_state->config.max_sources) {
        *out_position = plugin->internal_state->sources[source_index].position;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_position_set(struct audio_plugin* plugin, u32 source_index, vec3 position) {
    if (plugin && source_index <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_index];
        source->position = position;
        alSource3f(source->id, AL_POSITION, position.x, position.y, position.z);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_looping_query(struct audio_plugin* plugin, u32 source_index, b8* out_looping) {
    if (plugin && out_looping && source_index <= plugin->internal_state->config.max_sources) {
        *out_looping = plugin->internal_state->sources[source_index].looping;
        return true;
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
    return false;
}

b8 oal_plugin_source_looping_set(struct audio_plugin* plugin, u32 source_index, b8 looping) {
    if (plugin && source_index <= plugin->internal_state->config.max_sources) {
        audio_plugin_source* source = &plugin->internal_state->sources[source_index];
        source->looping = looping;
        alSourcei(source->id, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        return oal_plugin_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", source_index);
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
        out_file->internal_data->total_samples_left = stb_vorbis_stream_length_in_samples(out_file->internal_data->vorbis) * info.channels;

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

        out_file->internal_data->total_samples_left = info->samples;

        return true;
    }

    KERROR("Unsupported audio format.");
    return false;
}
static b8 oal_plugin_open_sound_file(audio_plugin* plugin, const char* path, sound_file* out_file) {
    if (!plugin || !path || !out_file) {
        KERROR("oal_plugin_open_sound_file requires valid pointers to plugin, path and out_file");
        return false;
    }

    kzero_memory(out_file, sizeof(sound_file));

    // Internal state.
    out_file->internal_data = kallocate(sizeof(sound_file_internal), MEMORY_TAG_AUDIO);
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
    if (!plugin || !sound) {
        KERROR("oal_plugin_close_sound_file requires valid pointers to plugin and file");
        return;
    }

    clear_buffer(plugin, &sound->file.internal_data->buffer, 0);
    sound_file* file = &sound->file;

    if (file->internal_data) {
        if (file->internal_data->vorbis) {
            stb_vorbis_close(file->internal_data->vorbis);
            file->internal_data->vorbis = 0;
        } else if (file->internal_data->mp3_info.buffer) {
            // TODO: dispose of mp3 data.
        }
        kfree(file->internal_data, sizeof(sound_file_internal), MEMORY_TAG_AUDIO);
        file->internal_data = 0;
    }

    if (file->file_path) {
        string_free(file->file_path);
        file->file_path = 0;
    }

    kzero_memory(file, sizeof(sound_file));
}
void oal_plugin_music_close(struct audio_plugin* plugin, struct audio_music* music) {
    if (!plugin || !music) {
        KERROR("oal_plugin_close_music_file requires valid pointers to plugin and file");
        return;
    }

    clear_buffer(plugin, music->file.internal_data->buffers, 2);
    music_file* file = &music->file;

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

        kfree(file->internal_data, sizeof(sound_file_internal), MEMORY_TAG_AUDIO);
        file->internal_data = 0;
    }

    if (file->file_path) {
        string_free(file->file_path);
        file->file_path = 0;
    }

    kzero_memory(file, sizeof(sound_file));
}

b8 oal_plugin_source_play(struct audio_plugin* plugin, i8 source_index) {
    if (!plugin || source_index < 0) {
        return false;
    }

    audio_plugin_source* source = &plugin->internal_state->sources[source_index];
    kmutex_lock(&source->data_mutex);
    if (source->current_sound || source->current_music) {
        source->trigger_play = true;
        source->in_use = true;
    }
    kmutex_unlock(&source->data_mutex);

    return true;
}

b8 oal_plugin_sound_play_on_source(struct audio_plugin* plugin, struct audio_sound* sound, i8 source_index, b8 loop) {
    if (!plugin || !sound || source_index < 0) {
        return false;
    }

    // Assign the sound's buffer to the source.
    audio_plugin_source* source = &plugin->internal_state->sources[source_index];
    kmutex_lock(&source->data_mutex);
    alSourceStop(source->id);
    alSourceQueueBuffers(source->id, 1, &sound->file.internal_data->buffer);

    // Unassign music, if appropriate, and assign sound.
    source->current_sound = sound;
    source->current_music = 0;
    source->in_use = true;
    source->trigger_play = true;
    kmutex_unlock(&source->data_mutex);

    return true;
}

b8 oal_plugin_music_play_on_source(struct audio_plugin* plugin, struct audio_music* music, i8 source_index, b8 loop) {
    if (!plugin || !music || source_index < 0) {
        return false;
    }

    // Assign the music's buffers to the source.
    audio_plugin_source* source = &plugin->internal_state->sources[source_index];

    // Load data into all buffers initially.
    b8 result = true;
    for (u32 i = 0; i < OAL_PLUGIN_MUSIC_BUFFER_COUNT; ++i) {
        if (!oal_plugin_stream_music_data(plugin, music->file.internal_data->buffers[i], music->file.internal_data)) {
            KERROR("Failed to stream data to buffer &u in music file. File load failed.", i);
            result = false;
            break;
        }
    }

    if (result) {
        kmutex_lock(&source->data_mutex);
        // Unassign sound, if appropriate, and assign music.
        source->current_sound = 0;
        source->current_music = music;
        alSourceStop(source->id);
        alSourceQueueBuffers(source->id, OAL_PLUGIN_MUSIC_BUFFER_COUNT, music->file.internal_data->buffers);

        source->in_use = true;
        source->trigger_play = true;
        kmutex_unlock(&source->data_mutex);
    }

    return true;
}

b8 oal_plugin_source_stop(struct audio_plugin* plugin, i8 source_index) {
    if (!plugin || source_index < 0) {
        return false;
    }

    audio_plugin_source* source = &plugin->internal_state->sources[source_index];

    // Stop/reset if the source is currently playing or paused.
    ALint source_state;
    alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
    if (source_state == AL_PAUSED || source_state == AL_PLAYING) {
        alSourceStop(source->id);

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

        // Rewind.
        alSourceRewind(source->id);
    }

    source->in_use = false;

    return true;
}
b8 oal_plugin_source_pause(struct audio_plugin* plugin, i8 source_index) {
    if (!plugin || source_index < 0) {
        return false;
    }

    // Trigger a pause if the source is currently playing.
    audio_plugin_source* source = &plugin->internal_state->sources[source_index];
    ALint source_state;
    alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
    if (source_state == AL_PLAYING) {
        alSourcePause(source->id);
    }

    return true;
}
b8 oal_plugin_source_resume(struct audio_plugin* plugin, i8 source_index) {
    if (!plugin || source_index < 0) {
        return false;
    }

    // Trigger a resume if the source is currently paused.
    audio_plugin_source* source = &plugin->internal_state->sources[source_index];
    ALint source_state;
    alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
    if (source_state == AL_PAUSED) {
        alSourcePlay(source->id);
    }

    return true;
}
