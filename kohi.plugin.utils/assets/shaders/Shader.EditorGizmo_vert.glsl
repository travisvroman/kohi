#version 450

layout(location = 0) in vec4 in_position; // NOTE: w is ignored.
layout(location = 1) in vec4 in_colour;

// All matrices
layout(std430, set = 0, binding = 0) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

layout(push_constant) uniform immediate_data {
	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
	uint mat_ssbo_tran_offset;
	uint mat_ssbo_genc_offset;
	// Indices into the matrix SSBO, offset by above types.
	uint view_index;
	uint projection_index;
	uint model_index;
	uint padding;
} immediate;


// Data Transfer Object
layout(location = 1) out struct dto {
	vec4 colour;
} out_dto;

void main() {
	mat4 view = global_matrices.matrices[immediate.mat_ssbo_view_offset + immediate.view_index];
	mat4 projection = global_matrices.matrices[immediate.mat_ssbo_proj_offset + immediate.projection_index];
	mat4 model = global_matrices.matrices[immediate.mat_ssbo_tran_offset + immediate.model_index];

	out_dto.colour = in_colour;
	gl_Position = projection * view * model * vec4(in_position.xyz, 1.0);
}
