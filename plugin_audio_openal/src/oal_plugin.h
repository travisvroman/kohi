#pragma once

#include <audio/audio_types.h>
#include <defines.h>

struct audio_plugin_source;

b8 oal_plugin_initialize(struct audio_plugin* plugin, audio_plugin_config config);

void oal_plugin_shutdown(struct audio_plugin* plugin);

b8 oal_plugin_update(struct audio_plugin* plugin, struct frame_data* p_frame_data);

b8 oal_plugin_listener_position_query(struct audio_plugin* plugin, vec3* out_position);
b8 oal_plugin_listener_position_set(struct audio_plugin* plugin, vec3 position);

b8 oal_plugin_listener_orientation_query(struct audio_plugin* plugin, vec3* out_forward, vec3* out_up);
b8 oal_plugin_listener_orientation_set(struct audio_plugin* plugin, vec3 forward, vec3 up);

struct audio_plugin_source* oal_plugin_find_free_source(struct audio_plugin* plugin);
b8 oal_plugin_source_reset(struct audio_plugin* plugin, struct audio_plugin_source* source, b8 reset_use);

b8 oal_plugin_source_gain_query(struct audio_plugin* plugin, u32 source_id, f32* out_gain);
b8 oal_plugin_source_gain_set(struct audio_plugin* plugin, u32 source_id, f32 gain);

b8 oal_plugin_source_pitch_query(struct audio_plugin* plugin, u32 source_id, f32* out_pitch);
b8 oal_plugin_source_pitch_set(struct audio_plugin* plugin, u32 source_id, f32 pitch);

b8 oal_plugin_source_position_query(struct audio_plugin* plugin, u32 source_id, vec3* out_position);
b8 oal_plugin_source_position_set(struct audio_plugin* plugin, u32 source_id, vec3 position);

b8 oal_plugin_source_looping_query(struct audio_plugin* plugin, u32 source_id, b8* out_looping);
b8 oal_plugin_source_looping_set(struct audio_plugin* plugin, u32 source_id, b8 looping);

struct audio_sound* oal_plugin_load_sound(struct audio_plugin* plugin, const char* path);
struct audio_music* oal_plugin_load_music(struct audio_plugin* plugin, const char* path);
void oal_plugin_sound_close(struct audio_plugin* plugin, struct audio_sound* sound);
void oal_plugin_music_close(struct audio_plugin* plugin, struct audio_music* music);

// new api
b8 oal_plugin_source_play(struct audio_plugin* plugin, i8 source_index);
b8 oal_plugin_sound_play_on_source(struct audio_plugin* plugin, struct audio_sound* sound, i8 source_index, b8 loop);
b8 oal_plugin_music_play_on_source(struct audio_plugin* plugin, struct audio_music* music, i8 source_index, b8 loop);

b8 oal_plugin_source_stop(struct audio_plugin* plugin, i8 source_index);
b8 oal_plugin_source_pause(struct audio_plugin* plugin, i8 source_index);
b8 oal_plugin_source_resume(struct audio_plugin* plugin, i8 source_index);
