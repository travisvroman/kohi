#version 450

layout(location = 0) out vec4 out_colour;

struct directional_light {
    vec4 colour;
    vec4 direction;
};

struct point_light {
    vec4 colour;
    vec4 position;
    // Usually 1, make sure denominator never gets smaller than 1
    float constant_f;
    // Reduces light intensity linearly
    float linear;
    // Makes the light fall off slower at longer distances.
    float quadratic;
    float padding;
};

const int POINT_LIGHT_MAX = 10;
const int MAX_TERRAIN_MATERIALS = 4;

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
	vec4 ambient_colour;
	vec3 view_position;
	int mode;
    directional_light dir_light;
} global_ubo;

struct material_phong_properties {
    vec4 diffuse_colour;
    vec3 padding;
    float shininess;
};

struct material_terrain_properties {
    material_phong_properties materials[MAX_TERRAIN_MATERIALS];
    vec3 padding;
    int num_materials;
    vec4 padding2;
};

layout(set = 1, binding = 0) uniform instance_uniform_object {
    material_terrain_properties properties;
    point_light p_lights[POINT_LIGHT_MAX];
    vec3 padding;
    int num_p_lights;
} instance_ubo;


// Samplers, diffuse, spec
const int SAMP_DIFFUSE_OFFSET = 0;
const int SAMP_SPECULAR_OFFSET = 1;
const int SAMP_NORMAL_OFFSET = 2;

// diff, spec, normal, diff, spec, normal, etc...
layout(set = 1, binding = 1) uniform sampler2D samplers[3 * MAX_TERRAIN_MATERIALS];

layout(location = 0) flat in int in_mode;
// Data Transfer Object
layout(location = 1) in struct dto {
    vec4 ambient;
	vec2 tex_coord;
	vec3 normal;
	vec3 view_position;
	vec3 frag_position;
    vec4 colour;
	vec3 tangent;
    vec4 mat_weights;
} in_dto;

mat3 TBN;

vec4 calculate_directional_light(directional_light light, vec3 normal, vec3 view_direction, vec4 diff_samp, vec4 spec_samp, material_phong_properties mat_info);
vec4 calculate_point_light(point_light light, vec3 normal, vec3 frag_position, vec3 view_direction, vec4 diff_samp, vec4 spec_samp, material_phong_properties mat_info);

void main() {
    vec3 normal = in_dto.normal;
    vec3 tangent = in_dto.tangent;
    tangent = (tangent - dot(tangent, normal) *  normal);
    vec3 bitangent = cross(in_dto.normal, in_dto.tangent);
    TBN = mat3(tangent, bitangent, normal);

    // Update the normal to use a sample from the normal map.
    vec3 normals[MAX_TERRAIN_MATERIALS];
    vec4 diffuses[MAX_TERRAIN_MATERIALS];
    vec4 specs[MAX_TERRAIN_MATERIALS];

    material_phong_properties mat;
    mat.diffuse_colour = vec4(0);
    mat.shininess = 1.0;

    // Manually sample the first material.
    // TODO: This mix isn't quite right.
    vec4 diff = vec4(1);
    vec4 spec = vec4(0.5);
    vec3 norm = vec3(0, 1, 0);
    for(int m = 0; m < instance_ubo.properties.num_materials; ++m) {
        normals[m] = normalize(TBN * (2.0 * texture(samplers[(m * 3) + SAMP_NORMAL_OFFSET], in_dto.tex_coord).rgb - 1.0));
        diffuses[m] = texture(samplers[(m * 3) + SAMP_DIFFUSE_OFFSET], in_dto.tex_coord);
        specs[m] = texture(samplers[(m * 3) + SAMP_SPECULAR_OFFSET], in_dto.tex_coord);

        norm = mix(norm, normals[m], in_dto.mat_weights[m]);
        diff = mix(diff, diffuses[m], in_dto.mat_weights[m]);
        spec = mix(spec, specs[m], in_dto.mat_weights[m]);

        mat.diffuse_colour = mix(mat.diffuse_colour, instance_ubo.properties.materials[m].diffuse_colour, in_dto.mat_weights[m]);
        mat.shininess = mix(mat.shininess, instance_ubo.properties.materials[m].shininess, in_dto.mat_weights[m]);
    }


    if(in_mode == 0 || in_mode == 1) {
        vec3 view_direction = normalize(in_dto.view_position - in_dto.frag_position);

        out_colour = calculate_directional_light(global_ubo.dir_light, norm, view_direction, diff, spec, mat);

        for(int i = 0; i < instance_ubo.num_p_lights; ++i) {
            out_colour += calculate_point_light(instance_ubo.p_lights[i], norm, in_dto.frag_position, view_direction, diff, spec, mat);
        }
        // out_colour += calculate_point_light(global_ubo.p_light_1, normal, in_dto.frag_position, view_direction);
    } else if(in_mode == 2) {
        out_colour = vec4(abs(normal), 1.0);
    }
}

vec4 calculate_directional_light(directional_light light, vec3 normal, vec3 view_direction, vec4 diff_samp, vec4 spec_samp, material_phong_properties mat_info) {
    float diffuse_factor = max(dot(normal, -light.direction.xyz), 0.0);

    vec3 half_direction = normalize(view_direction - light.direction.xyz);
    float specular_factor = pow(max(dot(half_direction, normal), 0.0), mat_info.shininess);

    vec4 ambient = vec4(vec3(in_dto.ambient * mat_info.diffuse_colour), diff_samp.a);
    vec4 diffuse = vec4(vec3(light.colour * diffuse_factor), diff_samp.a);
    vec4 specular = vec4(vec3(light.colour * specular_factor), diff_samp.a);
    
    if(in_mode == 0) {
        diffuse *= diff_samp;
        ambient *= diff_samp;
        specular *= spec_samp;
    }

    return (ambient + diffuse + specular);
}

vec4 calculate_point_light(point_light light, vec3 normal, vec3 frag_position, vec3 view_direction, vec4 diff_samp, vec4 spec_samp, material_phong_properties mat_info) {
    vec3 light_direction =  normalize(light.position.xyz - frag_position);
    float diff = max(dot(normal, light_direction), 0.0);

    vec3 reflect_direction = reflect(-light_direction, normal);
    float spec = pow(max(dot(view_direction, reflect_direction), 0.0), mat_info.shininess);

    // Calculate attenuation, or light falloff over distance.
    float distance = length(light.position.xyz - frag_position);
    float attenuation = 1.0 / (light.constant_f + light.linear * distance + light.quadratic * (distance * distance));

    vec4 ambient = in_dto.ambient;
    vec4 diffuse = light.colour * diff;
    vec4 specular = light.colour * spec;
    
    if(in_mode == 0) {
        diffuse *= diff_samp;
        ambient *= diff_samp;
        specular *= spec_samp;
    }

    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;
    return (ambient + diffuse + specular);
}
