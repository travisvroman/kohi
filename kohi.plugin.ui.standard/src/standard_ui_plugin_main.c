#include "standard_ui_plugin_main.h"

#include <containers/darray.h>
#include <core/frame_data.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <plugins/plugin_types.h>
#include <standard_ui_system.h>

b8 kplugin_create(struct kruntime_plugin* out_plugin) {
    if (!out_plugin) {
        KERROR("Cannot create a plugin without a pointer to hold it, ya dingus!");
        return false;
    }

    out_plugin->plugin_state_size = sizeof(standard_ui_plugin_state);
    out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_PLUGIN);

    return true;
}

b8 kplugin_initialize(struct kruntime_plugin* plugin) {
    if (!plugin) {
        KERROR("Cannot initialize a plugin without a pointer to it, ya dingus!");
        return false;
    }

    standard_ui_plugin_state* plugin_state = plugin->plugin_state;

    standard_ui_system_config standard_ui_cfg = {0};
    standard_ui_cfg.max_control_count = 1024;
    standard_ui_system_initialize(&plugin_state->sui_state_memory_requirement, 0, &standard_ui_cfg);
    plugin_state->state = kallocate(plugin_state->sui_state_memory_requirement, MEMORY_TAG_PLUGIN);
    if (!standard_ui_system_initialize(&plugin_state->sui_state_memory_requirement, plugin_state->state, &standard_ui_cfg)) {
        KERROR("Failed to initialize standard ui system.");
        return false;
    }

    return true;
}

void kplugin_destroy(struct kruntime_plugin* plugin) {
    if (plugin) {
        standard_ui_plugin_state* plugin_state = plugin->plugin_state;
        standard_ui_system_shutdown(plugin_state->state);
    }
}

b8 kplugin_update(struct kruntime_plugin* plugin, struct frame_data* p_frame_data) {
    if (!plugin) {
        return false;
    }

    standard_ui_plugin_state* plugin_state = plugin->plugin_state;
    return standard_ui_system_update(plugin_state->state, p_frame_data);
}

b8 kplugin_frame_prepare(struct kruntime_plugin* plugin, struct frame_data* p_frame_data) {
    if (!plugin) {
        return false;
    }

    standard_ui_plugin_state* plugin_state = plugin->plugin_state;
    standard_ui_system_render_prepare_frame(plugin_state->state, p_frame_data);

    plugin_state->render_data = p_frame_data->allocator.allocate(sizeof(standard_ui_render_data));
    plugin_state->render_data->renderables = darray_create_with_allocator(standard_ui_renderable, &p_frame_data->allocator);
    plugin_state->render_data->ui_atlas = plugin_state->state->atlas_texture;

    // NOTE: The time at which this is called is actually imperative to proper operation.
    // This is because the UI typically should be drawn as the last thing in the frame.
    // Might not be able to use this entry point.
    return standard_ui_system_render(plugin_state->state, 0, p_frame_data, plugin_state->render_data);
}

void kplugin_on_window_resized(void* plugin_state, struct kwindow* window, u16 width, u16 height) {
    // TODO: resize logic.
}
