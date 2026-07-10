/**
 * @file light_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the implementation of the light system, which
 * manages all lighting objects within the engine.
 * @version 1.0
 * @date 2023-03-02
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */
#pragma once

#include <defines.h>
#include <math/math_types.h>

#include "core/engine.h"
#include "core/frame_data.h"
#include "core_resource_types.h"
#include "renderer/renderer_types.h"
#include "utils/kcolour.h"

#define KRENDERBUFFER_NAME_LIGHTING_GLOBAL "Kohi.StorageBuffer.LightingGlobal"

typedef struct klight_attenuation {
	f32 constant_f;
	f32 linear;
	f32 quadratic;
} klight_attenuation;

typedef enum klight_type {
	KLIGHT_TYPE_UNDEFINED,
	KLIGHT_TYPE_POINT,
	KLIGHT_TYPE_DIRECTIONAL
} klight_type;

typedef struct klight_data {
	klight_type type;
	colour3 colour;
	union {
		vec3 position;
		vec3 direction;
	};
	klight_attenuation attenuation;
} klight_data;

typedef u8 klight;
#define KLIGHT_INVALID INVALID_ID_U8

typedef struct klight_render_data {
	klight light;
	ktransform transform;
} klight_render_data;

typedef struct kdirectional_light_data {
	klight light;
	vec3 direction;
} kdirectional_light_data;

// NOTE: If the size of this changes, then klight will need to be a u16 AND the material renderer packed indices
// will have to be upgraded to u16s, effectively doubling the memory requirement for indices in immediates.
#define MAX_GLOBAL_SSBO_LIGHTS 256

// Used as either point or directional light data.
typedef struct light_shader_data {
	/**
	 * Directional light: .rgb = colour, .a = ignored
	 * Point light: .rgb = colour, .a = linear
	 */
	vec4 colour;

	union {
		/**
		 * Used for point lights.
		 * .xyz = position, .w = quadratic
		 */
		vec4 position;

		/**
		 * Used for directionl lights.
		 * .xyz = direction, .w = ignored
		 */
		vec4 direction;
	};
} light_shader_data;

// The large structure of data that lives in the SSBO. This is also
// used to manage the light system itself.
typedef struct light_global_ssbo_data {
	light_shader_data lights[MAX_GLOBAL_SSBO_LIGHTS];
} light_global_ssbo_data;

typedef struct light_system_state {
	krenderbuffer lighting_global_ssbo;

	klight_data *lights;
} light_system_state;

/**
 * @brief Initializes the light system. As with most systems, this should be called
 * twice, the first time to obtain the memory requirement (where memory=0), and a
 * second time passing allocated memory the size of memory_requirement.
 *
 * @param memory_requirement A pointer to hold the memory requirement.
 * @param memory Block of allocated memory, or 0 if requesting memory requirement.
 * @param config Configuration for this system. Currently unused.
 * @return True on success; otherwise false.
 */
b8 light_system_initialize (u64 *memory_requirement, light_system_state *memory, void *config);

/**
 * @brief Shuts down the light system, releasing all resources.
 *
 * @param state The state/memory block for the system.
 */
void light_system_shutdown (light_system_state *state);

void light_system_frame_prepare (light_system_state *state, frame_data *p_frame_data);

KAPI klight point_light_create (light_system_state *state, vec3 position, colour3 colour, f32 constant_f, f32 linear, f32 quadratic);
KAPI klight directional_light_create (light_system_state *state, vec3 direction, colour3 colour);

KAPI vec3 directional_light_get_direction (light_system_state *state, klight light);
KAPI colour3 directional_light_get_colour (light_system_state *state, klight light);
KAPI vec3 point_light_get_position (light_system_state *state, klight light);
KAPI colour3 point_light_get_colour (light_system_state *state, klight light);

KAPI void directional_light_set_direction (light_system_state *state, klight light, vec3 direction);
KAPI void point_light_set_position (light_system_state *state, klight light, vec3 position);
KAPI void point_light_set_colour (light_system_state *state, klight light, colour3 colour);
KAPI f32 point_light_radius_get (light_system_state *state, klight light);

KAPI void light_destroy (light_system_state *state, klight light);

KAPI klight_data light_get_data (light_system_state *state, klight light);
