#include "plugin_system.h"

#include "containers/darray.h"
#include "logger.h"
#include "platform/platform.h"
#include "plugins/plugin_types.h"

typedef struct plugin_system_state {
    // darray
    kruntime_plugin* plugins;
} plugin_system_state;

b8 plugin_system_intialize(u64* memory_requirement, struct plugin_system_state* state, struct plugin_system_config* config) {
    if (!memory_requirement) {
        return false;
    }

    *memory_requirement = sizeof(plugin_system_state);

    if (!state) {
        return true;
    }

    state->plugins = darray_create(kruntime_plugin);

    // Stand up all plugins in config. Don't initialize them yet, just create them.
    u32 plugin_count = darray_length(config->plugins);
    for (u32 i = 0; i < plugin_count; ++i) {
        plugin_system_plugin_config* plugin = &config->plugins[i];

        // TODO: Resolve configuration per plugin.
        if (!plugin_system_load_plugin(state, plugin->name, "")) {
            // Warn about it, but move on.
            KERROR("Plugin '%s' creation failed during plugin system boot.", plugin->name);
        }
    }

    return true;
}

void plugin_system_shutdown(struct plugin_system_state* state) {
    if (state) {
        if (state->plugins) {
            u32 plugin_count = darray_length(state->plugins);
            for (u32 i = 0; i < plugin_count; ++i) {
                kruntime_plugin* plugin = &state->plugins[i];
                if (plugin->kplugin_destroy) {
                    plugin->kplugin_destroy(plugin);
                }
            }
        }
        darray_destroy(state->plugins);
        state->plugins = 0;
    }
}

b8 plugin_system_update(struct plugin_system_state* state, struct frame_data* p_frame_data) {
    if (state && state->plugins) {
        u32 plugin_count = darray_length(state->plugins);
        for (u32 i = 0; i < plugin_count; ++i) {
            kruntime_plugin* plugin = &state->plugins[i];
            if (plugin->kplugin_update) {
                if (!plugin->kplugin_update(plugin, p_frame_data)) {
                    KERROR("Plugin '%s' failed update. See logs for details.", plugin->name);
                }
            }
        }
    }
    return true;
}

b8 plugin_system_frame_prepare(struct plugin_system_state* state, struct frame_data* p_frame_data) {
    if (state && state->plugins) {
        u32 plugin_count = darray_length(state->plugins);
        for (u32 i = 0; i < plugin_count; ++i) {
            kruntime_plugin* plugin = &state->plugins[i];
            if (plugin->kplugin_frame_prepare) {
                if (!plugin->kplugin_frame_prepare(plugin, p_frame_data)) {
                    KERROR("Plugin '%s' failed frame_prepare. See logs for details.", plugin->name);
                }
            }
        }
    }
    return true;
}

b8 plugin_system_render(struct plugin_system_state* state, struct frame_data* p_frame_data) {
    if (state && state->plugins) {
        u32 plugin_count = darray_length(state->plugins);
        for (u32 i = 0; i < plugin_count; ++i) {
            kruntime_plugin* plugin = &state->plugins[i];
            if (plugin->kplugin_render) {
                if (!plugin->kplugin_render(plugin, p_frame_data)) {
                    KERROR("Plugin '%s' failed render. See logs for details.", plugin->name);
                }
            }
        }
    }
    return true;
}

b8 plugin_system_on_window_resize(struct plugin_system_state* state, struct kwindow* window, u16 width, u16 height) {
    if (state && state->plugins) {
        u32 plugin_count = darray_length(state->plugins);
        for (u32 i = 0; i < plugin_count; ++i) {
            kruntime_plugin* plugin = &state->plugins[i];
            if (plugin->kplugin_render) {
                plugin->kplugin_on_window_resized(plugin, window, width, height);
            }
        }
    }
    return true;
}

b8 plugin_system_load_plugin(struct plugin_system_state* state, const char* name, const char* config) {
    if (!state) {
        return false;
    }

    if (!name) {
        KERROR("plugin_system_load_plugin requires a name!");
        return false;
    }

    kruntime_plugin new_plugin = {0};

    // Load the plugin library.
    if (!platform_dynamic_library_load(name, &new_plugin.library)) {
        KERROR("Failed to load library for plugin '%s'. See logs for details.", name);
        return false;
    }

    // kplugin_create is required. This should fail if it does not exist.
    PFN_kruntime_plugin_create plugin_create = platform_dynamic_library_load_function("kplugin_create", &new_plugin.library);
    if (!plugin_create) {
        KERROR("Required function kplugin_create does not exist in library '%s'. Plugin load failed.");
        return false;
    }

    // kplugin_destroy is required. This should fail if it does not exist.
    new_plugin.kplugin_destroy = platform_dynamic_library_load_function("kplugin_destroy", &new_plugin.library);
    if (!new_plugin.kplugin_destroy) {
        KERROR("Required function kplugin_destroy does not exist in library '%s'. Plugin load failed.");
        return false;
    }

    // Load optional hook functions.
    new_plugin.kplugin_initialize = platform_dynamic_library_load_function("kplugin_initialize", &new_plugin.library);
    new_plugin.kplugin_update = platform_dynamic_library_load_function("kplugin_update", &new_plugin.library);
    new_plugin.kplugin_initialize = platform_dynamic_library_load_function("kplugin_frame_prepare", &new_plugin.library);
    new_plugin.kplugin_render = platform_dynamic_library_load_function("kplugin_render", &new_plugin.library);
    new_plugin.kplugin_on_window_resized = platform_dynamic_library_load_function("kplugin_on_window_resized", &new_plugin.library);

    // Invoke plugin creation.
    if (!plugin_create(&new_plugin)) {
        KERROR("plugin_create call failed for plugin '%s'. Plugin load failed.", name);
        return false;
    }

    // Register the plugin
    darray_push(state->plugins, new_plugin);

    KINFO("Plugin '%s' successfully loaded.");
    return true;
}
