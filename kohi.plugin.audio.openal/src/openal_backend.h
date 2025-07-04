#pragma once

#include "core_audio_types.h"
#include <audio/kaudio_types.h>
#include <defines.h>
#include <identifiers/khandle.h>

b8 openal_backend_initialize(kaudio_backend_interface* backend, const kaudio_backend_config* config);

void openal_backend_shutdown(kaudio_backend_interface* backend);

b8 openal_backend_update(kaudio_backend_interface* backend, struct frame_data* p_frame_data);

b8 openal_backend_load(struct kaudio_backend_interface* backend, i32 channels, u32 sample_rate, u32 total_sample_count, u64 pcm_data_size, i16* pcm_data, b8 is_stream, kaudio audio);
void openal_backend_unload(struct kaudio_backend_interface* backend, kaudio audio);

b8 openal_backend_listener_position_set(kaudio_backend_interface* backend, vec3 position);
b8 openal_backend_listener_orientation_set(kaudio_backend_interface* backend, vec3 forward, vec3 up);
b8 openal_backend_channel_gain_set(kaudio_backend_interface* backend, u8 channel_id, f32 gain);
b8 openal_backend_channel_pitch_set(kaudio_backend_interface* backend, u8 channel_id, f32 pitch);
b8 openal_backend_channel_position_set(kaudio_backend_interface* backend, u8 channel_id, vec3 position);
b8 openal_backend_channel_looping_set(kaudio_backend_interface* backend, u8 channel_id, b8 looping);
b8 openal_backend_channel_play(kaudio_backend_interface* backend, u8 channel_id);
b8 openal_backend_channel_play_audio(kaudio_backend_interface* backend, kaudio audio, kaudio_space audio_space, u8 channel_id);
b8 openal_backend_channel_stop(kaudio_backend_interface* backend, u8 channel_id);
b8 openal_backend_channel_pause(kaudio_backend_interface* backend, u8 channel_id);
b8 openal_backend_channel_resume(kaudio_backend_interface* backend, u8 channel_id);

b8 openal_backend_channel_is_playing(kaudio_backend_interface* backend, u8 channel_id);
b8 openal_backend_channel_is_paused(kaudio_backend_interface* backend, u8 channel_id);
b8 openal_backend_channel_is_stopped(kaudio_backend_interface* backend, u8 channel_id);
