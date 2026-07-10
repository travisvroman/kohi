#include "kui_plugin_main.h"

#include "kohi.plugin.ui.kui_version.h"

#include "kui_types.h"
#include "renderer/kui_renderer.h"
#include <containers/darray.h>
#include <core/frame_data.h>
#include <kui_system.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <plugins/plugin_types.h>

b8 kohi_plugin_ui_kui_create (struct kruntime_plugin *out_plugin) {
	if (!out_plugin) {
		KERROR("Cannot create a plugin without a pointer to hold it, ya dingus!");
		return false;
	}

	out_plugin->plugin_state_size = sizeof(kui_plugin_state);
	out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_PLUGIN);

	KINFO("Kohi UI Plugin Creation successful (%s).", KVERSION);

	return true;
}

b8 kohi_plugin_ui_kui_initialize (struct kruntime_plugin *plugin) {
	if (!plugin) {
		KERROR("Cannot initialize a plugin without a pointer to it, ya dingus!");
		return false;
	}

	kui_plugin_state *plugin_state = plugin->plugin_state;

	kui_system_config kui_cfg = {0};
	kui_system_initialize(&plugin_state->state_memory_requirement, 0, &kui_cfg);
	plugin_state->state = kallocate(plugin_state->state_memory_requirement, MEMORY_TAG_PLUGIN);
	if (!kui_system_initialize(&plugin_state->state_memory_requirement, plugin_state->state, &kui_cfg)) {
		KERROR("Failed to initialize standard ui system.");
		return false;
	}

	return true;
}

void kohi_plugin_ui_kui_destroy (struct kruntime_plugin *plugin) {
	if (plugin) {
		kui_plugin_state *plugin_state = plugin->plugin_state;
		kui_system_shutdown(plugin_state->state);

		kfree(plugin_state->state, plugin_state->state_memory_requirement, MEMORY_TAG_PLUGIN);
		kfree(plugin->plugin_state, plugin->plugin_state_size, MEMORY_TAG_PLUGIN);
	}
}

b8 kohi_plugin_ui_kui_update (struct kruntime_plugin *plugin, struct frame_data *p_frame_data) {
	if (!plugin) {
		return false;
	}

	kui_plugin_state *plugin_state = plugin->plugin_state;
	return kui_system_update(plugin_state->state, p_frame_data);
}

b8 kohi_plugin_ui_kui_frame_prepare (struct kruntime_plugin *plugin, struct frame_data *p_frame_data) {
	if (!plugin) {
		return false;
	}

	kui_plugin_state *plugin_state = plugin->plugin_state;

	plugin_state->render_data = p_frame_data->allocator.allocate(sizeof(kui_render_data));
	plugin_state->render_data->renderables = darray_create_with_allocator(kui_renderable, &p_frame_data->allocator);
	plugin_state->render_data->ui_atlas = plugin_state->state->atlas_texture;

	// NOTE: The time at which this is called is actually imperative to proper operation.
	// This is because the UI typically should be drawn as the last thing in the frame.
	// Might not be able to use this entry point.
	return kui_system_render(plugin_state->state, INVALID_KUI_CONTROL, p_frame_data, plugin_state->render_data);
}

void kohi_plugin_ui_kui_on_window_resized (void *plugin_state, struct kwindow *window, u16 width, u16 height) {
	// TODO: resize logic.
}
