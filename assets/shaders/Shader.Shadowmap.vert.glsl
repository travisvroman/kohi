#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

// TODO: re-enable these once a single pass is achieved.
/* layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
} global_ubo;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    vec4 rubbish;
} instance_ubo; */

layout(set = 0, binding = 0) uniform instance_uniform_object {
    mat4 projection;
	mat4 view;
    vec4 rubbish;
} instance_ubo;

layout(push_constant) uniform push_constants {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} local_ubo;

// Data Transfer Object
layout(location = 1) out struct dto {
	vec2 tex_coord;
} out_dto;

void main() {
    out_dto.tex_coord = in_texcoord;
    gl_Position = (instance_ubo.projection * instance_ubo.view) * local_ubo.model * vec4(in_position, 1.0);
}
