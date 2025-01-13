#pragma once

#include "identifiers/khandle.h"
#include "math/math_types.h"
#include "strings/kname.h"
#include <defines.h>

/**
 * The maximum number of individually-controlled channels of audio available, each
 * with separate volume control. These are all nested under a master audio volume.
 */
#define AUDIO_CHANNEL_MAX_COUNT 16

struct kaudio_system_state;
struct frame_data;

b8 kaudio_system_initialize(u64* memory_requirement, void* memory, const char* config_str);
void kaudio_system_shutdown(struct kaudio_system_state* state);

/**
 * @brief Updates the audio system. Should happen once an update cycle.
 */
b8 kaudio_system_update(struct kaudio_system_state* state, struct frame_data* p_frame_data);

/**
 * Sets the orientation of the listener. Typically linked to the current camera in the world.
 * @param position The position of the listener.
 * @param forward The listener's forward vector.
 * @param up The listener's up vector.
 * @return True on success; otherwise false.
 */
KAPI b8 kaudio_system_listener_orientation_set(struct kaudio_system_state* state, vec3 position, vec3 forward, vec3 up);

KAPI void kaudio_system_master_volume_set(struct kaudio_system_state* state, f32 volume);
KAPI f32 kaudio_system_master_volume_get(struct kaudio_system_state* state);

/**
 * @brief Sets the volume for the given channel id.
 *
 * @param state A pointer to the sound system state.
 * @param channel_index The index of the channel to adjust volume for.
 * @volume The volume to set. Clamped to a range of [0.0-1.0].
 */
KAPI void kaudio_system_channel_volume_set(struct kaudio_system_state* state, u8 channel_index, f32 volume);
/**
 * @brief Queries the given channel's volume volume.
 *
 * @param state A pointer to the sound system state.
 * @param channel_index The id of the channel to query.
 * @return The channel's volume.
 */
KAPI f32 kaudio_system_channel_volume_get(struct kaudio_system_state* state, u8 channel_index);

KAPI b8 kaudio_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, b8 is_streaming, khandle* out_audio);
KAPI void kaudio_release(struct kaudio_system_state* state, khandle* instance);
KAPI b8 kaudio_play(struct kaudio_system_state* state, khandle audio, u8 channel_index);

KAPI b8 kaudio_is_valid(struct kaudio_system_state* state, khandle audio);
KAPI f32 kaudio_pitch_get(struct kaudio_system_state* state, khandle audio);
KAPI b8 kaudio_pitch_set(struct kaudio_system_state* state, khandle audio, f32 pitch);
KAPI f32 kaudio_volume_get(struct kaudio_system_state* state, khandle audio);
KAPI b8 kaudio_volume_set(struct kaudio_system_state* state, khandle audio, f32 volume);
KAPI b8 kaudio_looping_get(struct kaudio_system_state* state, khandle audio);
KAPI b8 kaudio_looping_set(struct kaudio_system_state* state, khandle audio, b8 looping);

// KAPI b8 kaudio_seek(struct kaudio_system_state* state, khandle audio, f32 seconds);
// KAPI f32 kaudio_time_played_get(struct kaudio_system_state* state, khandle audio);
// KAPI f32 kaudio_time_length_get(struct kaudio_system_state* state, khandle audio);

KAPI b8 kaudio_channel_play(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_pause(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_resume(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_stop(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_is_playing(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_is_paused(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_is_stopped(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_looping_get(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_looping_set(struct kaudio_system_state* state, u8 channel_index, b8 looping);
KAPI f32 kaudio_channel_volume_get(struct kaudio_system_state* state, u8 channel_index);
KAPI b8 kaudio_channel_volume_set(struct kaudio_system_state* state, u8 channel_index, f32 volume);
