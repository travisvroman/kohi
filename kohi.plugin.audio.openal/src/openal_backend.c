#include "openal_backend.h"

// OpenAL
#ifdef KPLATFORM_WINDOWS
#    include <al.h>
#    include <alc.h>
#else
#    include <AL/al.h>
#    include <AL/alc.h>
#endif

// Core
#include <containers/darray.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <threads/kmutex.h>
#include <threads/kthread.h>
#include <strings/kstring.h>

// #ifdef KPLATFORM_WINDOWS
// #    include <malloc.h>
// #else
// #    include <alloca.h>
// #endif

// Runtime
#include <audio/kaudio_types.h>
#include <core_audio_types.h>
#include <systems/job_system.h>
#include <utils/audio_utils.h>

// The number of buffers used for streaming music file data.
#define OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT 2

// This corresponds to audio data by index on the frontend.
typedef struct kaudio_internal_data {
    // The openal sound format (i.e. 16-bit mono/stereo)
    u32 format;
    // The current buffer being used to play sound effect types.
    ALuint buffer;
    // A secondary buffer used to play downmixed mono where the original asset was stereo.
    ALuint mono_buffer;
    // The internal buffers used for streaming data from larger files.
    ALuint streaming_buffers[OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT];
    // Indicates if the music file should loop.
    b8 is_looping;
    // Indicates if the internal asset should be streamed or all loaded at once.
    b8 is_stream;

    // Used to track samples in streaming type files.
    u32 total_samples_left;

    // The number of channels (i.e. 1 for mono or 2 for stereo)
    i32 channels;
    // The sample rate of the sound/music (i.e. 44100)
    u32 sample_rate;

    u32 total_sample_count;

    u64 pcm_data_size;
    /** Pulse-code modulation buffer, or raw data to be fed into a buffer. */
    i16* pcm_data;
    i16* mono_pcm_data;
    u64 downmixed_size;
} kaudio_internal_data;

// Sources are used to play sounds, potentially at a space in 3D.
typedef struct kaudio_plugin_source {
    // Internal OpenAL source.
    ALCuint id;

    // Worker thread for this source.
    kthread thread;

    kmutex data_mutex; // everything from here down should be accessed/changed during lock.
    // Currently playing audio data. Is INVALID_KAUDIO if not in use.
    kaudio current;
    // The current audio space.
    kaudio_space current_audio_space;

    b8 trigger_play;
    b8 trigger_exit;
} kaudio_plugin_source;

// The internal state for this audio backend.
typedef struct kaudio_backend_state {

    /** @brief The maximum number of buffers available. Default: 256 */
    u32 max_buffers;
    /**
     * @brief The maximum number of sources available.
     *
     * These map to "channels" on the frontend.
     * Default: 8
     * */
    u32 max_sources;

    /** @brief The frequency to output audio at. */
    u32 frequency;
    /**
     * @brief The number of audio channels to support (i.e. 2 for stereo, 1 for mono).
     * not to be confused with audio_channel_count below.
     */
    u32 channel_count;

    /**
     * The size to chunk streamed audio data in.
     */
    u32 chunk_size;

    // The selected audio device.
    ALCdevice* device;
    // The current audio context.
    ALCcontext* context;
    // A pool of buffers to be used for all kinds of audio/music playback.
    ALuint* buffers;
    // The total number of buffers available.
    u32 buffer_count;

    // A collection of available sources. config.max_sources has the count of this.
    kaudio_plugin_source* sources;

    // An array to keep free/available buffer ids.
    u32* free_buffers;

    // The max number of audios that can be loaded at any one time. Synced with frontend.
    u32 max_count;

    // Internal data array aligning with that of the frontend.
    kaudio_internal_data* datas;
} kaudio_backend_state;

typedef struct ksource_work_thread_params {
    kaudio_backend_interface* backend;
    kaudio_plugin_source* source;
} ksource_work_thread_params;

static b8 openal_backend_check_error(void);
static b8 openal_backend_channel_create(kaudio_backend_interface* backend, kaudio_plugin_source* out_source);
static void openal_backend_channel_destroy(kaudio_backend_interface* backend, kaudio_plugin_source* source);
static u32 openal_backend_find_free_buffer(kaudio_backend_interface* backend);

static b8 stream_data(kaudio_backend_interface* backend, ALuint buffer, kaudio_space audio_space, kaudio audio);
static b8 openal_backend_stream_update(kaudio_backend_interface* plugin, kaudio_plugin_source* source);
static u32 source_work_thread(void* params);
static b8 source_set_defaults(kaudio_backend_interface* backend, kaudio_plugin_source* source, b8 reset_use);
static b8 openal_backend_channel_create(kaudio_backend_interface* backend, kaudio_plugin_source* out_source);
static void openal_backend_channel_destroy(kaudio_backend_interface* backend, kaudio_plugin_source* source);
static void openal_backend_find_playing_sources(kaudio_backend_interface* backend, u32 playing[], u32* count);
static void clear_buffer(kaudio_backend_interface* backend, u32* buf_ptr, u32 amount);
static u32 openal_backend_find_free_buffer(kaudio_backend_interface* backend);
static const char* openal_backend_error_str(ALCenum err);
static b8 openal_backend_check_error(void);
static b8 channel_id_valid(kaudio_backend_state* state, u8 channel_id);

b8 openal_backend_initialize(kaudio_backend_interface* backend, const kaudio_backend_config* config) {
    if (backend) {
        backend->internal_state = kallocate(sizeof(kaudio_backend_state), MEMORY_TAG_AUDIO);
        kaudio_backend_state* state = backend->internal_state;

        // Copy over the relevant frontend config properties.
        state->max_sources = config->audio_channel_count; // MAX_AUDIO_CHANNELS;
        state->chunk_size = config->chunk_size;
        state->frequency = config->frequency;
        state->channel_count = config->channel_count;
        state->max_count = config->max_count;
        state->datas = KALLOC_TYPE_CARRAY(kaudio_internal_data, state->max_count);

        state->buffer_count = 256; // FIXME: load from config.

        if (state->max_sources < 1) {
            KWARN("Audio plugin config.max_sources was configured as 0. Defaulting to 8.");
            state->max_sources = 8;
        }

        // Get the default device. TODO: enumerate devices and pick via ALC_ENUMERATION_EXT?
        state->device = alcOpenDevice(0);
        // NOTE: Don't check for errors before context is created and made current.
        // openal_backend_check_error();
        if (!state->device) {
            KERROR("Unable to obtain OpenAL device. Plugin initialize failed.");
            return false;
        } else {
            KINFO("OpenAL Device acquired.");
        }

        // Get context and make it current.
        state->context = alcCreateContext(state->device, 0);
        // NOTE: Don't check for errors before context is created and made current.
        // openal_backend_check_error();
        if (!alcMakeContextCurrent(state->context)) {
            openal_backend_check_error();
        }

        // Disable the built-in attenuation models as the frontend handles this.
        alDistanceModel(AL_NONE);

        // Configure the listener with some defaults.
        openal_backend_listener_position_set(backend, vec3_zero());
        openal_backend_listener_orientation_set(backend, vec3_forward(), vec3_up());

        // NOTE: zeroing out velocity
        alListener3f(AL_VELOCITY, 0, 0, 0);
        openal_backend_check_error();

        state->sources = kallocate(sizeof(kaudio_plugin_source) * state->max_sources, MEMORY_TAG_AUDIO);
        // Create all sources.
        for (u32 i = 0; i < state->max_sources; ++i) {
            if (!openal_backend_channel_create(backend, &state->sources[i])) {
                KERROR("Unable to create audio source in OpenAL plugin.");
                return false;
            }
        }

        // Buffers
        // TODO: Should make a pool for this.
        state->buffers = kallocate(sizeof(u32) * state->buffer_count, MEMORY_TAG_ARRAY);
        alGenBuffers(state->buffer_count, state->buffers);
        openal_backend_check_error();

        state->free_buffers = darray_create(u32);

        // Make sure all buffers are marked as free. Note that the array of buffers retrieved above must be used
        // directly, as there is no guarantee as to what the buffer ids will be. On one Windows installation, this
        // started at id 9. Yep. 9. Makes no sense, I'll tell ya hwhat. But there it is.
        for (u32 i = 0; i < state->buffer_count; ++i) {
            darray_push(state->free_buffers, state->buffers[i]);
        }

        // NOTE: source generation, which is basically a sound emitter.
        KINFO("OpenAL plugin intialized.");

        return true;
    }

    KERROR("openal_backend_initialize requires a valid pointer to a backend.");
    return false;
}

void openal_backend_shutdown(kaudio_backend_interface* backend) {
    if (backend) {
        if (backend->internal_state) {
            // Destroy sources.
            for (u32 i = 0; i < backend->internal_state->max_sources; ++i) {
                openal_backend_channel_destroy(backend, &backend->internal_state->sources[i]);
            }
            if (backend->internal_state->device) {
                alcCloseDevice(backend->internal_state->device);
                backend->internal_state->device = 0;
            }
            kfree(backend->internal_state, sizeof(kaudio_backend_state), MEMORY_TAG_AUDIO);
            backend->internal_state = 0;
        }

        kzero_memory(backend, sizeof(kaudio_backend_interface));
    }
}

b8 openal_backend_update(kaudio_backend_interface* backend, struct frame_data* p_frame_data) {
    if (!backend) {
        return false;
    }

    return true;
}

b8 openal_backend_load(struct kaudio_backend_interface* backend, i32 channels, u32 sample_rate, u32 total_sample_count, u64 pcm_data_size, i16* pcm_data, b8 is_stream, kaudio audio) {
    kaudio_backend_state* state = backend->internal_state;

    // Get the internal data.
    kaudio_internal_data* data = &state->datas[audio];
    data->is_stream = is_stream;
    data->channels = channels;
    data->sample_rate = sample_rate;
    data->total_sample_count = total_sample_count;
    data->pcm_data_size = pcm_data_size;
    data->pcm_data = kallocate(pcm_data_size, MEMORY_TAG_ARRAY);
    kcopy_memory(data->pcm_data, pcm_data, pcm_data_size);

    data->format = AL_FORMAT_MONO16;
    if (data->channels == 2) {
        data->format = AL_FORMAT_STEREO16;
        // TODO: maybe do this on the frontend?
        // If the asset is stereo, get a downmixed version of the audio so it can be used
        // as a "3D" sound if need be.
        data->mono_pcm_data = kaudio_downmix_stereo_to_mono(data->pcm_data, data->total_sample_count);
        data->downmixed_size = (data->total_sample_count / 2) * sizeof(i16);
    } else {
        // Asset was already mono, just point to the pcm data.
        data->mono_pcm_data = data->pcm_data;
        data->downmixed_size = 0; // Set to zero to indicate this shouldn't be freed separately.
    }

    data->total_samples_left = total_sample_count;

    if (is_stream) {
        // Streams need buffers to be used back to back.
        for (u32 i = 0; i < OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT; ++i) {
            data->streaming_buffers[i] = openal_backend_find_free_buffer(backend);
            if (data->streaming_buffers[i] == INVALID_ID) {
                KERROR("Unable to load streaming audio due to no buffers being available.");
                return 0;
            }

            // Streams loop by default.
            data->is_looping = true;
        }
        openal_backend_check_error();
    } else {
        // Non-streams only need one buffer.
        data->buffer = openal_backend_find_free_buffer(backend);
        if (data->buffer == INVALID_ID) {
            KERROR("Unable to open audio file due to no buffers being available.");
            return false;
        }
        openal_backend_check_error();

        if (data->total_samples_left > 0) {
            // Load the whole thing into the buffer.
            alBufferData(data->buffer, data->format, (i16*)data->pcm_data, data->total_samples_left, data->sample_rate);
            openal_backend_check_error();
        }

        // However, if the asset is stereo, a second buffer is required for the mono data.
        data->mono_buffer = openal_backend_find_free_buffer(backend);
        if (data->mono_buffer == INVALID_ID) {
            KERROR("Unable to open audio file due to no buffers being available.");
            return false;
        }
        openal_backend_check_error();
        if (data->total_samples_left > 0) {
            // Load the whole thing into the buffer.
            alBufferData(data->buffer, AL_FORMAT_MONO16, (i16*)data->mono_pcm_data, data->total_samples_left, data->sample_rate);
            openal_backend_check_error();
        }

        // Non-streams do not loop by default.
        data->is_looping = false;
    }

    return true;
}

void openal_backend_unload(struct kaudio_backend_interface* backend, kaudio audio) {
    kaudio_backend_state* state = backend->internal_state;

    // Get the internal data.
    kaudio_internal_data* data = &state->datas[audio];

    clear_buffer(backend, &data->buffer, 0);

    // FIXME: Mark entry as available for use
}

b8 openal_backend_listener_position_set(kaudio_backend_interface* backend, vec3 position) {
    if (!backend) {
        KERROR("openal_backend_listener_position_set requires a valid pointer to a plugin.");
        return false;
    }

    alListener3f(AL_POSITION, position.x, position.y, position.z);
    openal_backend_check_error();

    return true;
}

b8 openal_backend_listener_orientation_set(kaudio_backend_interface* backend, vec3 forward, vec3 up) {
    if (!backend) {
        KERROR("openal_backend_listener_orientation_set requires a valid pointer to a plugin.");
        return false;
    }

    ALfloat listener_orientation[] = {forward.x, forward.y, forward.z, up.x, up.y, up.z};
    alListenerfv(AL_ORIENTATION, listener_orientation);
    return openal_backend_check_error();
}

b8 openal_backend_channel_gain_set(kaudio_backend_interface* backend, u8 channel_id, f32 gain) {
    if (!backend) {
        return false;
    }
    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        kaudio_plugin_source* source = &state->sources[channel_id];
        alSourcef(source->id, AL_GAIN, gain);
        return openal_backend_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
    return false;
}

b8 openal_backend_channel_pitch_set(kaudio_backend_interface* backend, u8 channel_id, f32 pitch) {
    if (!backend) {
        return false;
    }
    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        kaudio_plugin_source* source = &state->sources[channel_id];
        alSourcef(source->id, AL_PITCH, pitch);
        return openal_backend_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
    return false;
}

b8 openal_backend_channel_position_set(kaudio_backend_interface* backend, u8 channel_id, vec3 position) {
    if (!backend) {
        return false;
    }
    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        kaudio_plugin_source* source = &state->sources[channel_id];
        alSource3f(source->id, AL_POSITION, position.x, position.y, position.z);
        return openal_backend_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
    return false;
}

b8 openal_backend_channel_looping_set(kaudio_backend_interface* backend, u8 channel_id, b8 looping) {
    if (!backend) {
        return false;
    }
    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        kaudio_plugin_source* source = &state->sources[channel_id];
        alSourcei(source->id, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        return openal_backend_check_error();
    }

    KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
    return false;
}

b8 openal_backend_channel_play(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        kaudio_plugin_source* source = &state->sources[channel_id];
        kmutex_lock(&source->data_mutex);
        if (source->current) {
            source->trigger_play = true;
        }
        kmutex_unlock(&source->data_mutex);
    }

    return true;
}

b8 openal_backend_channel_play_audio(kaudio_backend_interface* backend, kaudio audio, kaudio_space audio_space, u8 channel_id) {
    if (!channel_id_valid(backend->internal_state, channel_id)) {
        return false;
    }

    KTRACE("Play on channel %d", channel_id);
    kaudio_backend_state* state = backend->internal_state;
    kaudio_internal_data* data = &state->datas[audio];

    // Assign the sound's buffer to the source.
    kaudio_plugin_source* source = &state->sources[channel_id];
    kmutex_lock(&source->data_mutex);
    source->current_audio_space = audio_space;

    if (data->is_stream) {
        // Load data into all buffers initially.
        b8 result = true;
        for (u32 i = 0; i < OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT; ++i) {
            if (!stream_data(backend, data->streaming_buffers[i], audio_space, audio)) {
                KERROR("Failed to stream data to buffer &u in music file. File load failed.", i);
                result = false;
                break;
            }
        }
        // Queue up new buffers.
        alSourceQueueBuffers(source->id, OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT, data->streaming_buffers);
        openal_backend_check_error();
        if (!result) {
            KERROR("Failed to stream audio data. See logs for details.");
            return false;
        }
    } else {

        // If a sound is currently playing here, clip it off by stopping first.
        alSourceStop(source->id);

        // Unqueue any existing buffers that are queued.
        // Processed buffers.
        ALint processed_buffer_count = 0;
        alGetSourcei(source->id, AL_BUFFERS_PROCESSED, &processed_buffer_count);
        for (i32 i = 0; i < processed_buffer_count; ++i) {
            ALuint buffer_id = 0;
            alSourceUnqueueBuffers(source->id, 1, &buffer_id);
            KTRACE("Play - unqueued processed buffer %u", buffer_id);
            openal_backend_check_error();
        }

        // Queued buffers.
        ALint queued_buffer_count = 0;
        alGetSourcei(source->id, AL_BUFFERS_QUEUED, &queued_buffer_count);
        for (i32 i = 0; i < queued_buffer_count; ++i) {
            ALuint buffer_id = INVALID_ID;
            alSourceUnqueueBuffers(source->id, 1, &buffer_id);
            KTRACE("Play - unqueued queued buffer %u", buffer_id);
            openal_backend_check_error();
        }

        // Queue up sound buffer.
        ALuint* bids = 0;
        if (data->channels == 2 && audio_space == KAUDIO_SPACE_3D) {
            // If stereo sound but wanting to play 3d, use the mono buffer.
            alSourcei(source->id, AL_SOURCE_RELATIVE, AL_FALSE);
            bids = &data->mono_buffer;
        } else {
            if (data->channels == 2) {
                // stereo, but using 2d sound. Play as normal.
                alSourcei(source->id, AL_SOURCE_RELATIVE, AL_FALSE);
                bids = &data->buffer;
            } else if (audio_space == KAUDIO_SPACE_3D) {
                // Mono sound, but want to play 3D. Play as normal.
                alSourcei(source->id, AL_SOURCE_RELATIVE, AL_FALSE);
                bids = &data->buffer;
            } else {
                // Mono sound, but play 2D. Set source relative, making the source
                // align with the listener, effectively making the sound 2d.
                alSourcei(source->id, AL_SOURCE_RELATIVE, AL_TRUE);
                bids = &data->buffer;
            }
        }
        alSourceQueueBuffers(source->id, 1, bids);
        openal_backend_check_error();
    }

    // Assign current, set flags, play, etc.
    source->current = audio;
    alSourcePlay(source->id);
    kmutex_unlock(&source->data_mutex);

    return true;
}

b8 openal_backend_channel_stop(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        kaudio_plugin_source* source = &state->sources[channel_id];

        alSourceStop(source->id);

        // Detach all buffers.
        alSourcei(source->id, AL_BUFFER, 0);
        openal_backend_check_error();

        // Rewind.
        alSourceRewind(source->id);

        source->current = 0;

        return true;
    }
    return false;
}

b8 openal_backend_channel_pause(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        // Trigger a pause if the source is currently playing.
        kaudio_plugin_source* source = &state->sources[channel_id];
        ALint source_state;
        alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
        if (source_state == AL_PLAYING) {
            alSourcePause(source->id);
        }

        return true;
    }
    return false;
}

b8 openal_backend_channel_resume(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        // Trigger a resume if the source is currently paused.
        kaudio_plugin_source* source = &state->sources[channel_id];
        ALint source_state;
        alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
        if (source_state == AL_PAUSED) {
            alSourcePlay(source->id);
        }

        return true;
    }
    return false;
}

b8 openal_backend_channel_is_playing(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        ALint source_state;
        alGetSourcei(state->sources[channel_id].id, AL_SOURCE_STATE, &source_state);
        return source_state == AL_PLAYING;
    }
    return false;
}

b8 openal_backend_channel_is_paused(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        ALint source_state;
        alGetSourcei(state->sources[channel_id].id, AL_SOURCE_STATE, &source_state);
        return source_state == AL_PAUSED;
    }
    return false;
}

b8 openal_backend_channel_is_stopped(kaudio_backend_interface* backend, u8 channel_id) {
    if (!backend) {
        return false;
    }

    kaudio_backend_state* state = backend->internal_state;
    if (channel_id_valid(state, channel_id)) {
        ALint source_state;
        alGetSourcei(state->sources[channel_id].id, AL_SOURCE_STATE, &source_state);
        return source_state == AL_STOPPED || source_state == AL_INITIAL;
    }
    return false;
}

static b8 stream_data(kaudio_backend_interface* backend, ALuint buffer, kaudio_space audio_space, kaudio audio) {
    kaudio_backend_state* state = backend->internal_state;

    kaudio_internal_data* data = &state->datas[audio];

    // Figure out how many samples can be taken.
    // TODO: This might be _way_ too much between chunk size and samples (maybe samples left * channels?)
    u64 sample_count = KMIN(data->total_samples_left, state->chunk_size);

    // 0 means the end of the file has been reached, and either the stream stops or needs to start over.
    if (sample_count == 0) {
        KTRACE("End of file reached. Returning false.");
        return false;
    }
    openal_backend_check_error();
    // Load the data into the buffer. Just a pointer into the pcm_data at an offset.
    u64 pos = data->total_sample_count - data->total_samples_left;

    // Figure out the format and source.
    i16* data_source = 0;
    ALenum format = data->format;
    if (data->channels == 2) {
        if (audio_space == KAUDIO_SPACE_3D) {
            // Use the mono data.
            format = AL_FORMAT_MONO16;
            data_source = data->mono_pcm_data;
        } else {
            // Stereo is fine
            format = AL_FORMAT_STEREO16;
            data_source = data->pcm_data;
        }
    } else if (data->channels == 1) {
        // Only mono data exists to use, so use it.
        // The front-end should handle positioning the channel so it comes across in stereo (2d)
        format = AL_FORMAT_MONO16;
        data_source = data->mono_pcm_data;
    } else {
        KFATAL("Unsupported channel count %u", data->channels);
    }
    // Get the data offset.
    i16* streamed_data = data_source + pos;
    if (streamed_data) {
        alBufferData(buffer, format, streamed_data, sample_count * sizeof(ALshort), data->sample_rate);
        openal_backend_check_error();
    } else {
        KERROR("Error streaming data. Check logs for more info.");
        return false;
    }

    // Update the samples remaining.
    data->total_samples_left -= sample_count;

    return true;
}

static b8 openal_backend_stream_update(kaudio_backend_interface* backend, kaudio_plugin_source* source) {
    kaudio_backend_state* state = backend->internal_state;

    // It's possible sometimes for this to not be playing, even with buffers queued up.
    // Make sure to handle this case.
    ALint source_state;
    alGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
    if (source_state != AL_PLAYING) {
        KTRACE("Stream update, play needed for source id: %u", source->id);
        alSourcePlay(source->id);
    }

    // Check for processed buffers that can be popped off.
    ALint processed_buffer_count = 0;
    alGetSourcei(source->id, AL_BUFFERS_PROCESSED, &processed_buffer_count);

    while (processed_buffer_count--) {
        ALuint buffer_id = 0;
        alSourceUnqueueBuffers(source->id, 1, &buffer_id);

        // If this returns false, there was nothing further to read (i.e at the end of the file).
        if (!stream_data(backend, buffer_id, source->current_audio_space, source->current)) {
            KTRACE("stream_data returned false");
            b8 done = true;

            kaudio_internal_data* data = &state->datas[source->current];

            // If set to loop, start over at the beginning.
            if (data->is_looping) {
                KTRACE("Internal audio set to loop. Rewinding and starting over.");
                // Loop around.
                data->total_samples_left = data->total_sample_count;
                /* audio->rewind(audio); */
                done = !stream_data(backend, buffer_id, source->current_audio_space, source->current);
            }

            // If not set to loop, the sound is done playing.
            if (done) {
                KTRACE("Sound is done playing.");
                return false;
            }
        }
        // Queue up the next buffer.
        alSourceQueueBuffers(source->id, 1, &buffer_id);
    }

    return true;
}

static u32 source_work_thread(void* params) {
    ksource_work_thread_params* typed_params = params;
    kaudio_backend_interface* backend = typed_params->backend;
    kaudio_backend_state* state = backend->internal_state;
    kaudio_plugin_source* source = typed_params->source;

    // Release this right away since it's no longer needed.
    kfree(params, sizeof(ksource_work_thread_params), MEMORY_TAG_AUDIO);

    KDEBUG("Audio source thread starting...");

    b8 do_break = false;
    while (!do_break) {
        if (!source->data_mutex.internal_data) {
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

        if (source->current != INVALID_KAUDIO) {
            kaudio_internal_data* data = &state->datas[source->current];
            if (data->is_stream) {
                // If currently playing stream, try updating the stream.
                openal_backend_stream_update(backend, source);
            }
        }

        platform_sleep(2);
    }

    KDEBUG("Audio source thread shutting down.");
    return 0;
}

static b8 source_set_defaults(kaudio_backend_interface* backend, kaudio_plugin_source* source, b8 reset_use) {

    // Mark it as not in use.
    if (reset_use) {
        source->current = 0;
    }

    // Set some defaults. FIXME: Define these instead of having magic numbers.
    if (!openal_backend_channel_gain_set(backend, source->id - 1, 1.0f)) {
        KERROR("Failed to set source default gain.");
        return false;
    }
    if (!openal_backend_channel_pitch_set(backend, source->id - 1, 1.0f)) {
        KERROR("Failed to set source default pitch.");
        return false;
    }
    if (!openal_backend_channel_position_set(backend, source->id - 1, vec3_zero())) {
        KERROR("Failed to set source default position.");
        return false;
    }
    if (!openal_backend_channel_looping_set(backend, source->id - 1, false)) {
        KERROR("Failed to set source default looping.");
        return false;
    }

    return true;
}

static b8 openal_backend_channel_create(kaudio_backend_interface* backend, kaudio_plugin_source* out_source) {
    if (!backend || !out_source) {
        KERROR("openal_backend_channel_create requires valid pointers to a plugin and out_source.");
        return false;
    }

    alGenSources((ALuint)1, &out_source->id);
    if (!openal_backend_check_error()) {
        KERROR("Failed to create source.");
        return false;
    }

    if (!source_set_defaults(backend, out_source, true)) {
        KERROR("Failed to set source defaults, and thus failed to create source.");
    }

    // Create the source worker thread's mutex.
    kmutex_create(&out_source->data_mutex);

    char* name = string_format("KohiAudWrkT%2u", out_source->id);

    // Also create the worker thread itself for this source.
    ksource_work_thread_params* params = kallocate(sizeof(ksource_work_thread_params), MEMORY_TAG_AUDIO);
    params->source = out_source;
    params->backend = backend;
    kthread_create(source_work_thread, name, params, true, &out_source->thread);

    string_free(name);

    return true;
}

static void openal_backend_channel_destroy(kaudio_backend_interface* backend, kaudio_plugin_source* source) {
    if (backend && source) {
        alDeleteSources(1, &source->id);
        kzero_memory(source, sizeof(kaudio_plugin_source));
        source->id = INVALID_ID;
    }
}

static void openal_backend_find_playing_sources(kaudio_backend_interface* backend, u32 playing[], u32* count) {
    if (!backend || !count) {
        return;
    }

    kaudio_backend_state* state = backend->internal_state;

    ALint source_state = 0;
    for (u32 i = 0; i < state->max_sources; ++i) {
        alGetSourcei(state->sources[i - 1].id, AL_SOURCE_STATE, &source_state);
        if (source_state == AL_PLAYING) {
            playing[(*count)] = state->sources[i - 1].id;
            (*count)++;
        }
    }
}

static void clear_buffer(kaudio_backend_interface* backend, u32* buf_ptr, u32 amount) {
    if (backend) {

        kaudio_backend_state* state = backend->internal_state;

        for (u32 a = 0; a < amount; ++a) {
            for (u32 i = 0; i < state->buffer_count; ++i) {
                if (buf_ptr[a] == state->buffers[i]) {
                    darray_push(state->free_buffers, i);
                    return;
                }
            }
        }
    }
    KWARN("Buffer could not be cleared.");
}

static u32 openal_backend_find_free_buffer(kaudio_backend_interface* backend) {
    if (backend) {

        kaudio_backend_state* state = backend->internal_state;
        u32 free_count = darray_length(state->free_buffers);

        // If there are no free buffers, attempt to free one first.
        if (free_count == 0) {
            KINFO("openal_backend_find_free_buffer() - no free buffers, attempting to free an existing one.");
            if (!openal_backend_check_error()) {
                return false;
            }

            u32 playing_source_count = 0;
            u32* playing_sources = kallocate(sizeof(u32) * state->max_sources, MEMORY_TAG_ARRAY);
            openal_backend_find_playing_sources(backend, playing_sources, &playing_source_count);
            // Avoid a crash when calling alGetSourcei while checking for freeable buffers. Resumed below.
            for (u32 i = 0; i < playing_source_count; ++i) {
                alSourcePause(state->sources[i - 1].id);
                openal_backend_check_error();
            }

            ALint to_be_freed = 0;
            ALuint buffers_freed = 0;
            for (u32 i = 0; i < state->buffer_count; ++i) {
                // Get number of buffers to be freed for this source.
                alGetSourcei(state->sources[i - 1].id, AL_BUFFERS_PROCESSED, &to_be_freed);
                openal_backend_check_error();
                if (to_be_freed > 0) {
                    // If there are buffers to be freed, free them.
                    openal_backend_check_error();
                    alSourceUnqueueBuffers(state->sources[i - 1].id, to_be_freed, &buffers_freed);
                    openal_backend_check_error();

                    clear_buffer(backend, &buffers_freed, to_be_freed);
                    /* alSourcePlay(state->sources[i - 1].id); */
                }
            }

            // Resume the paused sources.
            for (u32 i = 0; i < playing_source_count; ++i) {
                alSourcePlay(state->sources[i - 1].id);
                openal_backend_check_error();
            }
            kfree(playing_sources, sizeof(u32) * state->max_sources, MEMORY_TAG_ARRAY);
        }

        // Check free count again, this time there must be at least one or there is an error condition.
        free_count = darray_length(state->free_buffers);
        if (free_count < 1) {
            KERROR("Could not find or clear a buffer. This means too many things are being played at once.");
            return INVALID_ID;
        }

        // Nab the first one off the top
        u32 out_buffer_id;
        darray_pop_at(state->free_buffers, 0, &out_buffer_id);

        free_count = darray_length(state->free_buffers);
        KTRACE("Found free buffer id %u", out_buffer_id);
        KDEBUG("There are now %u free buffers remaining.", free_count);
        return out_buffer_id;
    }

    return INVALID_ID;
}

static const char* openal_backend_error_str(ALCenum err) {
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

static b8 openal_backend_check_error(void) {
    ALCenum error = alGetError();
    if (error != AL_NO_ERROR) {
        KERROR("OpenAL error %u: '%s'", error, openal_backend_error_str(error));
        return false;
    }
    return true;
}

static b8 channel_id_valid(kaudio_backend_state* state, u8 channel_id) {
    return state && channel_id < state->max_sources;
}
