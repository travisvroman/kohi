#version 450

layout(set = 1, binding = 1) uniform sampler2D reflection_texture;
layout(set = 1, binding = 2) uniform sampler2D refraction_texture;

layout(location = 0) in struct dto {
	vec4 clip_space;
} in_dto;

layout(location = 0) out vec4 out_colour;

void main() {
    // Perspective division to NDC for texture projection, then to screen space.
    vec2 ndc = (in_dto.clip_space.xy / in_dto.clip_space.w) / 2.0 + 0.5;
    vec2 refract_texcoord = vec2(ndc.x, -ndc.y);
    vec2 reflect_texcoord = vec2(ndc.x, ndc.y);

    vec4 reflect_colour = texture(reflection_texture, reflect_texcoord);
    vec4 refract_colour = texture(refraction_texture, refract_texcoord);

    out_colour = mix(reflect_colour, refract_colour, 0.5);
}