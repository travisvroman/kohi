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
