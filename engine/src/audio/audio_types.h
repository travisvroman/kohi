#pragma once

#include <math/math_types.h>
#include <platform/filesystem.h>

#include "defines.h"

struct audio_plugin_state;
struct frame_data;

struct audio_file_internal;
struct audio_sound;

typedef struct audio_file {
    char* file_path;

    struct audio_file_internal* internal_data;
    file_handle file;
    u8* raw_data;

    u32 total_samples_left;

} audio_file;

typedef struct audio_emitter {
    vec3 position;
    f32 volume;
    f32 falloff;
    b8 looping;
    struct audio_sound* sound;
} audio_emitter;

typedef struct audio_plugin_config {
    u32 max_sources;
} audio_plugin_config;

typedef struct audio_plugin {
    struct audio_plugin_state* internal_state;

    b8 (*initialize)(struct audio_plugin* plugin, audio_plugin_config config);

    void (*shutdown)(struct audio_plugin* plugin);

    b8 (*update)(struct audio_plugin* plugin, struct frame_data* p_frame_data);

    b8 (*listener_position_query)(struct audio_plugin* plugin, vec3* out_position);
    b8 (*listener_position_set)(struct audio_plugin* plugin, vec3 position);

    b8 (*listener_orientation_query)(struct audio_plugin* plugin, vec3* out_forward, vec3* out_up);
    b8 (*listener_orientation_set)(struct audio_plugin* plugin, vec3 forward, vec3 up);

    b8 (*source_gain_query)(struct audio_plugin* plugin, u32 source_id, f32* out_gain);
    /**
     * @param plugin A pointer to the plugin.
     * @param source_id The identifier of the source to modify.
     * @param gain Indicate the gain (volume amplification) applied. Range: [0.0f - ? ]
     *  A value of 1.0 means un-attenuated/unchanged. Each division by 2 equals an
     *  attenuation of -6dB. Each multiplicaton with 2 equals an amplification of +6dB.
     *  A value of 0.0f is meaningless with respect to a logarithmic scale; it is
     *  interpreted as zero volume - the channel is effectively disabled.
     * @returns True on success; otherwise false.
     */
    b8 (*source_gain_set)(struct audio_plugin* plugin, u32 source_id, f32 gain);

    b8 (*source_pitch_query)(struct audio_plugin* plugin, u32 source_id, f32* out_pitch);

    /**
     * @param plugin A pointer to the plugin.
     * @param source_id The identifier of the source to modify.
     * @param Specify the pitch to be applied at source. Range: [0.5f - 2.0f] Default: 1.0f
     * @returns True on success; otherwise false.
     */
    b8 (*source_pitch_set)(struct audio_plugin* plugin, u32 source_id, f32 pitch);

    b8 (*source_position_query)(struct audio_plugin* plugin, u32 source_id, vec3* out_position);
    b8 (*source_position_set)(struct audio_plugin* plugin, u32 source_id, vec3 position);

    b8 (*source_looping_query)(struct audio_plugin* plugin, u32 source_id, b8* out_looping);
    b8 (*source_looping_set)(struct audio_plugin* plugin, u32 source_id, b8 looping);

    struct audio_sound* (*load_sound)(struct audio_plugin* plugin, const char* path);
    void (*sound_close)(struct audio_plugin* plugin, struct audio_sound* sound);

    b8 (*play_sound_with_volume)(struct audio_plugin* plugin, struct audio_sound* sound, f32 volume);
    b8 (*play_emitter)(struct audio_plugin* plugin, f32 master_volume, struct audio_emitter* emitter);
    b8 (*stop_emitter)(struct audio_plugin* plugin, struct audio_emitter* emitter);

} audio_plugin;
