#include "plugin_system.h"

#include "containers/darray.h"
#include "logger.h"
#include "parsers/kson_parser.h"
#include "platform/platform.h"
#include "plugins/plugin_types.h"
#include "strings/kstring.h"

typedef struct plugin_system_state {
	// darray
	kruntime_plugin *plugins;
} plugin_system_state;

b8 plugin_system_deserialize_config (const char *config_str, plugin_system_config *out_config) {
	if (!config_str || !out_config) {
		KERROR("plugin_system_deserialize_config requires a valid string and a pointer to hold the config.");
		return false;
	}

	kson_tree tree = {0};
	if (!kson_tree_from_string(config_str, &tree)) {
		KERROR("Failed to parse plugin system configuration.");
		return false;
	}

	out_config->plugins = darray_create(plugin_system_plugin_config);

	// Get plugin configs.
	kson_array plugin_configs = {0};
	if (!kson_object_property_value_get_array(&tree.root, "plugins", &plugin_configs)) {
		KERROR("No plugins are configured.");
		return false;
	}

	u32 plugin_count = 0;
	if (!kson_array_element_count_get(&plugin_configs, &plugin_count)) {
		KERROR("Failed to get plugin count.");
		return false;
	}

	// Each plugin.
	for (u32 i = 0; i < plugin_count; ++i) {
		kson_object plugin_config_obj = {0};
		if (!kson_array_element_value_get_object(&plugin_configs, i, &plugin_config_obj)) {
			KERROR("Failed to get plugin config at index %u.", i);
			continue;
		}

		// Name is required.
		plugin_system_plugin_config plugin = {0};
		if (!kson_object_property_value_get_string(&plugin_config_obj, "name", &plugin.name)) {
			KERROR("Unable to get name for plugin at index %u.", i);
			continue;
		}

		// Config is optional at this level. Attempt to extract the object first.
		kson_object plugin_config = {0};
		if (!kson_object_property_value_get_object(&plugin_config_obj, "config", &plugin_config)) {
			// If one doesn't exist, zero it out and move on.
			plugin.config_str = 0;
		} else {
			// If it does exist, convert it back to a string and store it.
			kson_tree config_tree = {0};
			config_tree.root = plugin_config;
			plugin.config_str = kson_tree_to_string(&config_tree);
		}

		// Push into the array.
		darray_push(out_config->plugins, plugin);
	}

	kson_tree_cleanup(&tree);

	return true;
}

void plugin_system_destroy_config (plugin_system_config *config) {
	u32 len = darray_length(config->plugins);
	for (u32 i = 0; i < len; ++i) {
		string_free(config->plugins[i].config_str);
		string_free(config->plugins[i].name);
	}

	darray_destroy(config->plugins);
}

b8 plugin_system_intialize (u64 *memory_requirement, struct plugin_system_state *state, struct plugin_system_config *config) {
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
		plugin_system_plugin_config *plug_config = &config->plugins[i];

		if (!plugin_system_load_plugin(state, plug_config->name, plug_config->config_str)) {
			// Warn about it, but move on.
			KERROR("Plugin '%s' creation failed during plugin system boot.", plug_config->name);
		}
	}

	return true;
}

void plugin_system_shutdown_all_plugins (struct plugin_system_state *state) {
	if (state) {
		if (state->plugins) {
			u32 plugin_count = darray_length(state->plugins);
			for (u32 i = 0; i < plugin_count; ++i) {
				kruntime_plugin *plugin = &state->plugins[i];
				if (plugin->kplugin_destroy) {
					plugin->kplugin_destroy(plugin);
				}
				string_free(plugin->name);
				string_free(plugin->config_str);
				if (!plugin->block_auto_unload) {
					platform_dynamic_library_unload(&plugin->library);
				}
			}
		}
	}
}

void plugin_system_shutdown (struct plugin_system_state *state) {
	if (state) {
		darray_destroy(state->plugins);
		state->plugins = 0;
	}
}

b8 plugin_system_initialize_plugins (struct plugin_system_state *state) {
	if (state && state->plugins) {
		u32 plugin_count = darray_length(state->plugins);
		for (u32 i = 0; i < plugin_count; ++i) {
			kruntime_plugin *plugin = &state->plugins[i];
			// Invoke post-boot-time initialization of the plugin.
			if (plugin->kplugin_initialize) {
				if (!plugin->kplugin_initialize(plugin)) {
					KERROR("Failed to initialize new plugin.");
					return false;
				}
			}
		}
	}
	return true;
}

b8 plugin_system_update_plugins (struct plugin_system_state *state, struct frame_data *p_frame_data) {
	if (state && state->plugins) {
		u32 plugin_count = darray_length(state->plugins);
		for (u32 i = 0; i < plugin_count; ++i) {
			kruntime_plugin *plugin = &state->plugins[i];
			if (plugin->kplugin_update) {
				if (!plugin->kplugin_update(plugin, p_frame_data)) {
					KERROR("Plugin '%s' failed update. See logs for details.", plugin->name);
				}
			}
		}
	}
	return true;
}

b8 plugin_system_frame_prepare_plugins (struct plugin_system_state *state, struct frame_data *p_frame_data) {
	if (state && state->plugins) {
		u32 plugin_count = darray_length(state->plugins);
		for (u32 i = 0; i < plugin_count; ++i) {
			kruntime_plugin *plugin = &state->plugins[i];
			if (plugin->kplugin_frame_prepare) {
				if (!plugin->kplugin_frame_prepare(plugin, p_frame_data)) {
					KERROR("Plugin '%s' failed frame_prepare. See logs for details.", plugin->name);
				}
			}
		}
	}
	return true;
}

b8 plugin_system_render_plugins (struct plugin_system_state *state, struct frame_data *p_frame_data) {
	if (state && state->plugins) {
		u32 plugin_count = darray_length(state->plugins);
		for (u32 i = 0; i < plugin_count; ++i) {
			kruntime_plugin *plugin = &state->plugins[i];
			if (plugin->kplugin_render) {
				if (!plugin->kplugin_render(plugin, p_frame_data)) {
					KERROR("Plugin '%s' failed render. See logs for details.", plugin->name);
				}
			}
		}
	}
	return true;
}

b8 plugin_system_on_window_resize_plugins (struct plugin_system_state *state, struct kwindow *window, u16 width, u16 height) {
	if (state && state->plugins) {
		u32 plugin_count = darray_length(state->plugins);
		for (u32 i = 0; i < plugin_count; ++i) {
			kruntime_plugin *plugin = &state->plugins[i];
			if (plugin->kplugin_render) {
				plugin->kplugin_on_window_resized(plugin, window, width, height);
			}
		}
	}
	return true;
}

static void *get_func_from_tokenized_name (dynamic_library *lib, const char *plugin_name, b8 required, const char *func_name) {
	char *fn_name = string_format("%s_%s", plugin_name, func_name);
	void *pfn = platform_dynamic_library_load_function(fn_name, lib);
	if (!pfn && required) {
		KFATAL("Required function '%s' does not exist in library '%s'. Plugin load failed.", fn_name, lib->name);
		return 0;
	}
	string_free(fn_name);

	return pfn;
}

b8 plugin_system_load_plugin (struct plugin_system_state *state, const char *name, const char *config_str) {
	if (!state) {
		return false;
	}

	if (!name) {
		KERROR("plugin_system_load_plugin requires a name!");
		return false;
	}

	b8 success = false;

	kruntime_plugin new_plugin = {0};
	new_plugin.name = string_duplicate(name);

	char *plugin_fn_prefix = string_duplicate(name);
	string_replace_char_all(plugin_fn_prefix, '.', '_');

	// Load the plugin library.
	if (!platform_dynamic_library_load(name, &new_plugin.library)) {
		KERROR("Failed to load library for plugin '%s'. See logs for details.", name);
		goto plugin_system_load_plugin_cleanup;
	}

	// kplugin_create is required. This should fail if it does not exist.
	PFN_kruntime_plugin_create plugin_create = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, true, "create");
	if (!plugin_create) {
		goto plugin_system_load_plugin_cleanup;
	}

	// kplugin_destroy is required. This should fail if it does not exist.
	new_plugin.kplugin_destroy = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, true, "destroy");

	// Load optional hook functions.
	new_plugin.kplugin_boot = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, false, "boot");
	new_plugin.kplugin_initialize = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, false, "initialize");
	new_plugin.kplugin_update = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, false, "update");
	new_plugin.kplugin_frame_prepare = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, false, "frame_prepare");
	new_plugin.kplugin_render = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, false, "render");
	new_plugin.kplugin_on_window_resized = get_func_from_tokenized_name(&new_plugin.library, plugin_fn_prefix, false, "on_window_resized");

	// Invoke plugin creation.
	if (!plugin_create(&new_plugin)) {
		KERROR("plugin_create call failed for plugin '%s'. Plugin load failed.", name);
		goto plugin_system_load_plugin_cleanup;
	}

	// Invoke boot-time initialization of the plugin.
	if (new_plugin.kplugin_boot) {
		if (!new_plugin.kplugin_boot(&new_plugin)) {
			KERROR("Failed to boot new plugin during creation.");
			goto plugin_system_load_plugin_cleanup;
		}
	}

	// Take a copy of the config string if it exists.
	if (config_str) {
		new_plugin.config_str = string_duplicate(config_str);
	}

	// Register the plugin
	darray_push(state->plugins, new_plugin);

	KINFO("Plugin '%s' successfully loaded.", name);
	success = true;
plugin_system_load_plugin_cleanup:
	string_free(plugin_fn_prefix);

	return success;
}

kruntime_plugin *plugin_system_get (struct plugin_system_state *state, const char *name) {
	if (!state || !name) {
		return 0;
	}

	u32 plugin_count = darray_length(state->plugins);
	for (u32 i = 0; i < plugin_count; ++i) {
		kruntime_plugin *plugin = &state->plugins[i];
		if (strings_equali(name, plugin->name)) {
			return plugin;
		}
	}

	KERROR("No plugin named '%s' found. 0/null is returned.", name);
	return 0;
}
