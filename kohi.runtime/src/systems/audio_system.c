#include "audio_system.h"

#include "audio/audio_types.h"
#include "core/engine.h"
#include "defines.h"
#include "logger.h"

typedef struct audio_channel {
    f32 volume;
    // The currently bound sound/stream.
    struct audio_file* current;
    // The currently bound audio emitter.
    audio_emitter* emitter;

} audio_channel;

typedef struct audio_system_state {
    audio_system_config config;
    // FIXME: Resolve from app config.
    audio_plugin* plugin;
    f32 master_volume;
    audio_channel channels[MAX_AUDIO_CHANNELS];
} audio_system_state;

b8 audio_system_initialize(u64* memory_requirement, void* state, void* config) {
    if (!memory_requirement || !config) {
        KERROR("Audio system initialization requires valid pointers to memory_requirement and config.");
        return false;
    }
    audio_system_config* typed_config = (audio_system_config*)config;

    if (typed_config->audio_channel_count < 4) {
        KWARN("Invalid audio system config - audio_channel_count must be at least 4. Defaulting to 4.");
        typed_config->audio_channel_count = 4;
    }

    if (typed_config->chunk_size == 0) {
        typed_config->chunk_size = 4096 * 16;
    }

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

    typed_state->plugin = typed_config->plugin;

    audio_plugin_config plugin_config = {0};
    plugin_config.max_sources = typed_config->audio_channel_count; // MAX_AUDIO_CHANNELS;
    plugin_config.max_buffers = 256;
    plugin_config.chunk_size = typed_config->chunk_size;
    plugin_config.frequency = typed_config->frequency;
    plugin_config.channel_count = typed_config->channel_count;
    return typed_state->plugin->initialize(typed_state->plugin, plugin_config);
}

void audio_system_shutdown(void* state) {
    if (state) {
        audio_system_state* typed_state = (audio_system_state*)state;
        typed_state->plugin->shutdown(typed_state->plugin);
    }
}

b8 audio_system_update(void* state, struct frame_data* p_frame_data) {
    audio_system_state* typed_state = (audio_system_state*)state;

    for (u32 i = 0; i < typed_state->config.audio_channel_count; ++i) {
        audio_channel* channel = &typed_state->channels[i];
        if (channel->emitter) {
            // TODO: sync all properties
            typed_state->plugin->source_position_set(typed_state->plugin, i, channel->emitter->position);
            typed_state->plugin->source_looping_set(typed_state->plugin, i, channel->emitter->looping);
            typed_state->plugin->source_gain_set(typed_state->plugin, i, typed_state->master_volume * typed_state->channels[i].volume * channel->emitter->volume);
        }
    }

    return typed_state->plugin->update(typed_state->plugin, p_frame_data);
}

b8 audio_system_listener_orientation_set(vec3 position, vec3 forward, vec3 up) {
    audio_system_state* state = engine_systems_get()->audio_system;
    state->plugin->listener_position_set(state->plugin, position);
    state->plugin->listener_orientation_set(state->plugin, forward, up);
    return true;
}

struct audio_file* audio_system_chunk_load(const char* path) {
    audio_system_state* state = engine_systems_get()->audio_system;
    return state->plugin->chunk_load(state->plugin, path);
}

struct audio_file* audio_system_stream_load(const char* path) {
    audio_system_state* state = engine_systems_get()->audio_system;
    return state->plugin->stream_load(state->plugin, path);
}

void audio_system_close(struct audio_file* file) {
    audio_system_state* state = engine_systems_get()->audio_system;
    state->plugin->audio_unload(state->plugin, file);
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
        state->plugin->source_gain_set(state->plugin, i, mixed_volume);
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
    state->plugin->source_gain_set(state->plugin, channel_id, mixed_volume);
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
    state->plugin->source_gain_set(state->plugin, channel_id, state->master_volume * channel->volume);

    if (file->type == AUDIO_FILE_TYPE_SOUND_EFFECT) {
        // Set the position to the listener position.
        vec3 position;
        state->plugin->listener_position_query(state->plugin, &position);
        state->plugin->source_position_set(state->plugin, channel_id, position);
        // Set looping.
        state->plugin->source_looping_set(state->plugin, channel_id, loop);
    }

    state->plugin->source_stop(state->plugin, channel_id);
    return state->plugin->play_on_source(state->plugin, file, channel_id);
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

    return state->plugin->play_on_source(state->plugin, emitter->file, channel_id);
}

void audio_system_channel_stop(i8 channel_id) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if (channel_id < 0) {
        // Stop all channels.
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            state->plugin->source_stop(state->plugin, i);
        }
    } else {
        // Stop the given channel.
        state->plugin->source_stop(state->plugin, channel_id);
    }
}

void audio_system_channel_pause(i8 channel_id) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if (channel_id < 0) {
        // Pause all channels.
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            state->plugin->source_pause(state->plugin, i);
        }
    } else {
        // Pause the given channel.
        state->plugin->source_pause(state->plugin, channel_id);
    }
}

void audio_system_channel_resume(i8 channel_id) {
    audio_system_state* state = engine_systems_get()->audio_system;
    if (channel_id < 0) {
        // Resume all channels.
        for (u32 i = 0; i < state->config.audio_channel_count; ++i) {
            state->plugin->source_resume(state->plugin, i);
        }
    } else {
        // Resume the given channel.
        state->plugin->source_resume(state->plugin, channel_id);
    }
}
