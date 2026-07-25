#version 450

#define MAX_CASCADES 4

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

struct animation_skin_data {
	mat4 bones[KANIMATION_SSBO_MAX_BONES_PER_MESH];
};

// =========================================================
// Inputs
// =========================================================

layout(set = 0, binding = 0) uniform global_ubo_data {
	mat4 view_projections[MAX_CASCADES];
} global_ubo;

// All matrices
layout(std430, set = 0, binding = 1) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

// All animation data
layout(std430, set = 0, binding = 2) readonly buffer global_animations_ssbo {
	animation_skin_data animations[]; // indexed by immediate.animation_index;
} global_animations;

layout (set = 1, binding = 0) uniform texture2D base_colour_texture;
layout (set = 1, binding = 1) uniform sampler base_colour_sampler;

layout(push_constant) uniform immediate_data {
	uint mat_ssbo_tran_offset;
	uint transform_index;
	uint cascade_index;
	uint animation_index;
	uint geo_type; // 0=static, 1=animaten
} immediate;

// Data Transfer Object from vertex shader
layout(location = 1) in struct dto {
	vec2 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================

void main() {
	float alpha = texture(sampler2D(base_colour_texture, base_colour_sampler), in_dto.tex_coord).a;
	if(alpha < 0.5) { // TODO: This should probably be configurable.
		discard;
	}
}
