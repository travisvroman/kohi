#version 450

layout(location = 0) out vec4 out_colour;

struct ui_properties {
    vec4 diffuse_colour;
};

layout(set = 1, binding = 0) uniform local_uniform_object {
    ui_properties properties;
} object_ubo;

// Samplers
const int SAMP_DIFFUSE = 0;
layout(set = 1, binding = 1) uniform sampler2D samplers[1];

// Data Transfer Object
layout(location = 1) in struct dto {
	vec2 tex_coord;
} in_dto;

void main() {
    out_colour =  object_ubo.properties.diffuse_colour * texture(samplers[SAMP_DIFFUSE], in_dto.tex_coord);
}