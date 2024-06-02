#include "plugin_audio_main.h"

#include <audio/audio_types.h>
#include <logger.h>
#include <defines.h>

#include "oal_plugin.h"
#include "kohi.plugin.audio.openal_version.h"
#include <plugins/plugin_types.h>
#include <memory/kmemory.h>

// Plugin entry point.
b8 kplugin_create(kruntime_plugin* out_plugin) {
    out_plugin->plugin_state_size = sizeof(audio_backend_interface);
    out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_AUDIO);

    audio_backend_interface* backend = out_plugin->plugin_state;
    
    // Assign function pointers.
    backend->initialize = oal_plugin_initialize;
    backend->shutdown = oal_plugin_shutdown;
    backend->update = oal_plugin_update;

    backend->listener_position_query = oal_plugin_listener_position_query;
    backend->listener_position_set = oal_plugin_listener_position_set;

    backend->listener_orientation_query = oal_plugin_listener_orientation_query;
    backend->listener_orientation_set = oal_plugin_listener_orientation_set;

    backend->source_gain_query = oal_plugin_source_gain_query;
    backend->source_gain_set = oal_plugin_source_gain_set;

    backend->source_pitch_query = oal_plugin_source_pitch_query;
    backend->source_pitch_set = oal_plugin_source_pitch_set;

    backend->source_position_query = oal_plugin_source_position_query;
    backend->source_position_set = oal_plugin_source_position_set;

    backend->source_looping_query = oal_plugin_source_looping_query;
    backend->source_looping_set = oal_plugin_source_looping_set;

    backend->chunk_load = oal_plugin_chunk_load;
    backend->stream_load = oal_plugin_stream_load;
    backend->audio_unload = oal_plugin_audio_file_close;
    backend->source_play = oal_plugin_source_play;
    backend->play_on_source = oal_plugin_play_on_source;

    backend->source_stop = oal_plugin_source_stop;
    backend->source_pause = oal_plugin_source_pause;
    backend->source_resume = oal_plugin_source_resume;

    KINFO("OpenAL Plugin Creation successful (%s).", KVERSION);
    return true;
}

void kplugin_destroy(kruntime_plugin* plugin) {
    if (plugin && plugin->plugin_state) {
        kfree(plugin->plugin_state, plugin->plugin_state_size, MEMORY_TAG_AUDIO);
    }
    kzero_memory(plugin, sizeof(kruntime_plugin));
}