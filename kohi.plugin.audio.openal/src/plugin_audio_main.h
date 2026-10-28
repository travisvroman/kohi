/**
 * @file plugin_audio_main.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The main interface for the OpenAL audio plugin.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <defines.h>

struct kruntime_plugin;

// Plugin entry point.
KAPI b8 kohi_plugin_audio_openal_create (struct kruntime_plugin *out_plugin);
KAPI void kohi_plugin_audio_openal_destroy (struct kruntime_plugin *plugin);
