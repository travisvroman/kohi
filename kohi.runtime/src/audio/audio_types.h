#pragma once

#include <math/math_types.h>
#include <platform/filesystem.h>

#include "defines.h"

struct audio_plugin_state;
struct frame_data;

struct audio_system_config;
struct resource;

// TODO: delete this
typedef enum audio_file_type {
    AUDIO_FILE_TYPE_SOUND_EFFECT,
    AUDIO_FILE_TYPE_MUSIC_STREAM
} audio_file_type;

// TODO: delete this
typedef struct audio_file {
    // The type of audio file.
    audio_file_type type;
    struct resource* audio_resource;
    // The format (i.e. 16 bit stereo)
    u32 format;
    // The number of channels (i.e. 1 for mono or 2 for stereo)
    i32 channels;
    // The sample rate of the sound/music (i.e. 44100)
    u32 sample_rate;
    // Used to track samples in streaming type files.
    u32 total_samples_left;
    struct audio_file_internal* internal_data;
    struct audio_file_plugin_data* plugin_data;

    u64 (*load_samples)(struct audio_file* audio, u32 chunk_size, i32 count);
    void* (*stream_buffer_data)(struct audio_file* audio);
    void (*rewind)(struct audio_file* audio);

} audio_file;

// TODO: delete this
typedef struct audio_emitter {
    vec3 position;
    f32 volume;
    f32 falloff;
    b8 looping;
    struct audio_file* file;
    u32 source_id;
} audio_emitter;

typedef struct audio_backend_interface {
    struct audio_plugin_state* internal_state;

    b8 (*initialize)(struct audio_backend_interface* plugin, const struct audio_system_config* config, const char* plugin_config);

    void (*shutdown)(struct audio_backend_interface* plugin);

    b8 (*update)(struct audio_backend_interface* plugin, struct frame_data* p_frame_data);

    b8 (*listener_position_query)(struct audio_backend_interface* plugin, vec3* out_position);
    b8 (*listener_position_set)(struct audio_backend_interface* plugin, vec3 position);

    b8 (*listener_orientation_query)(struct audio_backend_interface* plugin, vec3* out_forward, vec3* out_up);
    b8 (*listener_orientation_set)(struct audio_backend_interface* plugin, vec3 forward, vec3 up);

    b8 (*source_gain_query)(struct audio_backend_interface* plugin, u32 source_id, f32* out_gain);
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
    b8 (*source_gain_set)(struct audio_backend_interface* plugin, u32 source_id, f32 gain);

    b8 (*source_pitch_query)(struct audio_backend_interface* plugin, u32 source_id, f32* out_pitch);

    /**
     * @param plugin A pointer to the plugin.
     * @param source_id The identifier of the source to modify.
     * @param Specify the pitch to be applied at source. Range: [0.5f - 2.0f] Default: 1.0f
     * @returns True on success; otherwise false.
     */
    b8 (*source_pitch_set)(struct audio_backend_interface* plugin, u32 source_id, f32 pitch);

    b8 (*source_position_query)(struct audio_backend_interface* plugin, u32 source_id, vec3* out_position);
    b8 (*source_position_set)(struct audio_backend_interface* plugin, u32 source_id, vec3 position);

    b8 (*source_looping_query)(struct audio_backend_interface* plugin, u32 source_id, b8* out_looping);
    b8 (*source_looping_set)(struct audio_backend_interface* plugin, u32 source_id, b8 looping);

    struct audio_file* (*chunk_load)(struct audio_backend_interface* plugin, const char* name);
    struct audio_file* (*stream_load)(struct audio_backend_interface* plugin, const char* name);
    void (*audio_unload)(struct audio_backend_interface* plugin, struct audio_file* file);

    b8 (*source_play)(struct audio_backend_interface* plugin, i8 source_index);
    b8 (*play_on_source)(struct audio_backend_interface* plugin, struct audio_file* file, i8 source_index);

    b8 (*source_stop)(struct audio_backend_interface* plugin, i8 source_index);
    b8 (*source_pause)(struct audio_backend_interface* plugin, i8 source_index);
    b8 (*source_resume)(struct audio_backend_interface* plugin, i8 source_index);

} audio_backend_interface;
