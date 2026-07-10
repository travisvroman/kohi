#pragma once

#include <defines.h>

struct kruntime_plugin;

// Plugin entry point.
KAPI b8 kohi_plugin_audio_openal_create (struct kruntime_plugin *out_plugin);
KAPI void kohi_plugin_audio_openal_destroy (struct kruntime_plugin *plugin);
