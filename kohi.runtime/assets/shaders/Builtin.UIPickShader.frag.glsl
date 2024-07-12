#version 450

layout(location = 0) out vec4 out_colour;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    vec3 id_colour;
} instance_ubo;

void main() {
    out_colour =  vec4(instance_ubo.id_colour, 1.0);
}
