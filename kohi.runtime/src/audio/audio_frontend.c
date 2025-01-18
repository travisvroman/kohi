#include "audio_frontend.h"

#include <containers/darray.h>
#include <core_audio_types.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <strings/kname.h>
#include <utils/audio_utils.h>

#include "assets/kasset_types.h"
#include "audio/kaudio_types.h"
#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "plugins/plugin_types.h"
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

typedef struct kaudio_resource_instance_data {
    // The unique id matching an associated handle. INVALID_ID_U64 means this slot is unused.
    u64 uniqueid;

    // Range: [0.5f - 2.0f] Default: 1.0f
    f32 pitch;

    // Range: 0-1
    f32 volume;

    // Position of the sound.
    vec3 position;

    // Indicates if the sound loops.
    b8 looping;

    // The radius around the position where the sound plays at full volume.
    f32 inner_radius;

    // The max distance from the position where the sound is still audible.
    f32 outer_radius;

    // The rate of falloff/how quickly the sound drops in volume as it is moved away from. Only used in exponential attenuation; otherwise ignored.
    f32 falloff;

    // The model to use for falloff of sound as the listener moves away.
    kaudio_attenuation_model attenuation_model;

    // The space in which the sound exists.
    kaudio_space audio_space;

    // A flag set when a play is requested. Remains on until the asset is valid and
    // a play kicks off or if stopped.
    b8 trigger_play;
} kaudio_resource_instance_data;

// The frontend-specific data for an audio resource.
typedef struct kaudio_resource_handle_data {
    // The unique id matching an associated handle. INVALID_ID_U64 means this slot is unused.
    u64 uniqueid;

    // A pointer to the underlying audio resource.
    kresource_audio* resource;

    kname resource_name;
    kname package_name;

    // Indicates if the audio should be streamed in small bits (large files) or loaded all at once (small files)
    b8 is_streaming;

    // darray of instances of this resource.
    kaudio_resource_instance_data* instances;

} kaudio_resource_handle_data;

typedef struct kaudio_channel {
    // The channel index.
    u8 index;
    // The channel volume
    f32 volume;

    // A pointer to the currently bound resource handle data, if in  use; otherwise 0/null (i.e. not in use)
    kaudio_resource_handle_data* bound_resource;
    // A pointer to the currently bound instance handle data, if in  use; otherwise 0/null (i.e. not in use)
    kaudio_resource_instance_data* bound_instance;
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

    vec3 listener_position;
    vec3 listener_up;
    vec3 listener_forward;

    // The backend plugin.
    kruntime_plugin* plugin;

    // Pointer to the backend interface.
    kaudio_backend_interface* backend;
} kaudio_system_state;

typedef struct audio_asset_request_listener {
    kaudio_system_state* state;
    audio_instance* instance;
} audio_asset_request_listener;

static b8 deserialize_config(const char* config_str, kaudio_system_config* out_config);
static khandle get_base_handle(kaudio_system_state* state, kname resource_name, kname package_name);
static void on_audio_asset_loaded(kresource* resource, void* listener);
static b8 base_resource_handle_is_valid_and_pristine(kaudio_system_state* state, khandle handle);
static b8 instance_handle_is_valid_and_pristine(kaudio_system_state* state, kaudio_resource_handle_data* base, khandle handle);
static kaudio_resource_handle_data* get_base(kaudio_system_state* state, khandle base_resource);
static kaudio_resource_instance_data* get_instance(kaudio_system_state* state, kaudio_resource_handle_data* base, khandle instance);
static u32 get_active_instance_count(kaudio_resource_handle_data* base);
static kaudio_channel* get_channel(kaudio_system_state* state, i8 channel_index);

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
    for (i8 i = 0; i < (i8)state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        channel->index = i;
        channel->volume = 1.0f;
        // Also set some other reasonable defaults.
        channel->bound_resource = 0;
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

        // Listener updates.
        state->backend->listener_position_set(state->backend, state->listener_position);
        state->backend->listener_orientation_set(state->backend, state->listener_forward, state->listener_up);

        // Adjust each channel's properties based on what is bound to them (if anything).
        for (u32 i = 0; i < state->audio_channel_count; ++i) {
            kaudio_channel* channel = &state->channels[i];
            if (channel->bound_resource && channel->bound_instance) {
                b8 is_valid = channel->bound_resource->resource && channel->bound_resource->uniqueid != INVALID_ID_U64 && channel->bound_resource->resource->base.state == KRESOURCE_STATE_LOADED;
                kaudio_resource_instance_data* instance = channel->bound_instance;

                // If a play has been triggered and the resource is valid/ready for playing, do it.
                if (channel->bound_instance->trigger_play && is_valid) {
                    b8 play_result = state->backend->channel_play_resource(
                        state->backend,
                        channel->bound_resource->resource->internal_resource,
                        channel->bound_instance->audio_space,
                        channel->index);

                    if (!play_result) {
                        KERROR("Failed to play resource on channel index %i", channel->index);
                    } else {
                        // Unset the flag on success.
                        channel->bound_instance->trigger_play = false;
                    }
                }

                // Volume
                f32 gain = 1.0f;
                // Apply the volume at various levels by mixing them.
                f32 mixed_volume = channel->bound_instance->volume * channel->volume * state->master_volume;

                if (channel->bound_instance->audio_space == KAUDIO_SPACE_3D) {
                    // Perform custom attenuation for sounds based on distance and falloff method. This is only done for
                    // mono sounds.
                    f32 distance = vec3_distance(channel->bound_instance->position, state->listener_position);
                    gain = calculate_spatial_gain(distance, instance->inner_radius, instance->outer_radius, instance->falloff, instance->attenuation_model);

                    state->backend->channel_position_set(state->backend, i, channel->bound_instance->position);
                } else {
                    // Treat as 2D, even if mono, by syncing the position of the sound/channel with the listener.
                    state->backend->channel_position_set(state->backend, i, state->listener_position);
                    // NOTE: gain is left at 1.0 here, effectively meaning "zero-distance"
                }

                // Apply the mixed volume
                gain *= mixed_volume;

                state->backend->channel_gain_set(state->backend, i, gain);

                // Pitch
                state->backend->channel_pitch_set(state->backend, i, channel->bound_instance->pitch);
                // Looping setting
                b8 looping = channel->bound_instance->looping;
                if (channel->bound_resource->is_streaming) {
                    // Audio channels for streams should never loop directly, but are checked internally instead.
                    // Always force these to be false for streams.
                    looping = false;
                }
                state->backend->channel_looping_set(state->backend, i, looping);

                // Position is only applied for mono sounds, because only those can be spatial/use position.
                if (channel->bound_resource->resource->channels == 1) {
                    state->backend->channel_position_set(state->backend, i, channel->bound_instance->position);
                }
            }
        }

        return state->backend->update(state->backend, p_frame_data);
    }
    return false;
}

void kaudio_system_listener_orientation_set(struct kaudio_system_state* state, vec3 position, vec3 forward, vec3 up) {
    if (state) {
        state->listener_up = up;
        state->listener_forward = forward;
        state->listener_position = position;
    }
}

void kaudio_master_volume_set(struct kaudio_system_state* state, f32 volume) {
    if (state) {
        state->master_volume = KCLAMP(volume, 0.0f, 1.0f);
    }
}

f32 kaudio_master_volume_get(struct kaudio_system_state* state) {
    if (state) {
        return state->master_volume;
    }
    return 0.0f;
}

b8 kaudio_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, b8 is_streaming, kaudio_space audio_space, audio_instance* out_audio_instance) {
    if (!state) {
        return false;
    }

    // Get/create a new handle for the resource.
    out_audio_instance->base_resource = get_base_handle(state, resource_name, package_name);
    kaudio_resource_handle_data* data = &state->resources[out_audio_instance->base_resource.handle_index];
    if (!data->resource) {
        // New handle was created, need to request resource.
        data->resource_name = resource_name;
        data->package_name = package_name;

        data->is_streaming = is_streaming;

        // Listener for the request.
        audio_asset_request_listener* listener = KALLOC_TYPE(audio_asset_request_listener, MEMORY_TAG_RESOURCE);
        listener->state = state;
        listener->instance = out_audio_instance;

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
        if (!kresource_system_request(engine_systems_get()->kresource_state, resource_name, (kresource_request_info*)&request)) {
            KERROR("Failed to request audio resource. See logs for details.");
            kfree(listener, sizeof(audio_asset_request_listener), MEMORY_TAG_RESOURCE);
            return false;
        }

        // Create the darray for the instances.
        data->instances = darray_create(kaudio_resource_instance_data);
    }

    // Setup instance
    // Check to see if there is a free instance slot first. Otherwise push a new one.
    u32 resource_index = INVALID_ID;
    u32 instance_count = darray_length(data->instances);
    for (u32 i = 0; i < instance_count; ++i) {
        kaudio_resource_instance_data* instance = &data->instances[i];
        if (instance->uniqueid == INVALID_ID_U64) {
            // available
            instance_count = i;
            break;
        }
    }
    // Push a new one one was not found.
    if (resource_index == INVALID_ID) {
        darray_push(data->instances, (kaudio_resource_instance_data){0});
        resource_index = instance_count;
    }

    kaudio_resource_instance_data* instance = &data->instances[resource_index];
    out_audio_instance->instance = khandle_create(resource_index);
    instance->uniqueid = out_audio_instance->instance.unique_id.uniqueid;

    // Set reasonable defaults for an instance.
    instance->looping = is_streaming; // Streaming sounds automatically loop.
    instance->pitch = AUDIO_PITCH_DEFAULT;
    instance->volume = AUDIO_VOLUME_DEFAULT;
    instance->position = vec3_zero();
    instance->inner_radius = AUDIO_INNER_RADIUS_DEFAULT;
    instance->outer_radius = AUDIO_OUTER_RADIUS_DEFAULT;
    instance->falloff = AUDIO_FALLOFF_DEFAULT;
    // Set the instance's audio space accordingly.
    instance->audio_space = audio_space;

    return true;
}

void kaudio_release(struct kaudio_system_state* state, audio_instance* instance) {
    if (state && instance) {
        // Check both instance and base handle
        kaudio_resource_handle_data* base_resource = get_base(state, instance->base_resource);
        if (!base_resource) {
            KERROR("kaudio_release was passed a base resource handle that is either invalid or stale. Nothing to be done.");
            return;
        }
        kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance->instance);
        if (!instance_data) {
            KERROR("kaudio_release was passed an instance resource handle that is either invalid or stale. Nothing to be done.");
            return;
        }

        // Invalidate the instance data.
        kzero_memory(instance_data, sizeof(kaudio_resource_instance_data));
        instance_data->uniqueid = INVALID_ID_U64;

        // Invalidate the handles.
        khandle_invalidate(&instance->base_resource);
        khandle_invalidate(&instance->instance);

        // See how many active instances there are left. If none, release.
        u32 active_instance_count = get_active_instance_count(base_resource);
        if (!active_instance_count) {
            KTRACE("Audio resource '%s' has no more instances and will be released.", kname_string_get(base_resource->resource->base.name));

            // Release from backend.
            state->backend->resource_unload(state->backend, instance->base_resource);

            // Release the resource.
            kresource_system_release(engine_systems_get()->kresource_state, base_resource->resource->base.name);

            // Release instance array.
            darray_destroy(base_resource->instances);

            // Reset the handle data and make the slot available for use.
            kzero_memory(base_resource, sizeof(kaudio_resource_handle_data));
            base_resource->uniqueid = INVALID_ID_U64;
        }
    }
}

b8 kaudio_play(struct kaudio_system_state* state, audio_instance instance, i8 channel_index) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        if (channel_index >= 0) {
            KERROR("%s was called with an out of bounds channel_index of %hhu (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        }
        return false;
    }

    // Bind the base resource.
    channel->bound_resource = base_resource;
    channel->bound_instance = instance_data;

    // Trigger a play on the next update if/when the bound resource is valid for playing.
    instance_data->trigger_play = true;

    // NOTE: deliberately not playing here as it's possible the sound isn't ready yet.
    return true;
}

b8 kaudio_stop(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        if (channel->bound_resource == base_resource && channel->bound_instance == instance_data) {
            // Found the channel it's bound to, stop.
            return kaudio_channel_stop(state, i);
        }
    }

    // Return false if the audio wasn't bound to any channel.
    return false;
}

b8 kaudio_pause(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        if (channel->bound_resource == base_resource && channel->bound_instance == instance_data) {
            // Found the channel it's bound to, stop.
            return kaudio_channel_pause(state, i);
        }
    }

    // Return false if the audio wasn't bound to any channel.
    return false;
}

b8 kaudio_resume(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        if (channel->bound_resource == base_resource && channel->bound_instance == instance_data) {
            // Found the channel it's bound to, stop.
            return kaudio_channel_resume(state, i);
        }
    }

    // Return false if the audio wasn't bound to any channel.
    return false;
}

b8 kaudio_is_valid(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }

    return base_resource->uniqueid != INVALID_ID_U64 && base_resource->resource && base_resource->resource->base.state == KRESOURCE_STATE_LOADED;
}

f32 kaudio_pitch_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }
    return instance_data->pitch;
}

b8 kaudio_pitch_set(struct kaudio_system_state* state, audio_instance instance, f32 pitch) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }
    // Clamp to a valid range.
    instance_data->pitch = KCLAMP(pitch, AUDIO_PITCH_MIN, AUDIO_PITCH_MAX);
    return true;
}

f32 kaudio_volume_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    return instance_data->volume;
}

b8 kaudio_volume_set(struct kaudio_system_state* state, audio_instance instance, f32 volume) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    // Clamp to a valid range.
    instance_data->volume = KCLAMP(volume, AUDIO_VOLUME_MIN, AUDIO_VOLUME_MAX);
    return true;
}

b8 kaudio_looping_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    return instance_data->looping;
}

b8 kaudio_looping_set(struct kaudio_system_state* state, audio_instance instance, b8 looping) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    instance_data->looping = looping;
    return true;
}

vec3 kaudio_position_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return vec3_zero();
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return vec3_zero();
    }

    return instance_data->position;
}

b8 kaudio_position_set(struct kaudio_system_state* state, audio_instance instance, vec3 position) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    instance_data->position = position;
    return true;
}

f32 kaudio_inner_radius_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return 0;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return 0;
    }

    return instance_data->inner_radius;
}

b8 kaudio_inner_radius_set(struct kaudio_system_state* state, audio_instance instance, f32 inner_radius) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    instance_data->inner_radius = KCLAMP(inner_radius, AUDIO_INNER_RADIUS_MIN, AUDIO_INNER_RADIUS_MAX);
    return true;
}

f32 kaudio_outer_radius_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return 0;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return 0;
    }

    return instance_data->outer_radius;
}

b8 kaudio_outer_radius_set(struct kaudio_system_state* state, audio_instance instance, f32 outer_radius) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    instance_data->outer_radius = KCLAMP(outer_radius, AUDIO_OUTER_RADIUS_MIN, AUDIO_OUTER_RADIUS_MAX);
    return true;
}

f32 kaudio_falloff_get(struct kaudio_system_state* state, audio_instance instance) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return 0;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return 0;
    }

    return instance_data->falloff;
}

b8 kaudio_falloff_set(struct kaudio_system_state* state, audio_instance instance, f32 falloff) {
    kaudio_resource_handle_data* base_resource = get_base(state, instance.base_resource);
    if (!base_resource) {
        KERROR("%s was called with an invalid or stale base_resource handle.", __FUNCTION__);
        return false;
    }
    kaudio_resource_instance_data* instance_data = get_instance(state, base_resource, instance.instance);
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance handle.", __FUNCTION__);
        return false;
    }

    instance_data->falloff = KCLAMP(falloff, AUDIO_FALLOFF_MIN, AUDIO_FALLOFF_MAX);
    return true;
}

b8 kaudio_channel_play(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    // Attempt to play the already bound resource if one exists. Otherwise this fails.
    if (channel->bound_resource) {
        return state->backend->channel_play(state->backend, channel_index);
    }

    return false;
}

b8 kaudio_channel_pause(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->backend->channel_pause(state->backend, channel_index);
}

b8 kaudio_channel_resume(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->backend->channel_resume(state->backend, channel_index);
}

b8 kaudio_channel_stop(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    // Unbind the resource and instance on stop.
    channel->bound_resource = 0;
    channel->bound_instance = 0;

    return state->backend->channel_stop(state->backend, channel_index);
}

b8 kaudio_channel_is_playing(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->backend->channel_is_playing(state->backend, channel_index);
}

b8 kaudio_channel_is_paused(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->backend->channel_is_paused(state->backend, channel_index);
}

b8 kaudio_channel_is_stopped(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->backend->channel_is_stopped(state->backend, channel_index);
}

f32 kaudio_channel_volume_get(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    return state->channels[channel_index].volume;
}

b8 kaudio_channel_volume_set(struct kaudio_system_state* state, u8 channel_index, f32 volume) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
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
        chunk_size = 4096 * 16;
    }
    if (chunk_size == 0) {
        chunk_size = 4096 * 16;
    }
    out_config->chunk_size = chunk_size;

    kson_tree_cleanup(&tree);

    return true;
}

static khandle get_base_handle(kaudio_system_state* state, kname resource_name, kname package_name) {
    // Search for name/package_name combo and return if found.
    for (u32 i = 0; i < state->max_resource_count; ++i) {
        kaudio_resource_handle_data* data = &state->resources[i];
        if (data->resource_name == resource_name && data->package_name == package_name) {
            // Found a match, return.
            return khandle_create_with_u64_identifier(i, data->uniqueid);
        }
    }

    // Resource with name/package_name combo not found, need to request new.
    for (u32 i = 0; i < state->max_resource_count; ++i) {
        kaudio_resource_handle_data* data = &state->resources[i];
        if (data->uniqueid == INVALID_ID_U64) {
            // Found one.
            khandle h = khandle_create(i);
            // Mark as in use by syncing the uniqueid
            data->uniqueid = h.unique_id.uniqueid;
            data->resource = 0;
            return h;
        }
    }
    KFATAL("No more room to allocate a new handle for a sound. Expand the max_resource_count in configuration to load more at once.");
    return khandle_invalid();
}

static void on_audio_asset_loaded(kresource* resource, void* listener) {
    audio_asset_request_listener* listener_inst = listener;
    KTRACE("Audio resource loaded: '%s'.", kname_string_get(resource->name));

    kaudio_resource_handle_data* data = get_base(listener_inst->state, listener_inst->instance->base_resource);
    if (!data) {
        KFATAL("Data handle is invalid during audio asset load completion. Check application logic.");
    } else {
        data->resource = (kresource_audio*)resource;
        // Sync the resource's "internal" handle to the base resource handle we track in this system.
        data->resource->internal_resource = listener_inst->instance->base_resource;

        // Send over to the backend to be loaded.
        if (!listener_inst->state->backend->resource_load(listener_inst->state->backend, data->resource, data->is_streaming, listener_inst->instance->base_resource)) {
            KERROR("Failed to load audio resource into audio system backend. Resource will be released and handle unusable.");

            kresource_system_release(engine_systems_get()->kresource_state, resource->name);

            kzero_memory(data, sizeof(kaudio_resource_handle_data));
            data->uniqueid = INVALID_ID_U64;
        }
    }

    // Cleanup the listener.
    KFREE_TYPE(listener, audio_asset_request_listener, MEMORY_TAG_RESOURCE);
}

static b8 base_resource_handle_is_valid_and_pristine(kaudio_system_state* state, khandle handle) {
    return state && khandle_is_valid(handle) && handle.handle_index < state->max_resource_count && khandle_is_pristine(handle, state->resources[handle.handle_index].uniqueid);
}

static b8 instance_handle_is_valid_and_pristine(kaudio_system_state* state, kaudio_resource_handle_data* base, khandle handle) {
    u32 instance_count = darray_length(base->instances);
    return state && base && base->instances && khandle_is_valid(handle) && handle.handle_index < instance_count && khandle_is_pristine(handle, base->instances[handle.handle_index].uniqueid);
}

static kaudio_resource_handle_data* get_base(kaudio_system_state* state, khandle base_resource) {
    if (!base_resource_handle_is_valid_and_pristine(state, base_resource)) {
        return 0;
    }
    return &state->resources[base_resource.handle_index];
}

static kaudio_resource_instance_data* get_instance(kaudio_system_state* state, kaudio_resource_handle_data* base, khandle instance) {
    if (!instance_handle_is_valid_and_pristine(state, base, instance)) {
        return 0;
    }
    return &base->instances[instance.handle_index];
}

static u32 get_active_instance_count(kaudio_resource_handle_data* base) {
    u32 count = 0;
    u32 length = darray_length(base->instances);
    for (u32 i = 0; i < length; ++i) {
        count += (base->instances[i].uniqueid != INVALID_ID_U64);
    }
    return count;
}

static kaudio_channel* get_channel(kaudio_system_state* state, i8 channel_index) {
    if (!state) {
        return 0;
    }
    if (channel_index < 0) {
        // First available
        for (u32 i = 0; i < state->audio_channel_count; ++i) {
            kaudio_channel* channel = &state->channels[i];
            if (!channel->bound_instance && !channel->bound_resource) {
                // Available, use it.
                return channel;
            }
        }
        KWARN("No channel is available for auto-selection.");
        return 0;

    } else if (channel_index < (i8)state->audio_channel_count) {
        // Explicit channel id must be within range.
        return &state->channels[channel_index];
    }

    return 0;
}
