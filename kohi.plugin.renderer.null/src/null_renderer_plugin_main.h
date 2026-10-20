/**
 * @file null_renderer_plugin_main.h
 * @brief Entry points for the null renderer backend plugin.
 *
 * The null renderer accepts every renderer call and draws nothing. It needs no GPU, no
 * graphics driver and no display, which makes it suitable for headless runs: CI, servers,
 * and AI agents that drive the engine through its state rather than its pixels.
 *
 * Buffers are backed by CPU memory, so systems that write to mapped buffers (transforms,
 * materials, lights and so on) behave as they would with a real backend. Textures hold no
 * pixel data; reading one back returns zeroes.
 */

#pragma once

#include <plugins/plugin_types.h>

/**
 * @brief Creates a new runtime plugin of the renderer type.
 *
 * @param out_plugin A pointer to hold the newly-created renderer plugin.
 * @return True if successful; otherwise false.
 */
KAPI b8 kohi_plugin_renderer_null_create (kruntime_plugin *out_plugin);

/**
 * @brief Destroys the given plugin.
 *
 * @param plugin A pointer to the runtime plugin to be destroyed.
 */
KAPI void kohi_plugin_renderer_null_destroy (kruntime_plugin *plugin);
