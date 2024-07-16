#version 450

layout(location = 0) in vec4 in_position; // NOTE: w is ignored.

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
	vec3 view_position;
	float padding;
} global_ubo;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    float tiling;
    float wave_strength;
	float move_factor;
	float padding;
} instance_ubo;

layout(location = 0) out struct dto {
	vec4 clip_space;
	vec2 texcoord;
	vec2 padding;
	vec3 world_to_camera;
	float padding2;
} out_dto;

layout(push_constant) uniform push_constants {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} local_ubo;

void main() {
	vec4 world_position = local_ubo.model * in_position;
	out_dto.clip_space = global_ubo.projection * global_ubo.view * world_position;
	gl_Position = out_dto.clip_space;
	out_dto.texcoord = vec2((in_position.x * 0.5) + 0.5, (in_position.z * 0.5) + 0.5) * instance_ubo.tiling;

	vec4 world_position2 = local_ubo.model * vec4(in_position.x, 0, in_position.y, 1.0);
	vec3 view_position = global_ubo.view_position;
	// view_position.y *= -1.0;
	out_dto.world_to_camera = view_position - world_position.xyz;
	// out_dto.world_to_camera = world_position.xyz - view_position;
}