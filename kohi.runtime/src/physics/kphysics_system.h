/**
 * @file kphysics_system.h
 * @brief The physics system: rigid bodies simulated by a backend plugin, stepped once per fixed
 * simulation step.
 *
 * Optional. It starts only if the app config has a "physics" system entry, for example:
 *
 *   { name = "physics" config = { backend_plugin_name = "kohi.plugin.physics.jolt" gravity = "0 -9.81 0" } }
 *
 * The engine steps physics after application_fixed_update, so fixed_step_hz should be set; with a
 * variable time step physics still runs, but results then depend on the frame rate.
 */
#pragma once

#include "kphysics_types.h"

struct kphysics_system_state;

b8 kphysics_system_initialize (u64 *memory_requirement, void *memory, const char *config_str);
void kphysics_system_shutdown (struct kphysics_system_state *state);

/** @brief Advances the simulation by one step. Called by the engine; applications don't need to call it. */
b8 kphysics_system_step (struct kphysics_system_state *state, f32 step_seconds);

KAPI kphysics_body kphysics_body_create (struct kphysics_system_state *state, const kphysics_body_config *config);
KAPI void kphysics_body_destroy (struct kphysics_system_state *state, kphysics_body body);

KAPI void kphysics_body_transform_get (struct kphysics_system_state *state, kphysics_body body, vec3 *out_position, quat *out_rotation);
KAPI void kphysics_body_transform_set (struct kphysics_system_state *state, kphysics_body body, vec3 position, quat rotation);
KAPI vec3 kphysics_body_linear_velocity_get (struct kphysics_system_state *state, kphysics_body body);
KAPI void kphysics_body_linear_velocity_set (struct kphysics_system_state *state, kphysics_body body, vec3 velocity);
KAPI vec3 kphysics_body_angular_velocity_get (struct kphysics_system_state *state, kphysics_body body);
KAPI void kphysics_body_angular_velocity_set (struct kphysics_system_state *state, kphysics_body body, vec3 velocity);
KAPI void kphysics_body_force_add (struct kphysics_system_state *state, kphysics_body body, vec3 force);
KAPI void kphysics_body_impulse_add (struct kphysics_system_state *state, kphysics_body body, vec3 impulse);
/** @brief False once a body has come to rest and gone to sleep. */
KAPI b8 kphysics_body_is_active (struct kphysics_system_state *state, kphysics_body body);

/** @brief Finds the closest body along a ray. direction need not be normalized. */
KAPI b8 kphysics_raycast (struct kphysics_system_state *state, vec3 origin, vec3 direction, f32 max_distance, kphysics_raycast_hit *out_hit);

/**
 * @brief Saves the whole simulation state into a newly allocated buffer. Free it with
 * kphysics_state_free.
 */
KAPI b8 kphysics_state_save (struct kphysics_system_state *state, void **out_data, u64 *out_size);
/** @brief Restores a state saved earlier. The same bodies must exist, created in the same order. */
KAPI b8 kphysics_state_restore (struct kphysics_system_state *state, const void *data, u64 size);
KAPI void kphysics_state_free (void *data);
/**
 * @brief A 64-bit hash (CRC-64) of the full simulation state. Two runs that produce the same hash
 * after the same step are in the same state, which makes this the basis of determinism checks.
 */
KAPI u64 kphysics_state_hash (struct kphysics_system_state *state);
