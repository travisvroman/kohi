#include "plugin_audio_main.h"

#include <audio/audio_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <plugins/plugin_types.h>

#include "kohi.plugin.audio.openal_version.h"
#include "openal_backend.h"

// Plugin entry point.
b8 kplugin_create(kruntime_plugin* out_plugin) {
    out_plugin->plugin_state_size = sizeof(audio_backend_interface);
    out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_AUDIO);

    kaudio_backend_interface* backend = out_plugin->plugin_state;

    // Assign function pointers.
    backend->initialize = openal_backend_initialize;
    backend->shutdown = openal_backend_shutdown;
    backend->update = openal_backend_update;

    backend->listener_position_set = openal_backend_listener_position_set;
    backend->listener_orientation_set = openal_backend_listener_orientation_set;
    backend->channel_gain_set = openal_backend_channel_gain_set;
    backend->channel_pitch_set = openal_backend_channel_pitch_set;
    backend->channel_position_set = openal_backend_channel_position_set;
    backend->channel_looping_set = openal_backend_channel_looping_set;

    backend->resource_load = openal_backend_resource_load;
    backend->resource_unload = openal_backend_resource_unload;

    backend->channel_play = openal_backend_channel_play;
    backend->channel_play_resource = openal_backend_channel_play_resource;

    backend->channel_stop = openal_backend_channel_stop;
    backend->channel_pause = openal_backend_channel_pause;
    backend->channel_resume = openal_backend_channel_resume;

    KINFO("OpenAL Plugin Creation successful (%s).", KVERSION);
    return true;
}

void kplugin_destroy(kruntime_plugin* plugin) {
    if (plugin && plugin->plugin_state) {
        kfree(plugin->plugin_state, plugin->plugin_state_size, MEMORY_TAG_AUDIO);
    }
    kzero_memory(plugin, sizeof(kruntime_plugin));
}
