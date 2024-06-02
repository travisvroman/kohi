#ifndef _KOHI_PLUGIN_SYSTEM_H_
#define _KOHI_PLUGIN_SYSTEM_H_

#include "defines.h"
#include "parsers/kson_parser.h"
#include "plugins/plugin_types.h"

// Build up a system that can load plugins by configured names,
// which is then uses to load shared libraries. It will then load up the
// found kruntime_plugin functions that are available, setup the state
// and register it as globally available to be queried later.
// This should happen early in the bootup process so systems that have
// backend plugins (audio, renderer, etc) have what they need by init time.

struct plugin_system_state;

typedef struct plugin_system_plugin_config {
    // Name of the plugin.
    const char* name;
    // To be deserialized by the plugin itself since it knows how this should be laid out.
    const char* config_str;
} plugin_system_plugin_config;

/**
 * The overall configuration for the plugin system.
 */
typedef struct plugin_system_config {
    // darray The collection of plugin configs.
    plugin_system_plugin_config* plugins;
} plugin_system_config;

struct frame_data;
struct kwindow;

b8 plugin_system_deserialize_config(const char* config_str, plugin_system_config* out_config);

b8 plugin_system_intialize(u64* memory_requirement, struct plugin_system_state* state, struct plugin_system_config* config);

void plugin_system_shutdown(struct plugin_system_state* state);

b8 plugin_system_initialize_plugins(struct plugin_system_state* state);
b8 plugin_system_update_plugins(struct plugin_system_state* state, struct frame_data* p_frame_data);
b8 plugin_system_frame_prepare_plugins(struct plugin_system_state* state, struct frame_data* p_frame_data);
b8 plugin_system_render_plugins(struct plugin_system_state* state, struct frame_data* p_frame_data);

b8 plugin_system_on_window_resize_plugins(struct plugin_system_state* state, struct kwindow* window, u16 width, u16 height);

KAPI b8 plugin_system_load_plugin(struct plugin_system_state* state, const char* name, const char* config);

KAPI kruntime_plugin* plugin_system_get(struct plugin_system_state* state, const char* name);

#endif
