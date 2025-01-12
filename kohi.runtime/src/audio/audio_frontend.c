#include "audio_frontend.h"
#include "assets/kasset_types.h"
#include "audio/kaudio_types.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "plugins/plugin_types.h"
#include "strings/kname.h"
#include "systems/kresource_system.h"
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

// The frontend-specific data for an audio resource.
typedef struct kaudio_resource_handle_data {
    // The unique id matching an associated handle. INVALID_ID_U64 means this slot is unused.
    u64 uniqueid;

    // A pointer to the underlying audio resource.
    kresource_audio* resource;

    // Indicates if the audio should be streamed in small bits (large files) or loaded all at once (small files)
    b8 is_streaming;

    // Range: [0.5f - 2.0f] Default: 1.0f
    f32 pitch;

    // Range: 0-1
    f32 volume;

    b8 looping;

} kaudio_resource_handle_data;

typedef struct kaudio_channel {
    f32 volume;

    // Pitch, generally left at 1.
    f32 pitch;
    // Position of the sound.
    vec3 position;
    // Indicates if the source is looping.
    b8 looping;
    // A pointer to the currently bound resource handle data, if in  use; otherwise 0/null (i.e. not in use)
    kaudio_resource_handle_data* bound_data;
} kaudio_channel;

typedef struct kaudio_system_state {
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

    // Pointer to the backend interface.
    kaudio_backend_interface* backend;
} kaudio_system_state;

typedef struct audio_asset_request_listener {
    kaudio_system_state* state;
    khandle audio;
} audio_asset_request_listener;

static b8 deserialize_config(const char* config_str, kaudio_system_config* out_config);
static khandle get_new_handle(kaudio_system_state* state);
static void on_audio_asset_loaded(kresource* resource, void* listener);
static b8 handle_is_valid_and_pristine(kaudio_system_state* state, khandle handle);

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
        // This marks it as unused.
        data->uniqueid = INVALID_ID_U64;
    }

    // Default volumes for master and all channels to 1.0 (max);
    state->master_volume = 1.0f;
    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        channel->volume = 1.0f;
        // Also set some other reasonable defaults.
        channel->pitch = 1.0f;
        channel->position = vec3_zero();
        channel->looping = false;
        channel->bound_data = 0;
    }

    // Load the plugin.
    state->plugin = plugin_system_get(engine_systems_get()->plugin_system, config.backend_plugin_name);
    if (!state->plugin) {
        KERROR("Failed to load required audio backend plugin '%s'. See logs for details. Audio system init failed.", config.backend_plugin_name);
        return false;
    }

    state->backend = state->plugin->plugin_state;

    // TODO: setup console commands to load/play sounds/music, etc.

    kaudio_backend_config backend_config = {0};
    backend_config.frequency = config.frequency;
    backend_config.chunk_size = config.chunk_size;
    backend_config.channel_count = config.channel_count;
    backend_config.max_resource_count = config.max_resource_count;
    backend_config.audio_channel_count = config.audio_channel_count;
    return state->backend->initialize(state->backend, &backend_config);
}

void kaudio_system_shutdown(struct kaudio_system_state* state) {
    if (state) {
        // TODO: release all sources, device, etc.

        state->backend->shutdown(state->backend);
    }
}

b8 kaudio_system_update(struct kaudio_system_state* state, struct frame_data* p_frame_data) {
    if (state) {

        // Adjust each channel's properties based on what is bound to them (if anything).
        for (u32 i = 0; i < state->audio_channel_count; ++i) {
            kaudio_channel* channel = &state->channels[i];
            if (channel->bound_data) {
                // Volume
                f32 mixed_volume = channel->bound_data->volume * channel->volume * state->master_volume;
                state->backend->channel_gain_set(state->backend, i, mixed_volume);
                // Pitch
                f32 mixed_pitch = channel->pitch * channel->bound_data->pitch;
                state->backend->channel_pitch_set(state->backend, i, mixed_pitch);
                // Looping setting
                state->backend->channel_looping_set(state->backend, i, channel->bound_data->looping);

                // Position is only applied for mono sounds, because only those can be spatial/use position.
                if (channel->bound_data->resource->channels == 1) {
                    state->backend->channel_position_set(state->backend, i, channel->position);
                }
            }
        }

        return state->backend->update(state->backend, p_frame_data);
    }
    return false;
}

b8 kaudio_system_listener_orientation_set(struct kaudio_system_state* state, vec3 position, vec3 forward, vec3 up) {
    if (!state) {
        return false;
    }

    state->backend->listener_position_set(state->backend, position);
    state->backend->listener_orientation_set(state->backend, forward, up);
    return true;
}

void kaudio_system_master_volume_set(struct kaudio_system_state* state, f32 volume) {
    if (state) {
        state->master_volume = KCLAMP(volume, 0.0f, 1.0f);
    }
}

f32 kaudio_system_master_volume_get(struct kaudio_system_state* state) {
    if (state) {
        return state->master_volume;
    }
    return 0.0f;
}

void kaudio_system_channel_volume_set(struct kaudio_system_state* state, u8 channel_index, f32 volume) {
    if (state) {
        if (channel_index >= state->audio_channel_count) {
            KERROR("kaudio_system_channel_volume_set - channel_index %u is out of range (0-%u). Nothing will be done.", channel_index, state->audio_channel_count);
            return;
        }

        // Clamp volume to a sane range.
        state->channels[channel_index].volume = KCLAMP(volume, 0.0f, 1.0f);
    }
}

f32 kaudio_system_channel_volume_get(struct kaudio_system_state* state, u8 channel_index) {
    if (state) {
        if (channel_index >= state->audio_channel_count) {
            KERROR("kaudio_system_channel_volume_get - channel_index %u is out of range (0-%u). 0 will be returned.", channel_index, state->audio_channel_count);
            return 0.0f;
        }
        return state->channels[channel_index].volume;
    }
    return 0.0f;
}

b8 kaudio_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, b8 is_streaming, khandle* out_audio) {
    if (!state) {
        return false;
    }

    // Get/create a new handle for the resource.
    *out_audio = get_new_handle(state);

    // Mark the slot as "in-use" by syncing the uniqueid.
    kaudio_resource_handle_data* data = &state->resources[out_audio->handle_index];
    data->uniqueid = out_audio->unique_id.uniqueid;
    data->is_streaming = is_streaming;
    // Set reasonable defaults.
    data->looping = is_streaming ? true : false;
    data->pitch = 1.0f;

    // Listener for the request.
    audio_asset_request_listener* listener = KALLOC_TYPE(audio_asset_request_listener, MEMORY_TAG_RESOURCE);
    listener->state = state;
    listener->audio = *out_audio;

    // Request the resource. If it already exists it will return immediately and be in a ready/loaded state.
    // If not, it will be handled asynchronously. Either way, it'll go through the same callback.
    kresource_audio_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_AUDIO;
    request.base.assets = array_kresource_asset_info_create(1);
    request.base.user_callback = on_audio_asset_loaded;
    request.base.listener_inst = listener;
    kresource_asset_info* asset = &request.base.assets.data[0];
    asset->type = KASSET_TYPE_AUDIO;
    asset->asset_name = resource_name;
    asset->package_name = package_name;
    asset->watch_for_hot_reload = false; // Hot-reloading not supported for audio.
    return kresource_system_request(engine_systems_get()->kresource_state, resource_name, (kresource_request_info*)&request);
}

void kaudio_release(struct kaudio_system_state* state, khandle* audio) {
    if (state && audio) {
        if (!handle_is_valid_and_pristine(state, *audio)) {
            KERROR("kaudio_release was passed a handle that is either invalid or stale. Nothing to be done.");
            return;
        }

        kaudio_resource_handle_data* data = &state->resources[audio->handle_index];

        // Release from backend.
        state->backend->resource_unload(state->backend, *audio);

        // Release the resource.
        kresource_system_release(engine_systems_get()->kresource_state, data->resource->base.name);

        // Reset the handle data and make the slot available for use.
        data->is_streaming = false;
        data->resource = 0;
        data->uniqueid = INVALID_ID_U64;
    }
}

b8 kaudio_play(struct kaudio_system_state* state, khandle audio, u8 channel_index) {
    if (!handle_is_valid_and_pristine(state, audio)) {
        KERROR("%s was called with an invalid or stale handle.", __FUNCTION__);
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s was called with an out of bounds channel_index of %hhu (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->backend->channel_play_resource(state->backend, audio, channel_index);
}

b8 kaudio_is_valid(struct kaudio_system_state* state, khandle audio) {
    if (!handle_is_valid_and_pristine(state, audio)) {
        KERROR("%s was called with an invalid or stale handle.", __FUNCTION__);
        return false;
    }

    kaudio_resource_handle_data* data = &state->resources[audio.handle_index];
    return data->uniqueid != INVALID_ID_U64 && data->resource && data->resource->base.state == KRESOURCE_STATE_LOADED;
}

f32 kaudio_pitch_get(struct kaudio_system_state* state, khandle audio) {
    if (!handle_is_valid_and_pristine(state, audio)) {
        KERROR("%s was called with an invalid or stale handle.", __FUNCTION__);
        return 0.0f;
    }

    kaudio_resource_handle_data* data = &state->resources[audio.handle_index];
    return data->pitch;
}

b8 kaudio_pitch_set(struct kaudio_system_state* state, khandle audio, f32 pitch) {
    if (!handle_is_valid_and_pristine(state, audio)) {
        KERROR("%s was called with an invalid or stale handle.", __FUNCTION__);
        return false;
    }

    kaudio_resource_handle_data* data = &state->resources[audio.handle_index];
    // Clamp to a valid range.
    data->pitch = KCLAMP(pitch, 0.5f, 2.0f);
    return true;
}

f32 kaudio_volume_get(struct kaudio_system_state* state, khandle audio) {
    if (!handle_is_valid_and_pristine(state, audio)) {
        KERROR("%s was called with an invalid or stale handle.", __FUNCTION__);
        return 0.0f;
    }

    kaudio_resource_handle_data* data = &state->resources[audio.handle_index];
    return data->volume;
}

b8 kaudio_volume_set(struct kaudio_system_state* state, khandle audio, f32 volume) {
    if (!handle_is_valid_and_pristine(state, audio)) {
        KERROR("%s was called with an invalid or stale handle.", __FUNCTION__);
        return false;
    }

    kaudio_resource_handle_data* data = &state->resources[audio.handle_index];
    // Clamp to a valid range.
    data->volume = KCLAMP(volume, 0.0f, 1.0f);
    return true;
}

b8 kaudio_channel_play(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    // Attempt to play the already bound resource if one exists. Otherwise this fails.
    if (state->channels[channel_index].bound_data) {
        state->backend->channel_play(state->backend, channel_index);
        return true;
    }

    return false;
}

b8 kaudio_channel_pause(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }
    return state->backend->channel_pause(state->backend, channel_index);
}

b8 kaudio_channel_resume(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }
    return state->backend->channel_resume(state->backend, channel_index);
}

b8 kaudio_channel_stop(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }
    return state->backend->channel_stop(state->backend, channel_index);
}

b8 kaudio_channel_is_playing(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }
    return state->backend->channel_is_playing(state->backend, channel_index);
}

b8 kaudio_channel_is_paused(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }
    return state->backend->channel_is_paused(state->backend, channel_index);
}

b8 kaudio_channel_is_stopped(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }
    return state->backend->channel_is_stopped(state->backend, channel_index);
}

b8 kaudio_channel_looping_get(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->channels[channel_index].looping;
}

b8 kaudio_channel_looping_set(struct kaudio_system_state* state, u8 channel_index, b8 looping) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    state->channels[channel_index].looping = looping;
    return true;
}

f32 kaudio_channel_volume_get(struct kaudio_system_state* state, u8 channel_index) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->channels[channel_index].volume;
}

b8 kaudio_channel_volume_set(struct kaudio_system_state* state, u8 channel_index, f32 volume) {
    if (!state) {
        return false;
    }
    if (channel_index >= state->audio_channel_count) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    state->channels[channel_index].volume = volume;
    return true;
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

static khandle get_new_handle(kaudio_system_state* state) {
    for (u32 i = 0; i < state->max_resource_count; ++i) {
        kaudio_resource_handle_data* data = &state->resources[i];
        if (data->uniqueid == INVALID_ID_U64) {
            // Found one.
            khandle h = khandle_create(i);
            data->uniqueid = h.unique_id.uniqueid;
            return h;
        }
    }
    KFATAL("No more room to allocate a new handle for a sound. Expand the max_resource_count in configuration to load more at once.");
    return khandle_invalid();
}

static void on_audio_asset_loaded(kresource* resource, void* listener) {
    audio_asset_request_listener* listener_inst = listener;
    KTRACE("Audio resource loaded: '%s'.", kname_string_get(resource->name));

    kaudio_resource_handle_data* data = &listener_inst->state->resources[listener_inst->audio.handle_index];
    data->resource = (kresource_audio*)resource;

    // Send over to the backend to be loaded.
    if (!listener_inst->state->backend->resource_load(listener_inst->state->backend, data->resource, data->is_streaming, listener_inst->audio)) {
        KERROR("Failed to load audio resource into audio system backend. Resource will be released and handle unusable.");

        kresource_system_release(engine_systems_get()->kresource_state, resource->name);
        data->is_streaming = false;
        data->resource = 0;
        data->uniqueid = INVALID_ID_U64;
    }

    // Cleanup the listener.
    KFREE_TYPE(listener, audio_asset_request_listener, MEMORY_TAG_RESOURCE);
}

static b8 handle_is_valid_and_pristine(kaudio_system_state* state, khandle handle) {
    return state && khandle_is_valid(handle) && handle.handle_index < state->max_resource_count && khandle_is_pristine(handle, state->resources[handle.handle_index].uniqueid);
}
