#pragma once

#include <audio/audio_types.h>
#include <defines.h>

struct audio_plugin_source;

b8 oal_plugin_initialize(struct audio_backend_interface* plugin, audio_plugin_config config);

void oal_plugin_shutdown(struct audio_backend_interface* plugin);

b8 oal_plugin_update(struct audio_backend_interface* plugin, struct frame_data* p_frame_data);

b8 oal_plugin_listener_position_query(struct audio_backend_interface* plugin, vec3* out_position);
b8 oal_plugin_listener_position_set(struct audio_backend_interface* plugin, vec3 position);

b8 oal_plugin_listener_orientation_query(struct audio_backend_interface* plugin, vec3* out_forward, vec3* out_up);
b8 oal_plugin_listener_orientation_set(struct audio_backend_interface* plugin, vec3 forward, vec3 up);

b8 oal_plugin_source_gain_query(struct audio_backend_interface* plugin, u32 source_id, f32* out_gain);
b8 oal_plugin_source_gain_set(struct audio_backend_interface* plugin, u32 source_id, f32 gain);

b8 oal_plugin_source_pitch_query(struct audio_backend_interface* plugin, u32 source_id, f32* out_pitch);
b8 oal_plugin_source_pitch_set(struct audio_backend_interface* plugin, u32 source_id, f32 pitch);

b8 oal_plugin_source_position_query(struct audio_backend_interface* plugin, u32 source_id, vec3* out_position);
b8 oal_plugin_source_position_set(struct audio_backend_interface* plugin, u32 source_id, vec3 position);

b8 oal_plugin_source_looping_query(struct audio_backend_interface* plugin, u32 source_id, b8* out_looping);
b8 oal_plugin_source_looping_set(struct audio_backend_interface* plugin, u32 source_id, b8 looping);

struct audio_file* oal_plugin_chunk_load(struct audio_backend_interface* plugin, const char* name);
struct audio_file* oal_plugin_stream_load(struct audio_backend_interface* plugin, const char* name);
void oal_plugin_audio_file_close(struct audio_backend_interface* plugin, struct audio_file* file);

// new api
b8 oal_plugin_source_play(struct audio_backend_interface* plugin, i8 source_index);
b8 oal_plugin_play_on_source(struct audio_backend_interface* plugin, struct audio_file* file, i8 source_index);

b8 oal_plugin_source_stop(struct audio_backend_interface* plugin, i8 source_index);
b8 oal_plugin_source_pause(struct audio_backend_interface* plugin, i8 source_index);
b8 oal_plugin_source_resume(struct audio_backend_interface* plugin, i8 source_index);
