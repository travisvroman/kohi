#pragma once

#include <math/math_types.h>
#include <platform/filesystem.h>

#include "defines.h"

struct audio_plugin_state;
struct frame_data;

struct sound_file_internal;
struct music_file_internal;
struct audio_sound;
struct audio_music;

typedef struct sound_file {
    char* file_path;

    struct sound_file_internal* internal_data;
    file_handle file;
    u8* raw_data;
} sound_file;

typedef struct music_file {
    char* file_path;

    struct music_file_internal* internal_data;
    file_handle file;
    u8* raw_data;

} music_file;

typedef struct audio_emitter {
    vec3 position;
    f32 volume;
    f32 falloff;
    b8 looping;
    struct audio_sound* sound;
    struct audio_music* music;
    u32 source_id;
} audio_emitter;

typedef struct audio_plugin_config {
    /** @brief The maximum number of buffers available. Default: 256 */
    u32 max_buffers;
    /** @brief The maximum number of sources available. Default: 8 */
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
    struct audio_music* (*load_music)(struct audio_plugin* plugin, const char* path);
    void (*sound_close)(struct audio_plugin* plugin, struct audio_sound* sound);
    void (*music_close)(struct audio_plugin* plugin, struct audio_music* music);

    /* b8 (*play_sound_with_volume)(struct audio_plugin* plugin, struct audio_sound* sound, f32 volume, b8 loop);
    b8 (*play_music_with_volume)(struct audio_plugin* plugin, struct audio_music* music, f32 volume, b8 loop); */
    /* b8 (*play_emitter)(struct audio_plugin* plugin, f32 master_volume, struct audio_emitter* emitter);
    b8 (*update_emitter)(struct audio_plugin* plugin, f32 master_volume, struct audio_emitter* emitter);
    b8 (*stop_emitter)(struct audio_plugin* plugin, struct audio_emitter* emitter); */

    b8 (*source_play)(struct audio_plugin* plugin, i8 source_index);
    b8 (*sound_play_on_source)(struct audio_plugin* plugin, struct audio_sound* sound, i8 source_index, b8 loop);
    b8 (*music_play_on_source)(struct audio_plugin* plugin, struct audio_music* music, i8 source_index, b8 loop);

    b8 (*source_stop)(struct audio_plugin* plugin, i8 source_index);
    b8 (*source_pause)(struct audio_plugin* plugin, i8 source_index);
    b8 (*source_resume)(struct audio_plugin* plugin, i8 source_index);

} audio_plugin;
