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
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/plugin_system.h"

typedef struct kaudio_category_config {
    kname name;
    f32 volume;
    kaudio_space audio_space;
    u32 channel_id_count;
    u32* channel_ids;
} kaudio_category_config;

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
    u32 max_count;

    u32 category_count;
    kaudio_category_config* categories;

    /** @brief The name of the plugin to be loaded for the audio backend. */
    const char* backend_plugin_name;
} kaudio_system_config;

typedef struct kaudio_emitter_handle_data {
    u64 uniqueid;

    // Handle to underlying resource instance.
    kaudio_instance instance;
    // Emitter-specific volume.
    f32 volume;

    /** @brief inner_radius The inner radius around the sound's center point. A listener inside this radius experiences the volume at 100%. */
    f32 inner_radius;
    /** @brief outer_radius The outer radius around the sound's center point. A listener outside this radius experiences the volume at 0%. */
    f32 outer_radius;
    /** @brief The falloff factor to use for distance-based sound falloff. Only used for exponential falloff. */
    f32 falloff;
    /** @brief The attenuation model to use for distance-based sound falloff. */
    kaudio_attenuation_model attenuation_model;
    vec3 world_position;

    b8 is_looping;
    b8 is_streaming;

    // Only changed by audio system when within range.
    b8 playing_in_range;

    kname resource_name;
    kname package_name;

    vec3 velocity;
} kaudio_emitter_handle_data;

typedef struct kaudio_channel {
    // The channel index.
    u8 index;
    // The channel volume
    f32 volume;

    // A index to the currently bound audio data, if in use; otherwise INVALID_KAUDIO (i.e. not in use)
    kaudio bound_audio;
    // A index to the currently bound instance data, if in  use; otherwise INVALID_ID_U16 (i.e. not in use)
    u16 bound_instance;
} kaudio_channel;

typedef struct kaudio_category {
    kname name;
    f32 volume;
    kaudio_space audio_space;
    u32 channel_id_count;
    u32* channel_ids;
} kaudio_category;

typedef enum kaudio_instance_state {
    KAUDIO_INSTANCE_STATE_UNINITIALIZED,
    KAUDIO_INSTANCE_STATE_ACQUIRED
} kaudio_instance_state;

typedef struct kaudio_instance_data {
    // State of the instance. Uninitialized = free
    kaudio_instance_state state;
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
} kaudio_instance_data;

typedef enum kaudio_state {
    KAUDIO_STATE_UNINITIALIZED,
    KAUDIO_STATE_LOADING,
    KAUDIO_STATE_LOADED,
} kaudio_state;

typedef struct kaudio_data {
    // Names of kaudios.
    kname* names;

    kaudio_state* states;

    // Indicates if the audio should be streamed in small bits (large files) or loaded all at once (small files). Indexed by kaudio.
    b8* is_streamings;

    // The number of audio channels, indexed by kaudio.
    u8* channel_counts;

    // array of darrays of instances of kaudios, indexed by kaudio.
    // ex: data.instances[audio][instance_id]
    kaudio_instance_data** instances;
} kaudio_data;

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

    u32 category_count;
    kaudio_category categories[AUDIO_CHANNEL_MAX_COUNT];

    // The max number of audio resources that can be loaded at any time.
    u16 max_count;

    // audio data in the system's frontend.
    // Contains arrays of data.
    kaudio_data data;

    // darray of audio emitters.
    kaudio_emitter_handle_data* emitters;

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
    kaudio_instance instance;
} audio_asset_request_listener;

static b8 deserialize_config(const char* config_str, kaudio_system_config* out_config);
static kaudio create_base_audio(kaudio_system_state* state, b8 is_streaming);
static u16 issue_new_instance(kaudio_system_state* state, kaudio base);
static void kasset_audio_loaded_callback(void* listener, kasset_audio* asset);
static u16 get_active_instance_count(kaudio_system_state* state, kaudio base);
static kaudio_channel* get_channel(kaudio_system_state* state, i8 channel_index);
static kaudio_channel* get_available_channel_from_category(kaudio_system_state* state, u8 category_index);
static void kaudio_emitter_update(struct kaudio_system_state* state, kaudio_emitter_handle_data* emitter);

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
        config.max_count = 32;
    }

    state->chunk_size = config.chunk_size;
    state->channel_count = config.channel_count;
    state->audio_channel_count = config.audio_channel_count;
    state->frequency = config.frequency;
    state->max_count = config.max_count;

    state->data.instances = KALLOC_TYPE_CARRAY(kaudio_instance_data*, state->max_count);
    state->data.is_streamings = KALLOC_TYPE_CARRAY(b8, state->max_count);
    state->data.states = KALLOC_TYPE_CARRAY(kaudio_state, state->max_count);
    state->data.names = KALLOC_TYPE_CARRAY(kname, state->max_count);
    state->data.channel_counts = KALLOC_TYPE_CARRAY(u8, state->max_count);

    // Default volumes for master and all channels to 1.0 (max);
    state->master_volume = 1.0f;
    for (i8 i = 0; i < (i8)state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        channel->index = i;
        channel->volume = 1.0f;
        // Also set some other reasonable defaults.
        channel->bound_audio = INVALID_KAUDIO;
        channel->bound_instance = INVALID_ID_U16;
    }

    // Categories.
    state->category_count = config.category_count;
    for (u32 i = 0; i < config.category_count; ++i) {
        state->categories[i].name = config.categories[i].name;
        state->categories[i].audio_space = config.categories[i].audio_space;
        state->categories[i].volume = config.categories[i].volume;
        state->categories[i].channel_id_count = config.categories[i].channel_id_count;
        state->categories[i].channel_ids = KALLOC_TYPE_CARRAY(u32, state->categories[i].channel_id_count);
        kcopy_memory(state->categories[i].channel_ids, config.categories[i].channel_ids, sizeof(u32) * state->categories[i].channel_id_count);
    }

    // Darray for audio emitters.
    state->emitters = darray_create(kaudio_emitter_handle_data);

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
    backend_config.max_count = config.max_count;
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

        // Update the registered emitters.
        u32 emitter_count = darray_length(state->emitters);
        for (u32 i = 0; i < emitter_count; ++i) {
            if (state->emitters[i].uniqueid != INVALID_ID_U64) {
                kaudio_emitter_update(state, &state->emitters[i]);
            }
        }

        // Adjust each channel's properties based on what is bound to them (if anything).
        for (u32 i = 0; i < state->audio_channel_count; ++i) {
            kaudio_channel* channel = &state->channels[i];
            if (channel->bound_audio != INVALID_KAUDIO && channel->bound_instance != INVALID_ID_U16) {
                b8 is_valid = state->data.states[channel->bound_audio] == KAUDIO_STATE_LOADED;
                kaudio_instance_data* instance = &state->data.instances[channel->bound_audio][channel->bound_instance];

                // If a play has been triggered and the resource is valid/ready for playing, do it.
                if (instance->trigger_play && is_valid) {
                    b8 play_result = state->backend->channel_play_resource(
                        state->backend,
                        channel->bound_audio,
                        instance->audio_space,
                        channel->index);

                    if (!play_result) {
                        KERROR("Failed to play resource on channel index %i", channel->index);
                    } else {
                        // Unset the flag on success.
                        instance->trigger_play = false;
                    }
                }

                // Volume
                f32 gain = 1.0f;
                // Apply the volume at various levels by mixing them.
                f32 mixed_volume = instance->volume * channel->volume * state->master_volume;

                if (instance->audio_space == KAUDIO_SPACE_3D) {
                    // Perform custom attenuation for sounds based on distance and falloff method. This is only done for
                    // mono sounds.
                    f32 distance = vec3_distance(instance->position, state->listener_position);
                    gain = calculate_spatial_gain(distance, instance->inner_radius, instance->outer_radius, instance->falloff, instance->attenuation_model);

                    state->backend->channel_position_set(state->backend, i, instance->position);
                } else {
                    // Treat as 2D, even if mono, by syncing the position of the sound/channel with the listener.
                    state->backend->channel_position_set(state->backend, i, state->listener_position);
                    // NOTE: gain is left at 1.0 here, effectively meaning "zero-distance"
                }

                // Apply the mixed volume
                gain *= mixed_volume;

                state->backend->channel_gain_set(state->backend, i, gain);

                // Pitch
                state->backend->channel_pitch_set(state->backend, i, instance->pitch);
                // Looping setting
                b8 looping = instance->looping;
                if (state->data.is_streamings[channel->bound_audio]) {
                    // Audio channels for streams should never loop directly, but are checked internally instead.
                    // Always force these to be false for streams.
                    looping = false;
                }
                state->backend->channel_looping_set(state->backend, i, looping);

                // Position is only applied for mono sounds, because only those can be spatial/use position.
                // FIXME: Store channel count at this level?
                if (state->data.states[channel->bound_audio] == KAUDIO_STATE_LOADED && state->data.channel_counts[channel->bound_audio] == 1) {
                    state->backend->channel_position_set(state->backend, i, instance->position);
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

kaudio_instance kaudio_acquire(struct kaudio_system_state* state, kname asset_name, b8 is_streaming, kaudio_space audio_space) {
    return kaudio_acquire_from_package(state, asset_name, INVALID_KNAME, is_streaming, audio_space);
}

kaudio_instance kaudio_acquire_from_package(struct kaudio_system_state* state, kname asset_name, kname package_name, b8 is_streaming, kaudio_space audio_space) {
    kaudio_instance out_instance = {
        .base = INVALID_KAUDIO,
        .instance_id = INVALID_ID_U16};

    if (!state) {
        return out_instance;
    }

    // Search first for an existing kaudio with the asset_name as its name.
    for (u16 i = 0; i < state->max_count; ++i) {
        if (state->data.names[i] == asset_name) {
            // Issue new instance and return.
            out_instance.base = i;
            out_instance.instance_id = issue_new_instance(state, out_instance.base);
            state->data.instances[out_instance.base][out_instance.instance_id].audio_space = audio_space;
            return out_instance;
        }
    }

    // No existing kaudio, so create a new one.
    kaudio base = create_base_audio(state, is_streaming);
    out_instance.base = base;

    // Listener for the request.
    audio_asset_request_listener* listener = KALLOC_TYPE(audio_asset_request_listener, MEMORY_TAG_RESOURCE);
    listener->state = state;
    listener->instance = out_instance;

    // Request the asset.
    kasset_audio* asset = asset_system_request_audio_from_package(engine_systems_get()->asset_state, kname_string_get(package_name), kname_string_get(asset_name), listener, kasset_audio_loaded_callback);
    if (!asset) {
        KERROR("Failed to request kaudio asset. See logs for details.");
        kfree(listener, sizeof(audio_asset_request_listener), MEMORY_TAG_RESOURCE);
        return out_instance;
    }

    // Issue new instance for it.
    out_instance.instance_id = issue_new_instance(state, out_instance.base);

    // Set the instance's audio space accordingly.
    state->data.instances[out_instance.base][out_instance.instance_id].audio_space = audio_space;
    return out_instance;
}

void kaudio_release(struct kaudio_system_state* state, kaudio_instance* instance) {
    if (state && instance && instance->base != INVALID_KAUDIO && instance->instance_id != INVALID_ID_U16) {

        // Invalidate the instance data.
        kzero_memory(&state->data.instances[instance->base][instance->instance_id], sizeof(kaudio_instance_data));

        // See how many active instances there are left. If none, release.
        u16 active_instance_count = get_active_instance_count(state, instance->base);
        if (!active_instance_count) {
            KTRACE("KAudio '%s' has no more instances and will be released.", kname_string_get(state->data.names[instance->base]));

            // Release from backend.
            state->backend->unload(state->backend, instance->base);

            // Clear instance array.
            darray_clear(state->data.instances[instance->base]);

            // Reset the slot data and make the slot available for use.
            state->data.names[instance->base] = INVALID_KNAME;
            state->data.is_streamings[instance->base] = false;
            state->data.states[instance->base] = KAUDIO_STATE_UNINITIALIZED;
        }

        // Invalidate the instance.
        instance->base = INVALID_KAUDIO;
        instance->instance_id = INVALID_ID_U16;
    }
}

i8 kaudio_category_id_get(struct kaudio_system_state* state, kname name) {
    for (i8 i = 0; i < (i8)state->category_count; ++i) {
        if (state->categories[i].name == name) {
            return i;
        }
    }

    // Not found.
    return -1;
}

b8 kaudio_play_in_category_by_name(struct kaudio_system_state* state, kaudio_instance instance, kname category_name) {
    i8 category_index = kaudio_category_id_get(state, category_name);
    if (category_index < 0) {
        return false;
    }

    return kaudio_play_in_category(state, instance, (u8)category_index);
}

b8 kaudio_play_in_category(struct kaudio_system_state* state, kaudio_instance instance, u8 category_index) {
    if (!state || category_index >= state->category_count) {
        return false;
    }

    // Get a channel belonging to the category.
    kaudio_channel* channel = get_available_channel_from_category(state, category_index);
    if (!channel) {
        KWARN("No channel available to auto-select - perhaps increase number of channels for category? index=%u", category_index);
        // Pick the first channel in the category and clobber it's sound.
        channel = &state->channels[state->categories[category_index].channel_ids[0]];
        kaudio_channel_stop(state, channel->index);
    }

    // Play it on that channel.
    return kaudio_play(state, instance, channel->index);
}

b8 kaudio_play(struct kaudio_system_state* state, kaudio_instance instance, i8 channel_index) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    kaudio_instance_data* instance_data = &state->data.instances[instance.base][instance.instance_id];
    if (!instance_data) {
        KERROR("%s was called with an invalid or stale instance index.", __FUNCTION__);
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
    channel->bound_audio = instance.base;
    channel->bound_instance = instance.instance_id;

    // Trigger a play on the next update if/when the bound resource is valid for playing.
    instance_data->trigger_play = true;

    // NOTE: deliberately not playing here as it's possible the sound isn't ready yet.
    return true;
}

b8 kaudio_stop(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        if (channel->bound_audio == instance.base && channel->bound_instance == instance.instance_id) {
            // Found the channel it's bound to, stop.
            return kaudio_channel_stop(state, i);
        }
    }

    // Return false if the audio wasn't bound to any channel.
    return false;
}

b8 kaudio_pause(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        if (channel->bound_audio == instance.base && channel->bound_instance == instance.instance_id) {
            // Found the channel it's bound to, stop.
            return kaudio_channel_pause(state, i);
        }
    }

    // Return false if the audio wasn't bound to any channel.
    return false;
}

b8 kaudio_resume(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    for (u32 i = 0; i < state->audio_channel_count; ++i) {
        kaudio_channel* channel = &state->channels[i];
        if (channel->bound_audio == instance.base && channel->bound_instance == instance.instance_id) {
            // Found the channel it's bound to, stop.
            return kaudio_channel_resume(state, i);
        }
    }

    // Return false if the audio wasn't bound to any channel.
    return false;
}

b8 kaudio_is_valid(struct kaudio_system_state* state, kaudio_instance instance) {
    if (instance.base == INVALID_KAUDIO || instance.instance_id == INVALID_ID_U16) {
        return false;
    }

    // Check range of base audio and instance id.
    if (instance.base >= state->max_count || !state->data.instances[instance.base] || instance.instance_id >= darray_length(state->data.instances[instance.base])) {
        return false;
    }

    // Part of being 'valid' also requires it to have a fully-loaded asset.
    return true; // state->data.states[instance.base] == KAUDIO_STATE_LOADED;
}

f32 kaudio_pitch_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return 0.0f;
    }
    return state->data.instances[instance.base][instance.instance_id].pitch;
}

b8 kaudio_pitch_set(struct kaudio_system_state* state, kaudio_instance instance, f32 pitch) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    // Clamp to a valid range.
    state->data.instances[instance.base][instance.instance_id].pitch = KCLAMP(pitch, AUDIO_PITCH_MIN, AUDIO_PITCH_MAX);
    return true;
}

f32 kaudio_volume_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return 0.0f;
    }

    return state->data.instances[instance.base][instance.instance_id].volume;
}

b8 kaudio_volume_set(struct kaudio_system_state* state, kaudio_instance instance, f32 volume) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    // Clamp to a valid range.
    state->data.instances[instance.base][instance.instance_id].volume = KCLAMP(volume, AUDIO_VOLUME_MIN, AUDIO_VOLUME_MAX);
    return true;
}

b8 kaudio_looping_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    return state->data.instances[instance.base][instance.instance_id].looping;
}

b8 kaudio_looping_set(struct kaudio_system_state* state, kaudio_instance instance, b8 looping) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    state->data.instances[instance.base][instance.instance_id].looping = looping;
    return true;
}

vec3 kaudio_position_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return vec3_zero();
    }

    return state->data.instances[instance.base][instance.instance_id].position;
}

b8 kaudio_position_set(struct kaudio_system_state* state, kaudio_instance instance, vec3 position) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    state->data.instances[instance.base][instance.instance_id].position = position;
    return true;
}

f32 kaudio_inner_radius_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return 0.0f;
    }

    return state->data.instances[instance.base][instance.instance_id].inner_radius;
}

b8 kaudio_inner_radius_set(struct kaudio_system_state* state, kaudio_instance instance, f32 inner_radius) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    state->data.instances[instance.base][instance.instance_id].inner_radius = KCLAMP(inner_radius, AUDIO_INNER_RADIUS_MIN, AUDIO_INNER_RADIUS_MAX);
    return true;
}

f32 kaudio_outer_radius_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return 0.0f;
    }

    return state->data.instances[instance.base][instance.instance_id].outer_radius;
}

b8 kaudio_outer_radius_set(struct kaudio_system_state* state, kaudio_instance instance, f32 outer_radius) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    state->data.instances[instance.base][instance.instance_id].outer_radius = KCLAMP(outer_radius, AUDIO_OUTER_RADIUS_MIN, AUDIO_OUTER_RADIUS_MAX);
    return true;
}

f32 kaudio_falloff_get(struct kaudio_system_state* state, kaudio_instance instance) {
    if (!kaudio_is_valid(state, instance)) {
        return 0.0f;
    }

    return state->data.instances[instance.base][instance.instance_id].falloff;
}

b8 kaudio_falloff_set(struct kaudio_system_state* state, kaudio_instance instance, f32 falloff) {
    if (!kaudio_is_valid(state, instance)) {
        return false;
    }

    state->data.instances[instance.base][instance.instance_id].falloff = KCLAMP(falloff, AUDIO_FALLOFF_MIN, AUDIO_FALLOFF_MAX);
    return true;
}

b8 kaudio_channel_play(struct kaudio_system_state* state, u8 channel_index) {
    kaudio_channel* channel = get_channel(state, channel_index);
    if (!channel) {
        KERROR("%s called with channel_index %hhu out of range (range = 0-%u)", __FUNCTION__, channel_index, state->audio_channel_count);
        return false;
    }

    // Attempt to play the already bound resource if one exists. Otherwise this fails.
    if (channel->bound_audio != INVALID_KAUDIO) {
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
    channel->bound_audio = INVALID_KAUDIO;
    channel->bound_instance = INVALID_ID_U16;

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

b8 kaudio_emitter_create(struct kaudio_system_state* state, f32 inner_radius, f32 outer_radius, f32 volume, f32 falloff, b8 is_looping, b8 is_streaming, kname audio_resource_name, kname package_name, khandle* out_emitter) {
    if (!state || !out_emitter) {
        return false;
    }

    *out_emitter = khandle_invalid();

    // Look for a free slot, or push a new one if needed.
    kaudio_emitter_handle_data* emitter = 0;
    u32 length = darray_length(state->emitters);
    for (u32 i = 0; i < length; ++i) {
        if (state->emitters[i].uniqueid == INVALID_ID_U64) {
            emitter = &state->emitters[i];
            *out_emitter = khandle_create(i);
            break;
        }
    }

    if (!emitter) {
        kaudio_emitter_handle_data new_emitter = {0};
        new_emitter.uniqueid = INVALID_ID_U64;
        darray_push(state->emitters, new_emitter);
        emitter = &state->emitters[length];
        *out_emitter = khandle_create(length);
    }

    emitter->uniqueid = out_emitter->unique_id.uniqueid;
    emitter->volume = volume;
    emitter->inner_radius = inner_radius;
    emitter->outer_radius = outer_radius;
    emitter->falloff = falloff;
    emitter->is_looping = is_looping;
    emitter->is_streaming = is_streaming;
    emitter->resource_name = audio_resource_name;
    emitter->package_name = package_name;

    return true;
}

b8 kaudio_emitter_load(struct kaudio_system_state* state, khandle emitter_handle) {
    if (!state) {
        return false;
    }

    if (khandle_is_valid(emitter_handle) && khandle_is_pristine(emitter_handle, state->emitters[emitter_handle.handle_index].uniqueid)) {
        kaudio_emitter_handle_data* emitter = &state->emitters[emitter_handle.handle_index];
        // NOTE: always use 3d space for emitters.
        emitter->instance = kaudio_acquire_from_package(state, emitter->resource_name, emitter->package_name, emitter->is_streaming, KAUDIO_SPACE_3D);
        if (emitter->instance.base == INVALID_KAUDIO || emitter->instance.instance_id == INVALID_ID_U16) {
            KWARN("Failed to acquire audio resource from audio system.");
            return false;
        }

        // Apply properties to audio.
        kaudio_looping_set(state, emitter->instance, emitter->is_looping);
        kaudio_outer_radius_set(state, emitter->instance, emitter->outer_radius);
        kaudio_inner_radius_set(state, emitter->instance, emitter->inner_radius);
        kaudio_falloff_set(state, emitter->instance, emitter->falloff);
        kaudio_position_set(state, emitter->instance, emitter->world_position);
        kaudio_volume_set(state, emitter->instance, emitter->volume);
        return true;
    }

    return false;
}

b8 kaudio_emitter_unload(struct kaudio_system_state* state, khandle emitter_handle) {
    if (!state) {
        return false;
    }

    if (khandle_is_valid(emitter_handle) && khandle_is_pristine(emitter_handle, state->emitters[emitter_handle.handle_index].uniqueid)) {
        kaudio_emitter_handle_data* emitter = &state->emitters[emitter_handle.handle_index];
        if (emitter->playing_in_range) {
            // Stop playing
            kaudio_stop(state, emitter->instance);
            emitter->playing_in_range = false;
        }

        kaudio_release(state, &emitter->instance);

        // Take a copy of the invalidated instance.
        kaudio_instance invalid_inst = emitter->instance;

        kzero_memory(&emitter->instance, sizeof(kaudio_emitter_handle_data));

        // Invalidate the handle data.
        emitter->uniqueid = INVALID_ID_U64;
        emitter->instance = invalid_inst;

        return true;
    }

    return false;
}

b8 kaudio_emitter_world_position_set(struct kaudio_system_state* state, khandle emitter_handle, vec3 world_position) {
    if (!state) {
        return false;
    }

    if (khandle_is_valid(emitter_handle) && khandle_is_pristine(emitter_handle, state->emitters[emitter_handle.handle_index].uniqueid)) {
        kaudio_emitter_handle_data* emitter = &state->emitters[emitter_handle.handle_index];
        emitter->world_position = world_position;
        kaudio_position_set(state, emitter->instance, emitter->world_position);
        return true;
    }

    return false;
}

static void kaudio_emitter_update(struct kaudio_system_state* state, kaudio_emitter_handle_data* emitter) {
    if (emitter->playing_in_range) {
        // Check if still in range. If not, need to stop.
        if (vec3_distance(state->listener_position, emitter->world_position) > emitter->outer_radius) {
            KTRACE("Audio emitter no longer in listener range. Stopping.");
            // Stop playing
            kaudio_stop(state, emitter->instance);
            emitter->playing_in_range = false;

        } else {
            // Continue
        }
    } else {
        // Check if in range. If so, need to start playing.
        if (vec3_distance(state->listener_position, emitter->world_position) <= emitter->outer_radius) {
            KTRACE("Audio emitter came into listener range. Playing.");
            // HACK: Don't hardcode this. Config? Define family group, or index somehow?
            kaudio_play(state, emitter->instance, -1);
            emitter->playing_in_range = true;
        }
    }

    // If still playing, apply audio properties.
    if (emitter->playing_in_range) {
        kaudio_looping_set(state, emitter->instance, emitter->is_looping);
        kaudio_outer_radius_set(state, emitter->instance, emitter->outer_radius);
        kaudio_inner_radius_set(state, emitter->instance, emitter->inner_radius);
        kaudio_falloff_set(state, emitter->instance, emitter->falloff);
        kaudio_position_set(state, emitter->instance, emitter->world_position);
        kaudio_volume_set(state, emitter->instance, emitter->volume);
    }
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
    out_config->max_count = max_resource_count;

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

    kson_array category_obj_array = {0};
    if (kson_object_property_value_get_array(&tree.root, "categories", &category_obj_array)) {
        if (kson_array_element_count_get(&category_obj_array, &out_config->category_count)) {
            out_config->categories = KALLOC_TYPE_CARRAY(kaudio_category_config, out_config->category_count);

            // Each category.
            for (u32 i = 0; i < out_config->category_count; ++i) {
                kaudio_category_config* cat = &out_config->categories[i];
                kson_object cat_obj = {0};
                if (!kson_array_element_value_get_object(&category_obj_array, i, &cat_obj)) {
                    KERROR("Possible format error reading object at index %u in 'categories' array. Skipping", i);
                    continue;
                }

                // Name - required
                if (!kson_object_property_value_get_string_as_kname(&cat_obj, "name", &cat->name)) {
                    KERROR("Unable to find required category property 'name' at index %u. Skipping.", i);
                    continue;
                }

                // Volume - optional
                if (!kson_object_property_value_get_float(&cat_obj, "volume", &cat->volume)) {
                    // Default
                    cat->volume = 1.0f;
                }

                // Audio space - optional
                const char* audio_space_str = 0;
                if (!kson_object_property_value_get_string(&cat_obj, "audio_space", &audio_space_str)) {
                    cat->audio_space = KAUDIO_SPACE_2D; // default to 2d if not provided.
                } else {
                    cat->audio_space = string_to_audio_space(audio_space_str);
                    string_free(audio_space_str);
                }

                // Channel ids - required, must have at least one.
                kson_array channel_ids_array = {0};
                if (!kson_object_property_value_get_array(&cat_obj, "channel_ids", &channel_ids_array)) {
                    KERROR("'channel_ids', a required field for a cateregory, does not exist for cateregory index %u. Skipping.", i);
                    continue;
                }
                kson_array_element_count_get(&channel_ids_array, &cat->channel_id_count);
                if (!cat->channel_id_count) {
                    KERROR("Channel cateregory must have at least one channel id listed. Skipping index %u.", i);
                    continue;
                }

                cat->channel_ids = KALLOC_TYPE_CARRAY(u32, cat->channel_id_count);

                for (u32 c = 0; c < cat->channel_id_count; c++) {
                    i64 val = 0;
                    kson_array_element_value_get_int(&channel_ids_array, c, &val);
                    cat->channel_ids[c] = (u32)val;
                }
            }
        }
    }

    kson_tree_cleanup(&tree);

    return true;
}

static kaudio create_base_audio(kaudio_system_state* state, b8 is_streaming) {
    // Look for a new free slot.
    for (u16 i = 0; i < state->max_count; ++i) {
        if (state->data.states[i] == KAUDIO_STATE_UNINITIALIZED) {
            // Found one.
            state->data.states[i] = KAUDIO_STATE_LOADING;
            state->data.instances[i] = darray_create(kaudio_instance_data);
            state->data.is_streamings[i] = is_streaming;
            state->data.channel_counts[i] = 0;

            return i;
        }
    }
    KFATAL("No more room to allocate a new kaudio. Expand the max_count in configuration to load more at once.");
    return INVALID_KAUDIO;
}

static u16 issue_new_instance(kaudio_system_state* state, kaudio base) {
    u16 count = darray_length(state->data.instances[base]);
    u16 instance_id = INVALID_ID_U16;
    for (u16 i = 0; i < count; ++i) {
        if (state->data.instances[base][i].state == KAUDIO_INSTANCE_STATE_UNINITIALIZED) {
            // Found one, use it.
            instance_id = i;
            break;
        }
    }

    if (instance_id == INVALID_ID_U16) {
        // If this point is reached, no slots are available. Push a new one.
        darray_push(state->data.instances[base], (kaudio_instance_data){0});
        instance_id = count;
    }

    kaudio_instance_data* instance = &state->data.instances[base][instance_id];

    // Mark as in-use.
    instance->state = KAUDIO_INSTANCE_STATE_ACQUIRED;

    // Setup some reasonable defaults.
    instance->looping = state->data.is_streamings[base]; // Streaming sounds automatically loop.
    instance->pitch = AUDIO_PITCH_DEFAULT;
    instance->volume = AUDIO_VOLUME_DEFAULT;
    instance->position = vec3_zero();
    instance->inner_radius = AUDIO_INNER_RADIUS_DEFAULT;
    instance->outer_radius = AUDIO_OUTER_RADIUS_DEFAULT;
    instance->falloff = AUDIO_FALLOFF_DEFAULT;

    return instance_id;
}

// Invoked when an audio asset completes its async load from disk.
static void kasset_audio_loaded_callback(void* listener, kasset_audio* asset) {
    audio_asset_request_listener* listener_inst = listener;
    KTRACE("Audio asset loaded: '%s'.", kname_string_get(asset->name));
    kaudio_system_state* state = listener_inst->state;
    kaudio base = listener_inst->instance.base;

    // Send over to the backend to be loaded.
    // b8 (*load)(struct kaudio_backend_interface* backend, i32 channels, u32 sample_rate, u32 total_sample_count, u64 pcm_data_size, i16* pcm_data, b8 is_stream, kaudio audio);
    if (!listener_inst->state->backend->load(listener_inst->state->backend, asset->channels, asset->sample_rate, asset->total_sample_count, asset->pcm_data_size, asset->pcm_data, state->data.is_streamings[base], base)) {
        KERROR("Failed to load audio resource into audio system backend. Resource will be released and handle unusable.");
    } else {
        state->data.states[base] = KAUDIO_STATE_LOADED;

        // TODO: save off any asset info required before release.
        state->data.channel_counts[base] = asset->channels;
    }

    // Release the asset.
    asset_system_release_audio(engine_systems_get()->asset_state, asset);

    // Cleanup the listener.
    KFREE_TYPE(listener, audio_asset_request_listener, MEMORY_TAG_RESOURCE);
}

static u16 get_active_instance_count(kaudio_system_state* state, kaudio base) {
    u32 count = 0;
    kaudio_instance_data* datas = state->data.instances[base];
    if (!datas) {
        return 0;
    }
    u32 length = darray_length(datas);
    for (u32 i = 0; i < length; ++i) {
        count += (datas[i].state == KAUDIO_INSTANCE_STATE_ACQUIRED);
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
            if (channel->bound_instance == INVALID_ID_U16 && channel->bound_audio == INVALID_KAUDIO) {
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

static kaudio_channel* get_available_channel_from_category(kaudio_system_state* state, u8 category_index) {
    if (!state) {
        return 0;
    }

    if (category_index >= state->category_count) {
        return 0;
    }
    kaudio_category* cat = &state->categories[category_index];

    // First available
    for (u32 i = 0; i < cat->channel_id_count; ++i) {
        u32 channel_id = cat->channel_ids[i];
        kaudio_channel* channel = &state->channels[channel_id];
        if (channel->bound_instance == INVALID_ID_U16 && channel->bound_audio == INVALID_KAUDIO) {
            // Available, use it.
            return channel;
        }
    }

    KWARN("No channel is available for auto-selection via category, index=%u.", category_index);
    return 0;
}
