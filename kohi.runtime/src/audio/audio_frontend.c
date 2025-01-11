#include "audio_frontend.h"
#include "containers/darray.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "plugins/plugin_types.h"
#include "systems/plugin_system.h"

typedef struct kaudio_system_config {
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

    /**
     * @brief The number of separately-controlled channels used for mixing purposes. Each channel
     * can have its volume independently controlled. Not to be confused with channel_count above.
     */
    u32 audio_channel_count;

    /** @brief The maximum number of audio resources (sounds or music) that can be loaded at once. */
    u32 max_resource_count;

    /** @brief The name of the plugin to be loaded for the audio backend. */
    const char* backend_plugin_name;
} kaudio_system_config;

typedef struct kaudio_channel {
    f32 volume;

    // Pitch, generally left at 1.
    f32 pitch;
    // Position of the sound.
    vec3 position;
    // Indicates if the source is looping.
    b8 looping;
    // Indicates if this souce is in use.
    b8 in_use;

    // Handle into the sound backend/plugin resource array
    khandle resource_handle;
} kaudio_channel;

// The frontend-specific data for an instance of an audio resource.
typedef struct kaudio_resource_instance_handle_data {
    u64 uniqueid;

} kaudio_resource_instance_handle_data;

// The frontend-specific data for an audio resource.
typedef struct kaudio_resource_handle_data {
    u64 uniqueid;

    // A pointer to the underlying audio resource.
    kresource_audio* resource;

    // darray of instances of this resource.
    kaudio_resource_instance_handle_data* instances;
} kaudio_resource_handle_data;

typedef struct kaudio_system_state {
    // TODO: backend interface

    f32 master_volume;

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

    /**
     * @brief The number of separately-controlled channels used for mixing purposes. Each channel
     * can have its volume independently controlled. Not to be confused with channel_count above.
     */
    u32 audio_channel_count;

    // Channels which can play audio.
    kaudio_channel channels[AUDIO_CHANNEL_MAX_COUNT];

    // The max number of audio resources that can be loaded at any time.
    u32 max_resource_count;

    // Array of internal resources for audio data in the system's frontend.
    kaudio_resource_handle_data* resources;

    // The backend plugin.
    kruntime_plugin* plugin;
} kaudio_system_state;

static b8 deserialize_config(const char* config_str, kaudio_system_config* out_config);

b8 kaudio_system_initialize(u64* memory_requirement, void* memory, const char* config_str) {

    *memory_requirement = sizeof(kaudio_system_state);
    if (!memory) {
        return true;
    }

    kaudio_system_state* state = (kaudio_system_state*)memory;

    // Get config.
    kaudio_system_config config = {0};
    if (!deserialize_config(config_str, &config)) {
        KWARN("Failed to parse audio system config. See logs for details. Using reasonable defaults instead.");
        config.audio_channel_count = 8;
        config.backend_plugin_name = "kohi.plugin.audio.openal";
        config.frequency = 44100;
        config.channel_count = 2;
        config.chunk_size = 4096 * 16;
        config.max_resource_count = 32;
    }

    state->chunk_size = config.chunk_size;
    state->channel_count = config.channel_count;
    state->audio_channel_count = config.audio_channel_count;
    state->frequency = config.frequency;
    state->max_resource_count = config.max_resource_count;

    state->resources = KALLOC_TYPE_CARRAY(kaudio_resource_handle_data, state->max_resource_count);
    // Invalidate all entries.
    for (u32 i = 0; i < state->max_resource_count; ++i) {
        kaudio_resource_handle_data* data = &state->resources[i];
        data->resource = 0;
        data->uniqueid = INVALID_ID_U64;
        data->instances = 0;
    }

    // Default volumes for master and all channels to 1.0 (max);
    state->master_volume = 1.0f;
    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        channel->volume = 1.0f;
        // Also set some other reasonable defaults.
        channel->pitch = 1.0f;
        channel->position = vec3_zero();
        channel->in_use = false;
        channel->looping = false;
        channel->resource_handle = khandle_invalid();
    }

    // Load the plugin.
    state->plugin = plugin_system_get(engine_systems_get()->plugin_system, config.backend_plugin_name);
    if (!state->plugin) {
        KERROR("Failed to load required audio backend plugin '%s'. See logs for details. Audio system init failed.", config.backend_plugin_name);
        return false;
    }

    // TODO: Get the interface to the backend, then initialize()

    //
    // TODO: setup console commands to load/play sounds/music, etc.
}

void kaudio_system_shutdown(struct kaudio_system_state* state) {
    // TODO: release all sources, device, etc.
}

b8 kaudio_system_update(void* state, struct frame_data* p_frame_data) {
}

b8 kaudio_system_listener_orientation_set(struct kaudio_system_state* state, vec3 position, vec3 forward, vec3 up) {
}

void kaudio_system_master_volume_set(struct kaudio_system_state* state, f32 volume) {
}
f32 kaudio_system_master_volume_get(struct kaudio_system_state* state) {
}

void kaudio_system_channel_volume_set(struct kaudio_system_state* state, u8 channel_index, f32 volume) {
}
f32 kaudio_system_channel_volume_get(struct kaudio_system_state* state, u8 channel_index) {
}

b8 kaudio_sound_effect_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, ksound_effect_instance* out_instance) {}
void kaudio_sound_effect_release(struct kaudio_system_state* state, ksound_effect_instance* instance) {}
b8 kaudio_sound_play(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_pause(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_resume(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_stop(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_is_playing(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_is_valid(struct kaudio_system_state* state, ksound_effect_instance instance) {}
f32 kaudio_sound_pan_get(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_pan_set(struct kaudio_system_state* state, ksound_effect_instance instance, f32 pan) {}
b8 kaudio_sound_pitch_get(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_pitch_set(struct kaudio_system_state* state, ksound_effect_instance instance, f32 pitch) {}
b8 kaudio_sound_volume_get(struct kaudio_system_state* state, ksound_effect_instance instance) {}
b8 kaudio_sound_volume_set(struct kaudio_system_state* state, ksound_effect_instance instance, f32 volume) {}

b8 kaudio_music_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, kmusic_instance* out_instance) {}
void kaudio_music_release(struct kaudio_system_state* state, kmusic_instance* instance) {}
b8 kaudio_music_play(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_pause(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_resume(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_stop(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_seek(struct kaudio_system_state* state, kmusic_instance instance, f32 seconds) {}
f32 kaudio_music_time_length_get(struct kaudio_system_state* state, kmusic_instance instance) {}
f32 kaudio_music_time_played_get(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_is_playing(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_is_valid(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_looping_get(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_looping_set(struct kaudio_system_state* state, kmusic_instance instance, b8 looping) {}
// 0=left, 0.5=center, 1.0=right
f32 kaudio_music_pan_get(struct kaudio_system_state* state, kmusic_instance instance) {}
// 0=left, 0.5=center, 1.0=right
b8 kaudio_music_pan_set(struct kaudio_system_state* state, kmusic_instance instance, f32 pan) {}
b8 kaudio_music_pitch_get(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_pitch_set(struct kaudio_system_state* state, kmusic_instance instance, f32 pitch) {}
b8 kaudio_music_volume_get(struct kaudio_system_state* state, kmusic_instance instance) {}
b8 kaudio_music_volume_set(struct kaudio_system_state* state, kmusic_instance instance, f32 volume) {
}

static b8 deserialize_config(const char* config_str, kaudio_system_config* out_config) {
    if (!config_str || !out_config) {
        KERROR("audio_system_deserialize_config requires a valid pointer to out_config and config_str");
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(config_str, &tree)) {
        KERROR("Failed to parse audio system config.");
        return false;
    }

    // backend_plugin_name is required.
    if (!kson_object_property_value_get_string(&tree.root, "backend_plugin_name", &out_config->backend_plugin_name)) {
        KERROR("audio_system_deserialize_config: config does not contain backend_plugin_name, which is required.");
        return false;
    }

    i64 audio_channel_count = 0;
    if (!kson_object_property_value_get_int(&tree.root, "audio_channel_count", &audio_channel_count)) {
        audio_channel_count = 8;
    }
    if (audio_channel_count < 4) {
        KWARN("Invalid audio system config - audio_channel_count must be at least 4. Defaulting to 4.");
        audio_channel_count = 4;
    }
    out_config->audio_channel_count = audio_channel_count;

    i64 max_resource_count = 0;
    if (!kson_object_property_value_get_int(&tree.root, "max_resource_count", &max_resource_count)) {
        max_resource_count = 32;
    }
    if (max_resource_count < 32) {
        KWARN("Invalid audio system config - max_resource_count must be at least 32. Defaulting to 32.");
        max_resource_count = 32;
    }
    out_config->max_resource_count = max_resource_count;

    // FIXME: This is currently unused.
    i64 frequency;
    if (!kson_object_property_value_get_int(&tree.root, "frequency", &frequency)) {
        frequency = 44100;
    }
    out_config->frequency = frequency;

    i64 channel_count;
    if (!kson_object_property_value_get_int(&tree.root, "channel_count", &channel_count)) {
        channel_count = 2;
    }
    if (channel_count < 1) {
        channel_count = 1;
    } else if (channel_count > 2) {
        channel_count = 2;
    }
    out_config->channel_count = channel_count;

    i64 chunk_size;
    if (!kson_object_property_value_get_int(&tree.root, "chunk_size", &chunk_size)) {
        chunk_size = 8;
    }
    if (chunk_size == 0) {
        chunk_size = 4096 * 16;
    }
    out_config->chunk_size = chunk_size;

    kson_tree_cleanup(&tree);

    return true;
}
