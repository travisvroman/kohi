#version 450

layout(location = 0) in vec4 in_position; // NOTE: w is ignored.

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
} global_ubo;

layout(push_constant) uniform push_constants {
	mat4 model; // 64 bytes
    vec4 colour;
} local_ubo;


// Data Transfer Object
layout(location = 1) out struct dto {
	vec4 colour;
} out_dto;

void main() {
	out_dto.colour = local_ubo.colour;
	gl_Position = global_ubo.projection * global_ubo.view * local_ubo.model * vec4(in_position.xyz, 1.0);
}
