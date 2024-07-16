#version 450

layout(set = 1, binding = 1) uniform sampler2D reflection_texture;
layout(set = 1, binding = 2) uniform sampler2D refraction_texture;
layout(set = 1, binding = 3) uniform sampler2D dudv_texture;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    float tiling;
    float wave_strength;
    float move_factor;
	float padding;
} instance_ubo;

layout(location = 0) in struct dto {
	vec4 clip_space;
	vec2 texcoord;
    vec2 padding;
    vec3 world_to_camera;
    float padding2;
} in_dto;

layout(location = 0) out vec4 out_colour;

void main() {
    // Perspective division to NDC for texture projection, then to screen space.
    vec2 ndc = (in_dto.clip_space.xy / in_dto.clip_space.w) / 2.0 + 0.5;
    vec2 reflect_texcoord = vec2(ndc.x, ndc.y);
    vec2 refract_texcoord = vec2(ndc.x, -ndc.y);

    // Calculate surface distortion and bring it into [-1.0 - 1.0] range
    vec2 distortion_1 = (texture(dudv_texture, vec2(in_dto.texcoord.x + instance_ubo.move_factor, in_dto.texcoord.y)).rg * 2.0 - 1.0) * instance_ubo.wave_strength;
    vec2 distortion_2 = (texture(dudv_texture, vec2(-in_dto.texcoord.x + instance_ubo.move_factor, in_dto.texcoord.y)).rg * 2.0 - 1.0) * instance_ubo.wave_strength;
    vec2 distortion_total = distortion_1 + distortion_2;

    reflect_texcoord += distortion_total;
    // Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
    reflect_texcoord = clamp(reflect_texcoord, 0.001, 0.999);

    refract_texcoord += distortion_total;
    // Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
    refract_texcoord.x = clamp(refract_texcoord.x, 0.001, 0.999);
    refract_texcoord.y = clamp(refract_texcoord.y, -0.999, -0.001); // Account for flipped y-axis

    vec4 reflect_colour = texture(reflection_texture, reflect_texcoord);
    vec4 refract_colour = texture(refraction_texture, refract_texcoord);

    // Calculate the fresnel effect. TODO: Should read in the water plane normal.
    float fresnel_factor = dot(normalize(in_dto.world_to_camera), vec3(0, 1, 0));
    fresnel_factor = clamp(fresnel_factor, 0.0, 1.0);

    out_colour = mix(reflect_colour, refract_colour, fresnel_factor);
    // out_colour = mix(refract_colour, reflect_colour, fresnel_factor);
    vec4 tint = vec4(0.0, 0.3, 0.5, 1.0);
    out_colour = mix(out_colour, tint, 0.2);
}