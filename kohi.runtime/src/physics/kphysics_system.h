#pragma once

#include "core_physics_types.h"
#include "identifiers/khandle.h"
#include "physics_types.h"

struct kphysics_system_state;

KAPI b8 kphysics_system_initialize(u64* memory_requirement, struct kphysics_system_state* state, kphysics_system_config* config);

KAPI void kphysics_system_shutdown(struct kphysics_system_state* state);

KAPI b8 kphysics_system_fixed_update(struct kphysics_system_state* state, f64 fixed_update_time);

KAPI b8 kphysics_world_create(struct kphysics_system_state* state, kname name, vec3 gravity, kphysics_world* out_world);
KAPI void kphysics_world_destroy(struct kphysics_system_state* state, kphysics_world* world);
KAPI b8 kphysics_set_world(struct kphysics_system_state* state, kphysics_world* world);

KAPI b8 kphysics_world_add_body(struct kphysics_system_state* state, kphysics_world* world, khandle body);
KAPI b8 kphysics_world_remove_body(struct kphysics_system_state* state, kphysics_world* world, khandle body);

KAPI b8 kphysics_body_create_sphere(struct kphysics_system_state* state, kname name, vec3 position, f32 radius, f32 mass, f32 inertia, khandle* out_body);
KAPI b8 kphysics_body_create_rectangle(struct kphysics_system_state* state, kname name, vec3 position, vec3 half_extents, f32 mass, f32 inertia, khandle* out_body);
KAPI b8 kphysics_body_create_mesh(struct kphysics_system_state* state, kname name, vec3 position, u32 triangle_count, triangle_3d* tris, f32 mass, f32 inertia, khandle* out_body);

KAPI void kphysics_body_destroy(struct kphysics_system_state* state, khandle* body);
KAPI b8 kphysics_body_position_set(struct kphysics_system_state* state, khandle body, vec3 position);
KAPI b8 kphysics_body_rotation_set(struct kphysics_system_state* state, khandle body, quat rotation);
KAPI b8 kphysics_body_rotate(struct kphysics_system_state* state, khandle body, quat rotation);
KAPI b8 kphysics_body_apply_velocity(struct kphysics_system_state* state, khandle body, vec3 velocity);
KAPI b8 kphysics_body_set_force(struct kphysics_system_state* state, khandle body, vec3 force);

KAPI b8 kphysics_body_orientation_get(struct kphysics_system_state* state, khandle body, mat4* out_orientation);
KAPI b8 kphysics_body_velocity_get(struct kphysics_system_state* state, khandle body, vec3* out_velocity);

KAPI b8 kphysics_body_apply_impulse(struct kphysics_system_state* state, khandle body, vec3 point, vec3 force);
