#include "audio_system.h"

#include "audio/audio_types.h"
#include "core/engine.h"
#include "defines.h"
#include "logger.h"
#include "parsers/kson_parser.h"
#include "plugins/plugin_types.h"
#include "systems/plugin_system.h"

typedef struct audio_channel {
    f32 volume;
    // The currently bound sound/stream.
    struct audio_file* current;
    // The currently bound audio emitter.
    audio_emitter* emitter;

} audio_channel;

typedef struct audio_system_state {
    audio_system_config config;
    audio_backend_interface* backend;
    f32 master_volume;
    audio_channel channels[MAX_AUDIO_CHANNELS];
} audio_system_state;

b8 audio_system_deserialize_config(const char* config_str, audio_system_config* out_config) {
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

b8 audio_system_initialize(u64* memory_requirement, void* state, audio_system_config* config) {
    if (!memory_requirement || !config) {
        KERROR("Audio system initialization requires valid pointers to memory_requirement and config.");
        return false;
    }
    audio_system_config* typed_config = (audio_system_config*)config;

    u64 struct_requirement = sizeof(audio_system_state);
    *memory_requirement = struct_requirement;
    if (!state) {
        return true;
    }

    audio_system_state* typed_state = (audio_system_state*)state;
    typed_state->config = *typed_config;
    // Default all channels to 1.0 volume (max)
    typed_state->master_volume = 1.0f;
    for (u32 i = 0; i < typed_state->config.audio_channel_count; ++i) {
        typed_state->channels[i].volume = 1.0f;
    }

    kruntime_plugin* plugin = plugin_system_get(engine_systems_get()->plugin_system, config->backend_plugin_name);
    typed_state->backend = plugin->plugin_state;

    return typed_state->backend->initialize(typed_state->backend, config, plugin->config_str);
}

void audio_system_shutdown(void* state) {
    if (state) {
        audio_system_state* typed_state = (audio_system_state*)state;
        typed_state->backend->shutdown(typed_state->backend);
    }
}

b8 audio_system_update(void* state, struct frame_data* p_frame_data) {
    audio_system_state* typed_state = (audio_system_state*)state;

    for (u32 i = 0; i < typed_state->config.audio_channel_count; ++i) {
        audio_channel* channel = &typed_state->channels[i];
        if (channel->emitter) {
            // TODO: sync all properties
            typed_state->backend->source_position_set(typed_state->backend, i, channel->emitter->position);
            typed_state->backend->source_looping_set(typed_state->backend, i, channel->emitter->looping);
            typed_state->backend->source_gain_set(typed_state->backend, i, typed_state->master_volume * typed_state->channels[i].volume * channel->emitter->volume);
        }
    }

    return typed_state->backend->update(typed_state->backend, p_frame_data);
}

b8 audio_system_listener_orientation_set(vec3 position, vec3 forward, vec3 up) {
    audio_system_state* state = engine_systems_get()->audio_system;
    state->backend->listener_position_set(state->backend, position);
    state->backend->listener_orientation_set(state->backend, forward, up);
    return true;
}

struct audio_file* audio_system_chunk_load(const char* path) {
    audio_system_state* state = engine_systems_get()->audio_system;
    return state->backend->chunk_load(state->backend, path);
}

struct audio_file* audio_system_stream_load(const char* path) {
    audio_system_state* state = engine_systems_get()->audio_system;
    return state->backend->stream_load(state->backend, path);
}

void audio_system_close(struct audio_file* file) {
    audio_system_state* state = engine_systems_get()->audio_system;
    state->backend->audio_unload(state->backend, file);
}

void audio_system_master_volume_query(f32* out_volume) {
    if (out_volume) {
        audio_system_state* state = engine_systems_get()->audio_system;
        *out_volume = state->master_volume;
    }
}

void audio_system_master_volume_set(f32 volume) {
    audio_system_state* state = engine_systems_get()->audio_system;
    state->master_volume = KCLAMP(volume, 0.0f, 1.0f);

    // Now adjust each channel's volume to take this into account.
    for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
        f32 mixed_volume = state->channels[i].volume * state->master_volume;
        if (state->channels[i].emitter) {
            // Take the emitter's volume into account if there is one.
            mixed_volume *= state->channels[i].emitter->volume;
        }
        state->backend->source_gain_set(state->backend, i, mixed_volume);
    }
}

b8 audio_system_channel_volume_query(i8 channel_id, f32* out_volume) {
    if (channel_id < 0 || !out_volume) {
        return false;
    }
    audio_system_state* state = engine_systems_get()->audio_system;
    *out_volume = state->channels[channel_id].volume;

    return true;
}

b8 audio_system_channel_volume_set(i8 channel_id, f32 volume) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if ((u8)channel_id >= state->config.audio_channel_count) {
        KERROR("Channel id %u is outside the range of available channels. Nothing was done.", channel_id);
        return false;
    }

    state->channels[channel_id].volume = KCLAMP(volume, 0.0f, 1.0f);
    // Apply the channel volume, taking the master volume into account.
    f32 mixed_volume = state->channels[channel_id].volume * state->master_volume;
    if (state->channels[channel_id].emitter) {
        // Take the emitter's volume into account if there is one.
        mixed_volume *= state->channels[channel_id].emitter->volume;
    }
    state->backend->source_gain_set(state->backend, channel_id, mixed_volume);
    return true;
}

b8 audio_system_channel_play(i8 channel_id, struct audio_file* file, b8 loop) {
    if (!file) {
        return false;
    }

    audio_system_state* state = engine_systems_get()->audio_system;

    // If -1 is passed, use the first available channel.
    if (channel_id == -1) {
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            audio_channel* ch = &state->channels[i];
            if (!ch->current && !ch->emitter) {
                // Available, assign away.
                channel_id = i;
                break;
            }
        }
    }

    if (channel_id == -1) {
        // This means all channels are playing something. Drop the sound.
        KWARN("No channel available for playback. Dropping sound.");
        return false;
    }

    audio_channel* channel = &state->channels[channel_id];
    // Ensure nothing is assigned but what should be.
    channel->emitter = 0;
    channel->current = file;

    // Set the channel volume.
    state->backend->source_gain_set(state->backend, channel_id, state->master_volume * channel->volume);

    if (file->type == AUDIO_FILE_TYPE_SOUND_EFFECT) {
        // Set the position to the listener position.
        vec3 position;
        state->backend->listener_position_query(state->backend, &position);
        state->backend->source_position_set(state->backend, channel_id, position);
        // Set looping.
        state->backend->source_looping_set(state->backend, channel_id, loop);
    }

    state->backend->source_stop(state->backend, channel_id);
    return state->backend->play_on_source(state->backend, file, channel_id);
}

b8 audio_system_channel_emitter_play(i8 channel_id, struct audio_emitter* emitter) {
    if (!emitter || !emitter->file) {
        return false;
    }

    audio_system_state* state = engine_systems_get()->audio_system;
    // If -1 is passed, use the first available channel.
    if (channel_id == -1) {
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            audio_channel* ch = &state->channels[i];
            if (!ch->current && !ch->emitter) {
                // Available, assign away.
                channel_id = i;
                break;
            }
        }
    }

    if (channel_id == -1) {
        // This means all channels are playing something. Drop the sound.
        KWARN("No channel available for playback. Dropping emitter.");
        return false;
    }
    audio_channel* channel = &state->channels[channel_id];
    // Ensure nothing is assigned but what should be.
    channel->emitter = emitter;
    channel->current = emitter->file;

    return state->backend->play_on_source(state->backend, emitter->file, channel_id);
}

void audio_system_channel_stop(i8 channel_id) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if (channel_id < 0) {
        // Stop all channels.
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            state->backend->source_stop(state->backend, i);
        }
    } else {
        // Stop the given channel.
        state->backend->source_stop(state->backend, channel_id);
    }
}

void audio_system_channel_pause(i8 channel_id) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if (channel_id < 0) {
        // Pause all channels.
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            state->backend->source_pause(state->backend, i);
        }
    } else {
        // Pause the given channel.
        state->backend->source_pause(state->backend, channel_id);
    }
}

void audio_system_channel_resume(i8 channel_id) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if (channel_id < 0) {
        // Resume all channels.
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            state->backend->source_resume(state->backend, i);
        }
    } else {
        // Resume the given channel.
        state->backend->source_resume(state->backend, channel_id);
    }
}
