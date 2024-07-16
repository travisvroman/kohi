#version 450

layout(location = 0) in vec4 in_position; // NOTE: w is ignored.

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
} global_ubo;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    vec4 dummy;
} instance_ubo;

layout(location = 0) out struct dto {
	vec4 clip_space;
} out_dto;

layout(push_constant) uniform push_constants {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} local_ubo;

void main() {
	out_dto.clip_space = global_ubo.projection * global_ubo.view * local_ubo.model * vec4(in_position.xyz, 1.0);
	gl_Position = out_dto.clip_space;
}