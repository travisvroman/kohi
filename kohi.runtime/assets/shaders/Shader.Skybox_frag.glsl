#version 450

// =========================================================
// Inputs
// =========================================================

// per frame
layout(set = 0, binding = 0) uniform per_frame_ubo {
    mat4 view;
    mat4 projection;
} skybox_frame_ubo;

// per group NOTE: No per-group UBO for this shader
layout(set = 1, binding = 0) uniform textureCube cube_texture;
layout(set = 1, binding = 1) uniform sampler cube_sampler;
// per group NOTE: No per-group UBO for this shader

// Data Transfer Object from vertex shader.
layout(location = 0) in dto {
	vec3 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

void main() {
    out_colour = texture(samplerCube(cube_texture, cube_sampler), in_dto.tex_coord);
} 
