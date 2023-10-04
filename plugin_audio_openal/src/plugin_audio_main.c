#include <AL/al.h>
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

    out_plugin->load_sound = oal_plugin_load_sound;
    out_plugin->sound_close = oal_plugin_sound_close;
    out_plugin->play_sound_with_volume = oal_plugin_play_sound_with_volume;

    out_plugin->play_emitter = oal_plugin_play_emitter;
    out_plugin->stop_emitter = oal_plugin_stop_emitter;

    KINFO("OpenAL Plugin Creation successful.");
    return true;
}
