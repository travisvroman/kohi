/**
 * @file utils_plugin_main.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Main interface for the utils plugin.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#include <defines.h>

struct kruntime_plugin;

KAPI b8 kohi_plugin_utils_create (struct kruntime_plugin *out_plugin);
KAPI b8 kohi_plugin_utils_initialize (struct kruntime_plugin *plugin);
KAPI void kohi_plugin_utils_destroy (struct kruntime_plugin *plugin);
