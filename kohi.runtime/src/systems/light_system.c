#include "light_system.h"

#include "core/engine.h"
#include "debug/kassert.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"

static klight create_new_handle (light_system_state *state);

b8 light_system_initialize (u64 *memory_requirement, light_system_state *memory, void *config) {
	*memory_requirement = sizeof(light_system_state);
	if (!memory) {
		return true;
	}

	// NOTE: perform config/init here.

	light_system_state *state = (light_system_state *)memory;
	state->lights = KALLOC_TYPE_CARRAY(klight_data, MAX_GLOBAL_SSBO_LIGHTS);
	// Global lighting storage buffer
	u64 buffer_size = sizeof(light_global_ssbo_data);
	state->lighting_global_ssbo = renderer_renderbuffer_create(engine_systems_get()->renderer_system, kname_create(KRENDERBUFFER_NAME_LIGHTING_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT | RENDERBUFFER_FLAG_TRIPLE_BUFFERED_BIT);
	KASSERT(state->lighting_global_ssbo != KRENDERBUFFER_INVALID);
	KDEBUG("Created lighting global storage buffer.");

	return true;
}

void light_system_shutdown (light_system_state *state) {
	if (state) {
		// NOTE: perform teardown here.
		renderer_renderbuffer_destroy(engine_systems_get()->renderer_system, state->lighting_global_ssbo);
	}
}

void light_system_frame_prepare (light_system_state *state, frame_data *p_frame_data) {
	void *memory = renderer_renderbuffer_get_mapped_memory(engine_systems_get()->renderer_system, state->lighting_global_ssbo);

	for (u16 i = 0; i < MAX_GLOBAL_SSBO_LIGHTS; ++i) {
		klight_data *l = &state->lights[i];
		light_shader_data *sd = &((light_shader_data *)memory)[i];

		if (l->type == KLIGHT_TYPE_POINT) {
			sd->colour = vec4_from_vec3(l->colour, l->attenuation.linear);
			sd->position = vec4_from_vec3(l->position, l->attenuation.quadratic);
		} else if (l->type == KLIGHT_TYPE_DIRECTIONAL) {
			sd->colour = vec4_from_vec3(l->colour, 0);
			sd->direction = vec4_from_vec3(l->direction, 0);
		}

		// NOTE: Other/KLIGHT_TYPE_UNDEFINED are skipped.
	}
}

klight point_light_create (light_system_state *state, vec3 position, colour3 colour, f32 constant_f, f32 linear, f32 quadratic) {
	klight light = create_new_handle(state);
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	l->type = KLIGHT_TYPE_POINT;
	l->colour = colour;
	l->position = position;
	l->attenuation.constant_f = constant_f;
	l->attenuation.linear = linear;
	l->attenuation.quadratic = quadratic;

	return light;
}

klight directional_light_create (light_system_state *state, vec3 direction, colour3 colour) {
	klight light = create_new_handle(state);
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	l->type = KLIGHT_TYPE_DIRECTIONAL;
	l->colour = colour;
	l->direction = direction;

	return light;
}

vec3 directional_light_get_direction (light_system_state *state, klight light) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_DIRECTIONAL);
	return l->direction;
}

colour3 directional_light_get_colour (light_system_state *state, klight light) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_DIRECTIONAL);
	return l->colour;
}

vec3 point_light_get_position (light_system_state *state, klight light) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_POINT);
	return l->position;
}

colour3 point_light_get_colour (light_system_state *state, klight light) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_POINT || l->type == KLIGHT_TYPE_DIRECTIONAL);
	return l->colour;
}

void directional_light_set_direction (light_system_state *state, klight light, vec3 direction) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_DIRECTIONAL);
	l->direction = direction;
}

void point_light_set_position (light_system_state *state, klight light, vec3 position) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_POINT);
	l->position = position;
}

void point_light_set_colour (light_system_state *state, klight light, colour3 colour) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_POINT || l->type == KLIGHT_TYPE_DIRECTIONAL);
	l->colour = colour;
}

f32 point_light_radius_get (light_system_state *state, klight light) {
	KASSERT_DEBUG(light != KLIGHT_INVALID);
	klight_data *l = &state->lights[light];
	KASSERT_DEBUG(l->type == KLIGHT_TYPE_POINT);

	klight_attenuation *att = &l->attenuation;

	f32 intensity = 1.0f;
	f32 threshold = 0.1f;

	if (att->quadratic > K_FLOAT_EPSILON) {
		float disc = att->linear * att->linear - 4.0f * att->quadratic * (att->constant_f - intensity / threshold);
		if (disc <= 0.0f) {
			return 0.0f;
		}
		return KMAX(0.0f, (-att->linear + ksqrt(disc)) / (2.0f * att->quadratic));
	} else if (att->linear > 1e-8f) {
		return KMAX(0.0f, (intensity / threshold - att->constant_f) / att->linear);
	} else {
		return K_INFINITY;
	}
}

void light_destroy (light_system_state *state, klight light) {
	kzero_memory(&state->lights[light], sizeof(klight_data));
}

klight_data light_get_data (light_system_state *state, klight light) {
	return state->lights[light];
}

static klight create_new_handle (light_system_state *state) {
	for (u32 i = 0; i < MAX_GLOBAL_SSBO_LIGHTS; ++i) {
		if (state->lights[i].type == KLIGHT_TYPE_UNDEFINED) {
			return i;
		}
	}

	return KLIGHT_INVALID;
}
