#version 450

const uint SKYBOX_MAX_VIEWS = 4;
const uint SKYBOX_OPTION_IDX_VIEW_INDEX = 0;

// =========================================================
// Inputs
// =========================================================

// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

// per frame
layout(set = 0, binding = 0) uniform per_frame_ubo {
	mat4 views[SKYBOX_MAX_VIEWS];
    mat4 projection;
} skybox_frame_ubo;

// per group NOTE: No per-group UBO for this shader

// per draw 
layout(push_constant) uniform per_draw_ubo {
    uvec4 options;
} skybox_draw_ubo;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
	vec3 tex_coord;
} out_dto;

void main() {
	out_dto.tex_coord = -in_position;
	const uint view_index = skybox_draw_ubo.options[SKYBOX_OPTION_IDX_VIEW_INDEX];
	gl_Position = skybox_frame_ubo.projection * skybox_frame_ubo.views[view_index] * vec4(in_position, 1.0);
} 
