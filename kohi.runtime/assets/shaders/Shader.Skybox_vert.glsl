#version 450

// =========================================================
// Inputs
// =========================================================

// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

layout(set = 0, binding = 0) uniform global_ubo_data {
	vec4 fog_colour;
	
	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
} global_settings;

// All matrices
layout(std430, set = 0, binding = 1) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

layout(push_constant) uniform immediate_data {
	uint view_index;
	uint projection_index;
} immediate;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
	vec4 frag_pos;
	vec3 tex_coord;
} out_dto;

void main() {
	mat4 view = global_matrices.matrices[global_settings.mat_ssbo_view_offset + immediate.view_index];
	mat4 projection = global_matrices.matrices[global_settings.mat_ssbo_proj_offset + immediate.projection_index];

	out_dto.tex_coord = -in_position;
	out_dto.frag_pos = vec4(in_position, 1.0);
	gl_Position = projection * view * vec4(in_position, 1.0);
} 
