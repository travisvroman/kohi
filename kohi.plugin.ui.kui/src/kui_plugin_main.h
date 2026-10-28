/**
 * @file kui_plugin_main.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The main interface for the Kohi UI (KUI) plugin.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#include "kui_system.h"
#include <defines.h>

struct kruntime_plugin;
struct frame_data;
struct kwindow;
struct kui_state;
struct kui_render_data;

typedef struct kui_plugin_state {
	u64 state_memory_requirement;
	struct kui_state *state;
	struct kui_render_data *render_data;
} kui_plugin_state;

KAPI b8 kohi_plugin_ui_kui_create (struct kruntime_plugin *out_plugin);
KAPI b8 kohi_plugin_ui_kui_initialize (struct kruntime_plugin *plugin);
KAPI void kohi_plugin_ui_kui_destroy (struct kruntime_plugin *plugin);

KAPI b8 kohi_plugin_ui_kui_update (struct kruntime_plugin *plugin, struct frame_data *p_frame_data);
KAPI b8 kohi_plugin_ui_kui_frame_prepare (struct kruntime_plugin *plugin, struct frame_data *p_frame_data);
// NOTE: Actual rendering handled by configured rendergraph node
/* KAPI b8 kplugin_render(struct kruntime_plugin* plugin, struct frame_data* p_frame_data); */

KAPI void kohi_plugin_ui_kui_on_window_resized (void *plugin_state, struct kwindow *window, u16 width, u16 height);
