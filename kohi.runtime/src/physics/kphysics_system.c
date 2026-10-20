#include "kphysics_system.h"

#include <logger.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <strings/kstring.h>
#include <utils/crc64.h>

#include "core/console.h"
#include "core/engine.h"
#include "plugins/plugin_types.h"
#include "systems/plugin_system.h"

typedef struct kphysics_system_config {
	const char *backend_plugin_name;
	kphysics_backend_config backend;
} kphysics_system_config;

typedef struct kphysics_system_state {
	kruntime_plugin *plugin;
	kphysics_backend_interface *backend;
	/** @brief The snapshot taken by the physics_state_save console command, if any. */
	void *snapshot;
	u64 snapshot_size;
} kphysics_system_state;

static b8 deserialize_config (const char *config_str, kphysics_system_config *out_config) {
	kson_tree tree = {0};
	if (!config_str || !kson_tree_from_string(config_str, &tree)) {
		KERROR("Failed to parse physics system config.");
		return false;
	}

	b8 result = true;
	if (!kson_object_property_value_get_string(&tree.root, "backend_plugin_name", &out_config->backend_plugin_name)) {
		KERROR("Physics system config requires 'backend_plugin_name'.");
		result = false;
	}

	if (!kson_object_property_value_get_vec3(&tree.root, "gravity", &out_config->backend.gravity)) {
		out_config->backend.gravity = (vec3){0.0f, -9.81f, 0.0f};
	}

	i64 max_bodies = 4096;
	if (!kson_object_property_value_get_int(&tree.root, "max_bodies", &max_bodies) || max_bodies < 1) {
		max_bodies = 4096;
	}
	out_config->backend.max_bodies = (u32)max_bodies;

	i64 thread_count = 0;
	if (!kson_object_property_value_get_int(&tree.root, "thread_count", &thread_count) || thread_count < 0) {
		thread_count = 0;
	}
	out_config->backend.thread_count = (u32)thread_count;

	kson_tree_cleanup(&tree);
	return result;
}

static void on_physics_state_hash (console_command_context context) {
	kphysics_system_state *state = context.listener;
	KINFO("Physics state hash at step %llu: %016llx", engine_simulation_step_count_get(), kphysics_state_hash(state));
}

static void on_physics_state_save (console_command_context context) {
	kphysics_system_state *state = context.listener;
	kphysics_state_free(state->snapshot);
	state->snapshot = KNULL;
	state->snapshot_size = 0;
	if (kphysics_state_save(state, &state->snapshot, &state->snapshot_size)) {
		KINFO("Saved physics state at step %llu (%llu bytes).", engine_simulation_step_count_get(), state->snapshot_size);
	}
}

static void on_physics_state_restore (console_command_context context) {
	kphysics_system_state *state = context.listener;
	if (!state->snapshot) {
		KERROR("No saved physics state. Run physics_state_save first.");
		return;
	}
	if (kphysics_state_restore(state, state->snapshot, state->snapshot_size)) {
		KINFO("Restored physics state at step %llu.", engine_simulation_step_count_get());
	}
}

b8 kphysics_system_initialize (u64 *memory_requirement, void *memory, const char *config_str) {
	*memory_requirement = sizeof(kphysics_system_state);
	if (!memory) {
		return true;
	}

	kphysics_system_state *state = memory;
	kzero_memory(state, sizeof(kphysics_system_state));

	kphysics_system_config config = {0};
	if (!deserialize_config(config_str, &config)) {
		return false;
	}

	state->plugin = plugin_system_get(engine_systems_get()->plugin_system, config.backend_plugin_name);
	if (!state->plugin) {
		KERROR("Physics backend plugin '%s' is not loaded. Add it to the plugin_system config.", config.backend_plugin_name);
		string_free(config.backend_plugin_name);
		return false;
	}
	string_free(config.backend_plugin_name);
	state->backend = state->plugin->plugin_state;

	if (!state->backend->initialize(state->backend, &config.backend)) {
		KERROR("Physics backend failed to initialize.");
		return false;
	}

	console_command_register("physics_state_hash", 0, 0, state, on_physics_state_hash);
	console_command_register("physics_state_save", 0, 0, state, on_physics_state_save);
	console_command_register("physics_state_restore", 0, 0, state, on_physics_state_restore);
	KINFO("Physics system initialized (gravity %.2f %.2f %.2f, up to %u bodies).",
		  config.backend.gravity.x, config.backend.gravity.y, config.backend.gravity.z, config.backend.max_bodies);
	return true;
}

void kphysics_system_shutdown (kphysics_system_state *state) {
	if (state && state->backend) {
		console_command_unregister("physics_state_hash");
		console_command_unregister("physics_state_save");
		console_command_unregister("physics_state_restore");
		kphysics_state_free(state->snapshot);
		state->snapshot = KNULL;
		state->backend->shutdown(state->backend);
		state->backend = KNULL;
	}
}

b8 kphysics_system_step (kphysics_system_state *state, f32 step_seconds) {
	return state->backend->step(state->backend, step_seconds);
}

kphysics_body kphysics_body_create (kphysics_system_state *state, const kphysics_body_config *config) {
	return state->backend->body_create(state->backend, config);
}

void kphysics_body_destroy (kphysics_system_state *state, kphysics_body body) {
	if (body == KPHYSICS_BODY_INVALID) {
		return;
	}
	state->backend->body_destroy(state->backend, body);
}

void kphysics_body_transform_get (kphysics_system_state *state, kphysics_body body, vec3 *out_position, quat *out_rotation) {
	state->backend->body_transform_get(state->backend, body, out_position, out_rotation);
}

void kphysics_body_transform_set (kphysics_system_state *state, kphysics_body body, vec3 position, quat rotation) {
	state->backend->body_transform_set(state->backend, body, position, rotation);
}

vec3 kphysics_body_linear_velocity_get (kphysics_system_state *state, kphysics_body body) {
	return state->backend->body_linear_velocity_get(state->backend, body);
}

void kphysics_body_linear_velocity_set (kphysics_system_state *state, kphysics_body body, vec3 velocity) {
	state->backend->body_linear_velocity_set(state->backend, body, velocity);
}

vec3 kphysics_body_angular_velocity_get (kphysics_system_state *state, kphysics_body body) {
	return state->backend->body_angular_velocity_get(state->backend, body);
}

void kphysics_body_angular_velocity_set (kphysics_system_state *state, kphysics_body body, vec3 velocity) {
	state->backend->body_angular_velocity_set(state->backend, body, velocity);
}

void kphysics_body_force_add (kphysics_system_state *state, kphysics_body body, vec3 force) {
	state->backend->body_force_add(state->backend, body, force);
}

void kphysics_body_impulse_add (kphysics_system_state *state, kphysics_body body, vec3 impulse) {
	state->backend->body_impulse_add(state->backend, body, impulse);
}

b8 kphysics_body_is_active (kphysics_system_state *state, kphysics_body body) {
	return state->backend->body_is_active(state->backend, body);
}

b8 kphysics_raycast (kphysics_system_state *state, vec3 origin, vec3 direction, f32 max_distance, kphysics_raycast_hit *out_hit) {
	return state->backend->raycast(state->backend, origin, direction, max_distance, out_hit);
}

b8 kphysics_state_save (kphysics_system_state *state, void **out_data, u64 *out_size) {
	u64 size = state->backend->state_save(state->backend, KNULL, 0);
	void *data = kallocate(size, MEMORY_TAG_PHYSICS);
	if (state->backend->state_save(state->backend, data, size) != size) {
		// The state changed size between the two calls, which only happens if bodies were
		// created or destroyed on another thread.
		kfree(data);
		KERROR("Physics state changed while it was being saved.");
		return false;
	}
	*out_data = data;
	*out_size = size;
	return true;
}

b8 kphysics_state_restore (kphysics_system_state *state, const void *data, u64 size) {
	if (!state->backend->state_restore(state->backend, data, size)) {
		KERROR("Failed to restore physics state. It must come from a world with the same bodies, created in the same order.");
		return false;
	}
	return true;
}

void kphysics_state_free (void *data) {
	if (data) {
		kfree(data);
	}
}

u64 kphysics_state_hash (kphysics_system_state *state) {
	void *data = KNULL;
	u64 size = 0;
	if (!kphysics_state_save(state, &data, &size)) {
		return 0;
	}
	u64 hash = crc64(0, data, size);
	kphysics_state_free(data);
	return hash;
}
