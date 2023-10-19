#include "plugin_audio_main.h"

#include <audio/audio_types.h>
#include <core/logger.h>
#include <defines.h>

#include "oal_plugin.h"

// Plugin entry point.
b8 plugin_create(struct audio_plugin* out_plugin) {
    // Assign function pointers.
    out_plugin->initialize = oal_plugin_initialize;
    out_plugin->shutdown = oal_plugin_shutdown;
    out_plugin->update = oal_plugin_update;

    out_plugin->listener_position_query = oal_plugin_listener_position_query;
    out_plugin->listener_position_set = oal_plugin_listener_position_set;

    out_plugin->listener_orientation_query = oal_plugin_listener_orientation_query;
    out_plugin->listener_orientation_set = oal_plugin_listener_orientation_set;

    out_plugin->source_gain_query = oal_plugin_source_gain_query;
    out_plugin->source_gain_set = oal_plugin_source_gain_set;

    out_plugin->source_pitch_query = oal_plugin_source_pitch_query;
    out_plugin->source_pitch_set = oal_plugin_source_pitch_set;

    out_plugin->source_position_query = oal_plugin_source_position_query;
    out_plugin->source_position_set = oal_plugin_source_position_set;

    out_plugin->source_looping_query = oal_plugin_source_looping_query;
    out_plugin->source_looping_set = oal_plugin_source_looping_set;

    out_plugin->chunk_load = oal_plugin_chunk_load;
    out_plugin->stream_load = oal_plugin_stream_load;
    out_plugin->audio_unload = oal_plugin_audio_file_close;
    out_plugin->source_play = oal_plugin_source_play;
    out_plugin->play_on_source = oal_plugin_play_on_source;

    out_plugin->source_stop = oal_plugin_source_stop;
    out_plugin->source_pause = oal_plugin_source_pause;
    out_plugin->source_resume = oal_plugin_source_resume;

    KINFO("OpenAL Plugin Creation successful.");
    return true;
}
