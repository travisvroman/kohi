#version 450

layout(location = 0) in vec3 tex_coord;

layout(location = 0) out vec4 out_colour;

// Samplers
const int SAMP_DIFFUSE = 0;
layout(set = 1, binding = 0) uniform samplerCube samplers[1];

void main() {
    out_colour = texture(samplers[SAMP_DIFFUSE], tex_coord);
} 