#pragma once

#include <physics/kphysics_types.h>

b8 jolt_backend_initialize (kphysics_backend_interface *backend, const kphysics_backend_config *config);
void jolt_backend_shutdown (kphysics_backend_interface *backend);
b8 jolt_backend_step (kphysics_backend_interface *backend, f32 step_seconds);

kphysics_body jolt_backend_body_create (kphysics_backend_interface *backend, const kphysics_body_config *config);
void jolt_backend_body_destroy (kphysics_backend_interface *backend, kphysics_body body);
void jolt_backend_body_transform_get (kphysics_backend_interface *backend, kphysics_body body, vec3 *out_position, quat *out_rotation);
void jolt_backend_body_transform_set (kphysics_backend_interface *backend, kphysics_body body, vec3 position, quat rotation);
vec3 jolt_backend_body_linear_velocity_get (kphysics_backend_interface *backend, kphysics_body body);
void jolt_backend_body_linear_velocity_set (kphysics_backend_interface *backend, kphysics_body body, vec3 velocity);
vec3 jolt_backend_body_angular_velocity_get (kphysics_backend_interface *backend, kphysics_body body);
void jolt_backend_body_angular_velocity_set (kphysics_backend_interface *backend, kphysics_body body, vec3 velocity);
void jolt_backend_body_force_add (kphysics_backend_interface *backend, kphysics_body body, vec3 force);
void jolt_backend_body_impulse_add (kphysics_backend_interface *backend, kphysics_body body, vec3 impulse);
b8 jolt_backend_body_is_active (kphysics_backend_interface *backend, kphysics_body body);

b8 jolt_backend_raycast (kphysics_backend_interface *backend, vec3 origin, vec3 direction, f32 max_distance, kphysics_raycast_hit *out_hit);

u64 jolt_backend_state_save (kphysics_backend_interface *backend, void *buffer, u64 capacity);
b8 jolt_backend_state_restore (kphysics_backend_interface *backend, const void *data, u64 size);
