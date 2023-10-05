#include "audio_system.h"

#include "core/logger.h"
#include "core/systems_manager.h"
#include "defines.h"

typedef struct audio_channel {
    f32 volume;
} audio_channel;

typedef struct audio_system_state {
    audio_system_config config;
    audio_plugin plugin;
    f32 master_volume;
    audio_channel channels[MAX_AUDIO_CHANNELS];
} audio_system_state;

b8 audio_system_initialize(u64* memory_requirement, void* state, void* config) {
    if (!memory_requirement || !config) {
        KERROR("Audio system initialization requires valid pointers to memory_requirement and config.");
        return false;
    }
    audio_system_config* typed_config = (audio_system_config*)config;

    if (typed_config->audio_channel_count < 1) {
        KWARN("Invalid audio system config - audio_channel_count must be at least 1. Defaulting to 1.");
        typed_config->audio_channel_count = 1;
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
    plugin_config.max_sources = 8;
    plugin_config.max_buffers = 256;
    plugin_config.chunk_size = typed_config->chunk_size;
    plugin_config.frequency = typed_config->frequency;
    plugin_config.channel_count = typed_config->channel_count;
    return typed_state->plugin.initialize(&typed_state->plugin, plugin_config);
}

/**
 * @brief Shuts down the audio system.
 *
 * @param state The state block of memory.
 */
void audio_system_shutdown(void* state) {
    if (state) {
        audio_system_state* typed_state = (audio_system_state*)state;
        typed_state->plugin.shutdown(&typed_state->plugin);
    }
}

/**
 * @brief Updates the audio system. Should happen once an update cycle.
 */
b8 audio_system_update(void* state, struct frame_data* p_frame_data) {
    audio_system_state* typed_state = (audio_system_state*)state;
    return typed_state->plugin.update(&typed_state->plugin, p_frame_data);
}

b8 audio_system_listener_orientation_set(vec3 position, vec3 forward, vec3 up) {
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    state->plugin.listener_position_set(&state->plugin, position);
    state->plugin.listener_orientation_set(&state->plugin, forward, up);
    return true;
}

struct audio_sound* audio_system_sound_load(const char* path) {
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.load_sound(&state->plugin, path);
}

struct audio_music* audio_system_music_load(const char* path) {
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.load_music(&state->plugin, path);
}

void audio_system_sound_close(struct audio_sound* sound) {
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    state->plugin.sound_close(&state->plugin, sound);
}

void audio_system_music_close(struct audio_music* music) {
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    state->plugin.music_close(&state->plugin, music);
}

b8 audio_system_channel_volume_set(u8 channel_id, f32 volume) {
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    if (channel_id >= state->config.audio_channel_count) {
        KERROR("Channel id %u is outside the range of available channels. Nothing was done.", channel_id);
        return false;
    }
    state->channels[channel_id].volume = KCLAMP(volume, 0.0f, 1.0f);
    return true;
}

static f32 calculate_master_channel_volume(audio_system_state* state, u8 channel_id) {
    if (channel_id >= state->config.audio_channel_count) {
        KWARN("channel id %u is outside the range of available channels. Defaulting to the first channel.");
        channel_id = 0;
    }

    // Use the channel volume, also modified by the master volume.
    return state->master_volume * state->channels[channel_id].volume;
}

b8 audio_system_channel_play(u8 channel_id, struct audio_sound* sound, b8 loop) {
    if (!sound) {
        return false;
    }
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.play_sound_with_volume(&state->plugin, sound, calculate_master_channel_volume(state, channel_id), loop);
}

b8 audio_system_channel_play_music(u8 channel_id, struct audio_music* music, b8 loop) {
    if (!music) {
        return false;
    }
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.play_music_with_volume(&state->plugin, music, calculate_master_channel_volume(state, channel_id), loop);
}

b8 audio_system_emitter_play(u8 channel_id, struct audio_emitter* emitter) {
    if (!emitter) {
        return false;
    }
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.play_emitter(&state->plugin, calculate_master_channel_volume(state, channel_id), emitter);
}

b8 audio_system_emitter_update(u8 channel_id, struct audio_emitter* emitter) {
    if (!emitter) {
        return false;
    }
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.update_emitter(&state->plugin, calculate_master_channel_volume(state, channel_id), emitter);
}

b8 audio_system_emitter_stop(struct audio_emitter* emitter) {
    if (!emitter) {
        return false;
    }
    audio_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_AUDIO);
    return state->plugin.stop_emitter(&state->plugin, emitter);
}
