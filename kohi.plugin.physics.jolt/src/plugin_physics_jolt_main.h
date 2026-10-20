#pragma once

#include <defines.h>

struct kruntime_plugin;

// Plugin entry points, found by name by the plugin system.
KAPI b8 kohi_plugin_physics_jolt_create (struct kruntime_plugin *out_plugin);
KAPI void kohi_plugin_physics_jolt_destroy (struct kruntime_plugin *plugin);
