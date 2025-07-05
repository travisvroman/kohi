#pragma once

#include <defines.h>
#include <identifiers/khandle.h>
#include <math/math_types.h>

#include "core_audio_types.h"
#include "kresources/kresource_types.h"

struct frame_data;
struct kaudio_backend_state;

typedef struct kaudio_instance {
    kaudio base;
    u16 instance_id;
} kaudio_instance;

/**
 * @brief The configuration for an audio backend.
 */
typedef struct kaudio_backend_config {
    /** @brief The frequency to output audio at (i.e. 44100). */
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

    /** @brief The maximum number of kaudios (sounds or music) that can be loaded at once. */
    u16 max_count;
} kaudio_backend_config;

typedef struct kaudio_backend_interface {
    struct kaudio_backend_state* internal_state;

    b8 (*initialize)(struct kaudio_backend_interface* backend, const kaudio_backend_config* config);

    void (*shutdown)(struct kaudio_backend_interface* backend);

    b8 (*update)(struct kaudio_backend_interface* backend, struct frame_data* p_frame_data);

    b8 (*listener_position_set)(struct kaudio_backend_interface* backend, vec3 position);

    b8 (*listener_orientation_set)(struct kaudio_backend_interface* backend, vec3 forward, vec3 up);

    /**
     * @param backend A pointer to the backend interface.
     * @param channel_id The identifier of the channel to modify.
     * @param gain Indicate the gain (volume amplification) applied. Range: [0.0f - ? ]
     *  A value of 1.0 means un-attenuated/unchanged. Each division by 2 equals an
     *  attenuation of -6dB. Each multiplicaton with 2 equals an amplification of +6dB.
     *  A value of 0.0f is meaningless with respect to a logarithmic scale; it is
     *  interpreted as zero volume - the channel is effectively disabled.
     * @returns True on success; otherwise false.
     */
    b8 (*channel_gain_set)(struct kaudio_backend_interface* backend, u8 channel_id, f32 gain);

    /**
     * @param backend A pointer to the backend interface.
     * @param channel_id The identifier of the channel to modify.
     * @param Specify the pitch to be applied at source. Range: [0.5f - 2.0f] Default: 1.0f
     * @returns True on success; otherwise false.
     */
    b8 (*channel_pitch_set)(struct kaudio_backend_interface* backend, u8 channel_id, f32 pitch);

    b8 (*channel_position_set)(struct kaudio_backend_interface* backend, u8 channel_id, vec3 position);

    b8 (*channel_looping_set)(struct kaudio_backend_interface* backend, u8 channel_id, b8 looping);

    b8 (*load)(struct kaudio_backend_interface* backend, i32 channels, u32 sample_rate, u32 total_sample_count, u64 pcm_data_size, i16* pcm_data, b8 is_stream, kaudio audio);
    void (*unload)(struct kaudio_backend_interface* backend, kaudio audio);

    // Play whatever is currently bound to the channel.
    b8 (*channel_play)(struct kaudio_backend_interface* backend, u8 channel_id);
    b8 (*channel_play_resource)(struct kaudio_backend_interface* backend, kaudio audio, kaudio_space audio_space, u8 channel_id);

    b8 (*channel_stop)(struct kaudio_backend_interface* backend, u8 channel_id);
    b8 (*channel_pause)(struct kaudio_backend_interface* backend, u8 channel_id);
    b8 (*channel_resume)(struct kaudio_backend_interface* backend, u8 channel_id);

    b8 (*channel_is_playing)(struct kaudio_backend_interface* backend, u8 channel_id);
    b8 (*channel_is_paused)(struct kaudio_backend_interface* backend, u8 channel_id);
    b8 (*channel_is_stopped)(struct kaudio_backend_interface* backend, u8 channel_id);

} kaudio_backend_interface;
