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
// Extended vertex data
layout(location = 5) in ivec4 in_bone_ids;
layout(location = 6) in vec4 in_weights;

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
	animation_skin_data skin = global_animations.animations[immediate.animation_index];
	mat4 bones[] = skin.bones;
	out_dto.tex_coord = in_texcoord;

	// Accumulate bone transform.
	mat4 bone_transform = mat4(1.0);
	float gt = clamp(immediate.geo_type, 0.0, 1.0);
	bone_transform += (bones[in_bone_ids[0]] * in_weights[0]) * gt;
	bone_transform += (bones[in_bone_ids[1]] * in_weights[1]) * gt;
	bone_transform += (bones[in_bone_ids[2]] * in_weights[2]) * gt;
	bone_transform += (bones[in_bone_ids[3]] * in_weights[3]) * gt;

	gl_Position = global_ubo.view_projections[immediate.cascade_index] * model * bone_transform * vec4(in_position, 1.0);
}
