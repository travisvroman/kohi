/**
 * @file kphysics_types.h
 * @brief Types shared by the physics system and physics backend plugins.
 *
 * The engine talks to physics only through kphysics_backend_interface, so the library behind it
 * (Jolt today, via kohi.plugin.physics.jolt) can be replaced without touching engine code.
 */
#pragma once

#include <defines.h>
#include <math/math_types.h>

struct kphysics_backend_state;

/** @brief A handle to a physics body. */
typedef u32 kphysics_body;

#define KPHYSICS_BODY_INVALID INVALID_ID

typedef enum kphysics_shape_type {
	KPHYSICS_SHAPE_TYPE_SPHERE,
	KPHYSICS_SHAPE_TYPE_BOX,
	/** @brief A capsule along the local Y axis. */
	KPHYSICS_SHAPE_TYPE_CAPSULE
} kphysics_shape_type;

typedef struct kphysics_shape {
	kphysics_shape_type type;
	/** @brief Sphere and capsule radius. */
	f32 radius;
	/** @brief Capsule only: half the length of the cylinder part, excluding the end caps. */
	f32 half_height;
	/** @brief Box only: half the size along each local axis. */
	vec3 half_extents;
} kphysics_shape;

typedef enum kphysics_motion_type {
	/** @brief Never moves (floors, walls). */
	KPHYSICS_MOTION_TYPE_STATIC,
	/** @brief Moved by setting its velocity or transform; pushes dynamic bodies but is not pushed. */
	KPHYSICS_MOTION_TYPE_KINEMATIC,
	/** @brief Moved by forces, gravity and collisions. */
	KPHYSICS_MOTION_TYPE_DYNAMIC
} kphysics_motion_type;

typedef struct kphysics_body_config {
	kphysics_shape shape;
	kphysics_motion_type motion_type;
	vec3 position;
	quat rotation;
	/** @brief Dynamic bodies only. 0 lets the backend compute mass from the shape (density 1000 kg/m^3). */
	f32 mass;
	f32 friction;
	/** @brief Bounciness, 0 to 1. */
	f32 restitution;
	f32 linear_damping;
	f32 angular_damping;
	/** @brief Opaque value stored with the body and returned by queries, for example a scene entity. */
	u64 user_data;
} kphysics_body_config;

/** @brief Returns a body config with sensible defaults for the given shape and motion type. */
KINLINE kphysics_body_config kphysics_body_config_default (kphysics_shape shape, kphysics_motion_type motion_type) {
	kphysics_body_config config = {0};
	config.shape = shape;
	config.motion_type = motion_type;
	config.rotation = (quat){0, 0, 0, 1};
	config.friction = 0.2f;
	config.restitution = 0.0f;
	config.linear_damping = 0.05f;
	config.angular_damping = 0.05f;
	return config;
}

typedef struct kphysics_raycast_hit {
	kphysics_body body;
	u64 user_data;
	vec3 position;
	vec3 normal;
	/** @brief Distance from the ray origin to the hit. */
	f32 distance;
} kphysics_raycast_hit;

typedef struct kphysics_backend_config {
	vec3 gravity;
	u32 max_bodies;
	/** @brief Worker threads for the physics step. 0 runs the step on the calling thread. */
	u32 thread_count;
} kphysics_backend_config;

/**
 * @brief The functions a physics backend plugin provides. All positions are in world space and
 * all rotations are unit quaternions.
 */
typedef struct kphysics_backend_interface {
	struct kphysics_backend_state *internal_state;

	b8 (*initialize)(struct kphysics_backend_interface *backend, const kphysics_backend_config *config);
	void (*shutdown)(struct kphysics_backend_interface *backend);

	/** @brief Advances the simulation by step_seconds. Called once per fixed simulation step. */
	b8 (*step)(struct kphysics_backend_interface *backend, f32 step_seconds);

	kphysics_body (*body_create)(struct kphysics_backend_interface *backend, const kphysics_body_config *config);
	void (*body_destroy)(struct kphysics_backend_interface *backend, kphysics_body body);

	void (*body_transform_get)(struct kphysics_backend_interface *backend, kphysics_body body, vec3 *out_position, quat *out_rotation);
	void (*body_transform_set)(struct kphysics_backend_interface *backend, kphysics_body body, vec3 position, quat rotation);
	vec3 (*body_linear_velocity_get)(struct kphysics_backend_interface *backend, kphysics_body body);
	void (*body_linear_velocity_set)(struct kphysics_backend_interface *backend, kphysics_body body, vec3 velocity);
	vec3 (*body_angular_velocity_get)(struct kphysics_backend_interface *backend, kphysics_body body);
	void (*body_angular_velocity_set)(struct kphysics_backend_interface *backend, kphysics_body body, vec3 velocity);
	/** @brief Applies a force at the centre of mass for the next step. */
	void (*body_force_add)(struct kphysics_backend_interface *backend, kphysics_body body, vec3 force);
	/** @brief Applies an instant change in momentum at the centre of mass. */
	void (*body_impulse_add)(struct kphysics_backend_interface *backend, kphysics_body body, vec3 impulse);
	b8 (*body_is_active)(struct kphysics_backend_interface *backend, kphysics_body body);

	/** @brief Finds the closest body hit by a ray from origin along direction (any length) up to max_distance. */
	b8 (*raycast)(struct kphysics_backend_interface *backend, vec3 origin, vec3 direction, f32 max_distance, kphysics_raycast_hit *out_hit);

	/**
	 * @brief Writes the full simulation state into buffer if it fits, and returns the size it needs.
	 * Pass buffer = 0 and capacity = 0 to query the size. The state covers body positions,
	 * velocities, sleep and contact caches, but not which bodies exist.
	 */
	u64 (*state_save)(struct kphysics_backend_interface *backend, void *buffer, u64 capacity);
	/** @brief Restores state from state_save. The same bodies must exist, created in the same order. */
	b8 (*state_restore)(struct kphysics_backend_interface *backend, const void *data, u64 size);
} kphysics_backend_interface;
