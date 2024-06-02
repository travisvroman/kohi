#pragma once

#include "defines.h"

struct kruntime_plugin;

// Plugin entry point.
KAPI b8 kplugin_create(struct kruntime_plugin* out_plugin);
KAPI void kplugin_destroy(struct kruntime_plugin* plugin);