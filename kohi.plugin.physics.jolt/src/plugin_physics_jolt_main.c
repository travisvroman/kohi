#include "plugin_physics_jolt_main.h"

#include <logger.h>
#include <memory/kmemory.h>
#include <physics/kphysics_types.h>
#include <plugins/plugin_types.h>

#include "jolt_backend.h"
#include "kohi.plugin.physics.jolt_version.h"

b8 kohi_plugin_physics_jolt_create (kruntime_plugin *out_plugin) {
	out_plugin->plugin_state_size = sizeof(kphysics_backend_interface);
	out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_PHYSICS);

	kphysics_backend_interface *backend = out_plugin->plugin_state;
	backend->initialize = jolt_backend_initialize;
	backend->shutdown = jolt_backend_shutdown;
	backend->step = jolt_backend_step;

	backend->body_create = jolt_backend_body_create;
	backend->body_destroy = jolt_backend_body_destroy;
	backend->body_transform_get = jolt_backend_body_transform_get;
	backend->body_transform_set = jolt_backend_body_transform_set;
	backend->body_linear_velocity_get = jolt_backend_body_linear_velocity_get;
	backend->body_linear_velocity_set = jolt_backend_body_linear_velocity_set;
	backend->body_angular_velocity_get = jolt_backend_body_angular_velocity_get;
	backend->body_angular_velocity_set = jolt_backend_body_angular_velocity_set;
	backend->body_force_add = jolt_backend_body_force_add;
	backend->body_impulse_add = jolt_backend_body_impulse_add;
	backend->body_is_active = jolt_backend_body_is_active;

	backend->raycast = jolt_backend_raycast;
	backend->state_save = jolt_backend_state_save;
	backend->state_restore = jolt_backend_state_restore;

	KINFO("Jolt physics plugin created (%s).", KVERSION);
	return true;
}

void kohi_plugin_physics_jolt_destroy (kruntime_plugin *plugin) {
	if (plugin && plugin->plugin_state) {
		kfree(plugin->plugin_state);
		plugin->plugin_state = KNULL;
	}
}
