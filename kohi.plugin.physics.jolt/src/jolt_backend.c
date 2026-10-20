#include "jolt_backend.h"

#include <joltc.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>

// Two object layers: static bodies never test against each other, everything else collides.
#define LAYER_NON_MOVING 0
#define LAYER_MOVING 1
#define LAYER_COUNT 2

// Jolt marks an invalid body ID with all bits set.
#define JOLT_INVALID_BODY_ID 0xFFFFFFFFu

// Scratch memory Jolt uses during each step.
#define TEMP_ALLOCATOR_BYTES (16 * 1024 * 1024)

typedef struct kphysics_backend_state {
	JPH_PhysicsSystem *system;
	JPH_BodyInterface *bodies;
	const JPH_NarrowPhaseQuery *query;
	const JPH_BodyLockInterface *locks;
	JPH_JobSystem *job_system;
	JPH_TempAllocator *temp_allocator;
	JPH_ObjectLayerPairFilter *layer_pair_filter;
	JPH_BroadPhaseLayerInterface *broad_phase_layers;
	JPH_ObjectVsBroadPhaseLayerFilter *layer_vs_broad_phase_filter;
} kphysics_backend_state;

// Jolt is initialized once per process (it holds global allocator and type registries).
static u32 jolt_init_count = 0;

static JPH_Vec3 to_jolt_vec3 (vec3 v) { return (JPH_Vec3){v.x, v.y, v.z}; }
static vec3 from_jolt_vec3 (JPH_Vec3 v) { return (vec3){v.x, v.y, v.z}; }
static JPH_Quat to_jolt_quat (quat q) { return (JPH_Quat){q.x, q.y, q.z, q.w}; }
static quat from_jolt_quat (JPH_Quat q) { return (quat){q.x, q.y, q.z, q.w}; }

static void JPH_API_CALL jolt_trace (const char *message) {
	KTRACE("Jolt: %s", message);
}

static bool JPH_API_CALL jolt_assert_failed (const char *expression, const char *message, const char *file, uint32_t line) {
	KERROR("Jolt assertion failed: %s (%s) at %s:%u", expression, message ? message : "", file, line);
	// Returning true asks Jolt to break into the debugger.
	return false;
}

b8 jolt_backend_initialize (kphysics_backend_interface *backend, const kphysics_backend_config *config) {
	if (jolt_init_count == 0) {
		if (!JPH_Init()) {
			KERROR("Failed to initialize Jolt Physics.");
			return false;
		}
		JPH_SetTraceHandler(jolt_trace);
		JPH_SetAssertFailureHandler(jolt_assert_failed);
	}
	jolt_init_count++;

	kphysics_backend_state *state = KALLOC_TYPE(kphysics_backend_state, MEMORY_TAG_PHYSICS);
	backend->internal_state = state;

	state->layer_pair_filter = JPH_ObjectLayerPairFilterTable_Create(LAYER_COUNT);
	JPH_ObjectLayerPairFilterTable_EnableCollision(state->layer_pair_filter, LAYER_NON_MOVING, LAYER_MOVING);
	JPH_ObjectLayerPairFilterTable_EnableCollision(state->layer_pair_filter, LAYER_MOVING, LAYER_MOVING);

	state->broad_phase_layers = JPH_BroadPhaseLayerInterfaceTable_Create(LAYER_COUNT, LAYER_COUNT);
	JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(state->broad_phase_layers, LAYER_NON_MOVING, 0);
	JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(state->broad_phase_layers, LAYER_MOVING, 1);

	state->layer_vs_broad_phase_filter = JPH_ObjectVsBroadPhaseLayerFilterTable_Create(
		state->broad_phase_layers, LAYER_COUNT, state->layer_pair_filter, LAYER_COUNT);

	JPH_PhysicsSystemSettings settings = {0};
	settings.maxBodies = config->max_bodies;
	settings.numBodyMutexes = 0; // Let Jolt choose.
	settings.maxBodyPairs = config->max_bodies * 4;
	settings.maxContactConstraints = config->max_bodies * 4;
	settings.broadPhaseLayerInterface = state->broad_phase_layers;
	settings.objectLayerPairFilter = state->layer_pair_filter;
	settings.objectVsBroadPhaseLayerFilter = state->layer_vs_broad_phase_filter;
	state->system = JPH_PhysicsSystem_Create(&settings);
	if (!state->system) {
		KERROR("Failed to create the Jolt physics system.");
		return false;
	}

	JPH_Vec3 gravity = to_jolt_vec3(config->gravity);
	JPH_PhysicsSystem_SetGravity(state->system, &gravity);

	state->bodies = JPH_PhysicsSystem_GetBodyInterface(state->system);
	state->query = JPH_PhysicsSystem_GetNarrowPhaseQuery(state->system);
	state->locks = JPH_PhysicsSystem_GetBodyLockInterface(state->system);
	state->temp_allocator = JPH_TempAllocator_Create(TEMP_ALLOCATOR_BYTES);

	// Jolt gives the same result for any thread count, so threading is purely a speed choice.
	// numThreads = 0 means the step runs on the calling thread only.
	JobSystemThreadPoolConfig job_config = {0};
	job_config.maxJobs = 2048;
	job_config.maxBarriers = 8;
	job_config.numThreads = (int32_t)config->thread_count;
	state->job_system = JPH_JobSystemThreadPool_Create(&job_config);

	KINFO("Jolt physics backend initialized (%u worker threads).", config->thread_count);
	return true;
}

void jolt_backend_shutdown (kphysics_backend_interface *backend) {
	kphysics_backend_state *state = backend->internal_state;
	if (!state) {
		return;
	}

	if (state->job_system) {
		JPH_JobSystem_Destroy(state->job_system);
	}
	if (state->temp_allocator) {
		JPH_TempAllocator_Destroy(state->temp_allocator);
	}
	if (state->system) {
		JPH_PhysicsSystem_Destroy(state->system);
	}
	// The physics system takes ownership of the layer tables, so they are not destroyed here.

	kfree(state);
	backend->internal_state = KNULL;

	jolt_init_count--;
	if (jolt_init_count == 0) {
		JPH_Shutdown();
	}
}

b8 jolt_backend_step (kphysics_backend_interface *backend, f32 step_seconds) {
	kphysics_backend_state *state = backend->internal_state;
	// One collision step per update; the engine's fixed step is expected to be 1/30 s or shorter.
	JPH_PhysicsUpdateError error = JPH_PhysicsSystem_Update2(state->system, step_seconds, 1, state->temp_allocator, state->job_system);
	if (error != JPH_PhysicsUpdateError_None) {
		// These mean a configured limit (bodies, pairs, contacts) was exceeded. The step still ran,
		// but some collisions were skipped.
		KWARN("Jolt physics update reported error flags 0x%x. Consider raising max_bodies.", (u32)error);
	}
	return true;
}

static JPH_Shape *create_shape (const kphysics_shape *shape) {
	switch (shape->type) {
	case KPHYSICS_SHAPE_TYPE_SPHERE:
		return (JPH_Shape *)JPH_SphereShape_Create(shape->radius);
	case KPHYSICS_SHAPE_TYPE_BOX: {
		JPH_Vec3 half_extents = to_jolt_vec3(shape->half_extents);
		// The convex radius rounds the box's edges; it must be smaller than every half extent.
		f32 smallest = KMIN(half_extents.x, KMIN(half_extents.y, half_extents.z));
		f32 convex_radius = KMIN(JPH_DEFAULT_CONVEX_RADIUS, smallest * 0.5f);
		return (JPH_Shape *)JPH_BoxShape_Create(&half_extents, convex_radius);
	}
	case KPHYSICS_SHAPE_TYPE_CAPSULE:
		return (JPH_Shape *)JPH_CapsuleShape_Create(shape->half_height, shape->radius);
	}
	return KNULL;
}

kphysics_body jolt_backend_body_create (kphysics_backend_interface *backend, const kphysics_body_config *config) {
	kphysics_backend_state *state = backend->internal_state;

	JPH_Shape *shape = create_shape(&config->shape);
	if (!shape) {
		KERROR("Failed to create physics shape of type %d.", config->shape.type);
		return KPHYSICS_BODY_INVALID;
	}

	JPH_MotionType motion_type = JPH_MotionType_Static;
	JPH_ObjectLayer layer = LAYER_NON_MOVING;
	if (config->motion_type == KPHYSICS_MOTION_TYPE_KINEMATIC) {
		motion_type = JPH_MotionType_Kinematic;
		layer = LAYER_MOVING;
	} else if (config->motion_type == KPHYSICS_MOTION_TYPE_DYNAMIC) {
		motion_type = JPH_MotionType_Dynamic;
		layer = LAYER_MOVING;
	}

	JPH_RVec3 position = to_jolt_vec3(config->position);
	JPH_Quat rotation = to_jolt_quat(config->rotation);
	JPH_BodyCreationSettings *settings = JPH_BodyCreationSettings_Create3(shape, &position, &rotation, motion_type, layer);
	JPH_BodyCreationSettings_SetUserData(settings, config->user_data);
	JPH_BodyCreationSettings_SetFriction(settings, config->friction);
	JPH_BodyCreationSettings_SetRestitution(settings, config->restitution);
	JPH_BodyCreationSettings_SetLinearDamping(settings, config->linear_damping);
	JPH_BodyCreationSettings_SetAngularDamping(settings, config->angular_damping);

	if (motion_type == JPH_MotionType_Dynamic && config->mass > 0.0f) {
		JPH_MassProperties mass_properties;
		JPH_Shape_GetMassProperties(shape, &mass_properties);
		JPH_MassProperties_ScaleToMass(&mass_properties, config->mass);
		JPH_BodyCreationSettings_SetOverrideMassProperties(settings, JPH_OverrideMassProperties_MassAndInertiaProvided);
		JPH_BodyCreationSettings_SetMassPropertiesOverride(settings, &mass_properties);
	}

	JPH_Activation activation = (motion_type == JPH_MotionType_Static) ? JPH_Activation_DontActivate : JPH_Activation_Activate;
	JPH_BodyID id = JPH_BodyInterface_CreateAndAddBody(state->bodies, settings, activation);
	JPH_BodyCreationSettings_Destroy(settings);

	if (id == JOLT_INVALID_BODY_ID) {
		KERROR("Jolt could not create a body; the max_bodies limit may have been reached.");
		return KPHYSICS_BODY_INVALID;
	}
	return (kphysics_body)id;
}

void jolt_backend_body_destroy (kphysics_backend_interface *backend, kphysics_body body) {
	JPH_BodyInterface_RemoveAndDestroyBody(backend->internal_state->bodies, (JPH_BodyID)body);
}

void jolt_backend_body_transform_get (kphysics_backend_interface *backend, kphysics_body body, vec3 *out_position, quat *out_rotation) {
	JPH_RVec3 position;
	JPH_Quat rotation;
	JPH_BodyInterface_GetPositionAndRotation(backend->internal_state->bodies, (JPH_BodyID)body, &position, &rotation);
	*out_position = from_jolt_vec3(position);
	*out_rotation = from_jolt_quat(rotation);
}

void jolt_backend_body_transform_set (kphysics_backend_interface *backend, kphysics_body body, vec3 position, quat rotation) {
	JPH_RVec3 p = to_jolt_vec3(position);
	JPH_Quat r = to_jolt_quat(quat_normalize(rotation));
	JPH_BodyInterface_SetPositionAndRotation(backend->internal_state->bodies, (JPH_BodyID)body, &p, &r, JPH_Activation_Activate);
}

vec3 jolt_backend_body_linear_velocity_get (kphysics_backend_interface *backend, kphysics_body body) {
	JPH_Vec3 v;
	JPH_BodyInterface_GetLinearVelocity(backend->internal_state->bodies, (JPH_BodyID)body, &v);
	return from_jolt_vec3(v);
}

void jolt_backend_body_linear_velocity_set (kphysics_backend_interface *backend, kphysics_body body, vec3 velocity) {
	JPH_Vec3 v = to_jolt_vec3(velocity);
	JPH_BodyInterface_SetLinearVelocity(backend->internal_state->bodies, (JPH_BodyID)body, &v);
}

vec3 jolt_backend_body_angular_velocity_get (kphysics_backend_interface *backend, kphysics_body body) {
	JPH_Vec3 v;
	JPH_BodyInterface_GetAngularVelocity(backend->internal_state->bodies, (JPH_BodyID)body, &v);
	return from_jolt_vec3(v);
}

void jolt_backend_body_angular_velocity_set (kphysics_backend_interface *backend, kphysics_body body, vec3 velocity) {
	JPH_Vec3 v = to_jolt_vec3(velocity);
	JPH_BodyInterface_SetAngularVelocity(backend->internal_state->bodies, (JPH_BodyID)body, &v);
}

void jolt_backend_body_force_add (kphysics_backend_interface *backend, kphysics_body body, vec3 force) {
	JPH_Vec3 f = to_jolt_vec3(force);
	JPH_BodyInterface_AddForce(backend->internal_state->bodies, (JPH_BodyID)body, &f);
}

void jolt_backend_body_impulse_add (kphysics_backend_interface *backend, kphysics_body body, vec3 impulse) {
	JPH_Vec3 i = to_jolt_vec3(impulse);
	JPH_BodyInterface_AddImpulse(backend->internal_state->bodies, (JPH_BodyID)body, &i);
}

b8 jolt_backend_body_is_active (kphysics_backend_interface *backend, kphysics_body body) {
	return JPH_BodyInterface_IsActive(backend->internal_state->bodies, (JPH_BodyID)body);
}

b8 jolt_backend_raycast (kphysics_backend_interface *backend, vec3 origin, vec3 direction, f32 max_distance, kphysics_raycast_hit *out_hit) {
	kphysics_backend_state *state = backend->internal_state;

	f32 length = vec3_length(direction);
	if (length <= K_FLOAT_EPSILON || max_distance <= 0.0f) {
		return false;
	}
	// Jolt casts along the full direction vector, reporting hits as a fraction of it.
	vec3 ray = vec3_mul_scalar(direction, max_distance / length);

	JPH_RVec3 jolt_origin = to_jolt_vec3(origin);
	JPH_Vec3 jolt_ray = to_jolt_vec3(ray);
	JPH_RayCastResult result = {0};
	if (!JPH_NarrowPhaseQuery_CastRay(state->query, &jolt_origin, &jolt_ray, &result, KNULL, KNULL, KNULL)) {
		return false;
	}

	vec3 position = vec3_add(origin, vec3_mul_scalar(ray, result.fraction));
	out_hit->body = (kphysics_body)result.bodyID;
	out_hit->user_data = JPH_BodyInterface_GetUserData(state->bodies, result.bodyID);
	out_hit->position = position;
	out_hit->distance = result.fraction * max_distance;

	JPH_BodyLockRead lock;
	JPH_BodyLockInterface_LockRead(state->locks, result.bodyID, &lock);
	if (lock.body) {
		JPH_RVec3 jolt_position = to_jolt_vec3(position);
		JPH_Vec3 normal;
		JPH_Body_GetWorldSpaceSurfaceNormal(lock.body, result.subShapeID2, &jolt_position, &normal);
		out_hit->normal = from_jolt_vec3(normal);
	} else {
		out_hit->normal = vec3_zero();
	}
	JPH_BodyLockInterface_UnlockRead(state->locks, &lock);
	return true;
}

u64 jolt_backend_state_save (kphysics_backend_interface *backend, void *buffer, u64 capacity) {
	return (u64)JPH_PhysicsSystem_SaveState(backend->internal_state->system, buffer, (size_t)capacity);
}

b8 jolt_backend_state_restore (kphysics_backend_interface *backend, const void *data, u64 size) {
	return JPH_PhysicsSystem_RestoreState(backend->internal_state->system, data, (size_t)size);
}
