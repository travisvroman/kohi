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

/**
 * @brief A sound effect is a short sound clip designed to be played quickly
 * (i.e. menu sounds, in-world effects such as projectile sounds, etc.).
 * Internally, the entire referenced resource is loaded into a buffer and played.
 *
 * An instance is a single reference to the internal resource, and can be played
 * across multiple sources.
 */
typedef struct ksound_effect_instance {
    // A handle to the underlying resource.
    khandle resource_handle;
    // The instance handle.
    khandle instance;
} ksound_effect_instance;

/**
 * @brief A music "effect" is a longer sound, typically designed to be played
 * over a long time span and generally looped. Internally, the data is streamed
 * from the resource as needed.
 *
 * An instance is a single reference to the internal resource, and can be played
 * across multiple sources.
 */
typedef struct kmusic_instance {
    // A handle to the underlying resource.
    khandle resource_handle;
    // The instance handle.
    khandle instance;
} kmusic_instance;

b8 kaudio_system_initialize(u64* memory_requirement, void* memory, const char* config_str);
void kaudio_system_shutdown(struct kaudio_system_state* state);

/**
 * @brief Updates the audio system. Should happen once an update cycle.
 */
b8 kaudio_system_update(void* state, struct frame_data* p_frame_data);

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

KAPI b8 kaudio_sound_effect_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, ksound_effect_instance* out_instance);
KAPI void kaudio_sound_effect_release(struct kaudio_system_state* state, ksound_effect_instance* instance);
KAPI b8 kaudio_sound_play(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_pause(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_resume(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_stop(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_is_playing(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_is_valid(struct kaudio_system_state* state, ksound_effect_instance instance);
// 0=left, 0.5=center, 1.0=right
KAPI f32 kaudio_sound_pan_get(struct kaudio_system_state* state, ksound_effect_instance instance);
// 0=left, 0.5=center, 1.0=right
KAPI b8 kaudio_sound_pan_set(struct kaudio_system_state* state, ksound_effect_instance instance, f32 pan);
KAPI b8 kaudio_sound_pitch_get(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_pitch_set(struct kaudio_system_state* state, ksound_effect_instance instance, f32 pitch);
KAPI b8 kaudio_sound_volume_get(struct kaudio_system_state* state, ksound_effect_instance instance);
KAPI b8 kaudio_sound_volume_set(struct kaudio_system_state* state, ksound_effect_instance instance, f32 volume);

KAPI b8 kaudio_music_acquire(struct kaudio_system_state* state, kname resource_name, kname package_name, kmusic_instance* out_instance);
KAPI void kaudio_music_release(struct kaudio_system_state* state, kmusic_instance* instance);
KAPI b8 kaudio_music_play(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_pause(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_resume(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_stop(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_seek(struct kaudio_system_state* state, kmusic_instance instance, f32 seconds);
KAPI f32 kaudio_music_time_length_get(struct kaudio_system_state* state, kmusic_instance instance);
KAPI f32 kaudio_music_time_played_get(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_is_playing(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_is_valid(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_looping_get(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_looping_set(struct kaudio_system_state* state, kmusic_instance instance, b8 looping);
// 0=left, 0.5=center, 1.0=right
KAPI f32 kaudio_music_pan_get(struct kaudio_system_state* state, kmusic_instance instance);
// 0=left, 0.5=center, 1.0=right
KAPI b8 kaudio_music_pan_set(struct kaudio_system_state* state, kmusic_instance instance, f32 pan);
KAPI b8 kaudio_music_pitch_get(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_pitch_set(struct kaudio_system_state* state, kmusic_instance instance, f32 pitch);
KAPI b8 kaudio_music_volume_get(struct kaudio_system_state* state, kmusic_instance instance);
KAPI b8 kaudio_music_volume_set(struct kaudio_system_state* state, kmusic_instance instance, f32 volume);
