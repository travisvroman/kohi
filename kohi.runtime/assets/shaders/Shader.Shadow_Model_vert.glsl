#version 450

#define MAX_CASCADES 4

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

struct animation_skin_data {
	mat4 bones[KANIMATION_SSBO_MAX_BONES_PER_MESH];
};

// =========================================================
// Inputs
// =========================================================

// Vertex input

// Standard vertex data
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

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

layout(push_constant) uniform immediate_data {
	uint mat_ssbo_tran_offset;
	uint transform_index;
	uint cascade_index;
	uint animation_index;
	uint geo_type; // 0=static, 1=animated
} immediate;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader
layout(location = 1) out struct dto {
	vec2 tex_coord;
} out_dto;

void main() {
	mat4 model = global_matrices.matrices[immediate.mat_ssbo_tran_offset + immediate.transform_index];
	out_dto.tex_coord = in_texcoord;

	gl_Position = global_ubo.view_projections[immediate.cascade_index] * model * vec4(in_position, 1.0);
}
