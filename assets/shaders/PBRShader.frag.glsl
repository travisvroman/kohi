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

const int MAX_POINT_LIGHTS = 10;
const int MAX_SHADOW_CASCADES = 4;

struct pbr_properties {
    vec4 diffuse_colour;
    vec3 padding;
    float shininess;
};

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
	mat4 light_space[MAX_SHADOW_CASCADES];
    vec4 cascade_splits; // NOTE: 4 splits.
	vec3 view_position;
	int mode;
    int use_pcf;
    float bias;
    vec2 padding;
} global_ubo;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    directional_light dir_light;
    point_light p_lights[MAX_POINT_LIGHTS]; // TODO: move after props
    pbr_properties properties;
    int num_p_lights;
} instance_ubo;

// Material texture indices
const int SAMP_ALBEDO = 0;
const int SAMP_NORMAL = 1;
const int SAMP_METALLIC = 2;
const int SAMP_ROUGHNESS = 3;
const int SAMP_AO = 4;

const float PI = 3.14159265359;
// Material textures: albedo, normal, metallic, roughness, ao
layout(set = 1, binding = 1) uniform sampler2D material_textures[5];
// Shadow maps
layout(set = 1, binding = 2) uniform sampler2D shadow_textures[4];
// Environment map is at the last index.
layout(set = 1, binding = 3) uniform samplerCube irradiance_texture;

layout(location = 0) flat in int in_mode;
layout(location = 1) flat in int use_pcf;
// Data Transfer Object
layout(location = 2) in struct dto {
	vec4 light_space_frag_pos[MAX_SHADOW_CASCADES];
    vec4 cascade_splits;
	vec2 tex_coord;
	vec3 normal;
	vec3 view_position;
	vec3 frag_position;
    vec4 colour;
	vec3 tangent;
    float bias;
    vec3 padding;
} in_dto;


mat3 TBN;

// Percentage-Closer Filtering
float calculate_pcf(vec3 projected, int cascade_index) {
    float shadow = 0.0;
    vec2 texel_size = 1.0 / textureSize(shadow_textures[cascade_index], 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcf_depth = texture(shadow_textures[cascade_index], projected.xy + vec2(x, y) * texel_size).r;
            shadow += projected.z - in_dto.bias > pcf_depth ? 1.0 : 0.0;
        }
    }
    shadow /= 9;
    return 1.0 - shadow;
}

float calculate_unfiltered(vec3 projected, int cascade_index) {
    // Sample the shadow map.
    float map_depth = texture(shadow_textures[cascade_index], projected.xy).r;

    // TODO: cast/get rid of branch.
    float shadow = projected.z - in_dto.bias > map_depth ? 0.0 : 1.0;
    return shadow;
}

// Compare the fragment position against the depth buffer, and if it is further 
// back than the shadow map, it's in shadow. 0.0 = in shadow, 1.0 = not
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, directional_light light, int cascade_index) {
    // Perspective divide - note that while this is pointless for ortho projection,
    // perspective will require this.
    vec3 projected = light_space_frag_pos.xyz / light_space_frag_pos.w;
    // Need to reverse y
    projected.y = 1.0-projected.y;

    // NOTE: Transform to NDC not needed for Vulkan, but would be for OpenGL.
    // projected.xy = projected.xy * 0.5 + 0.5;

    if(use_pcf == 1) {
        return calculate_pcf(projected, cascade_index);
    } 

    return calculate_unfiltered(projected, cascade_index);
}

// Based on a combination of GGX and Schlick-Beckmann approximation to calculate probability
// of overshadowing micro-facets.
float geometry_schlick_ggx(float normal_dot_direction, float roughness) {
    roughness += 1.0;
    float k = (roughness * roughness) / 8.0;
    return normal_dot_direction / (normal_dot_direction * (1.0 - k) + k);
}

vec3 calculate_point_light_radiance(point_light light, vec3 view_direction, vec3 frag_position_xyz);
vec3 calculate_directional_light_radiance(directional_light light, vec3 view_direction);
vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance);

void main() {
    vec3 normal = in_dto.normal;
    vec3 tangent = in_dto.tangent;
    tangent = (tangent - dot(tangent, normal) *  normal);
    vec3 bitangent = cross(in_dto.normal, in_dto.tangent);
    TBN = mat3(tangent, bitangent, normal);

    // Update the normal to use a sample from the normal map.
    vec3 local_normal = 2.0 * texture(material_textures[SAMP_NORMAL], in_dto.tex_coord).rgb - 1.0;
    normal = normalize(TBN * local_normal);

    vec4 albedo_samp = texture(material_textures[SAMP_ALBEDO], in_dto.tex_coord);
    vec3 albedo = pow(albedo_samp.rgb, vec3(2.2));

    float metallic = texture(material_textures[SAMP_METALLIC], in_dto.tex_coord).r;
    float roughness = texture(material_textures[SAMP_ROUGHNESS], in_dto.tex_coord).r;
    float ao = texture(material_textures[SAMP_AO], in_dto.tex_coord).r;

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use base_reflectivity 
    // of 0.04 and if it's a metal, use the albedo color as base_reflectivity (metallic workflow)    
    vec3 base_reflectivity = vec3(0.04); 
    base_reflectivity = mix(base_reflectivity, albedo, metallic);

    if(in_mode == 0 || in_mode == 1 || in_mode == 3) {
        vec3 view_direction = normalize(in_dto.view_position - in_dto.frag_position);

        // Don't include albedo in mode 1 (lighting-only). Do this by using white 
        // multiplied by mode (mode 1 will result in white, mode 0 will be black),
        // then add this colour to albedo and clamp it. This will result in pure 
        // white for the albedo in mode 1, and normal albedo in mode 0, all without
        // branching.
        albedo += (vec3(1.0) * in_mode);         
        albedo = clamp(albedo, vec3(0.0), vec3(1.0));

        // This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).
        // This uses a micro-facet model to use roughness and metallic properties of materials to produce
        // physically accurate representation of material reflectance.

        // Overall reflectance.
        vec3 total_reflectance = vec3(0.0);

        // Directional light radiance.
        {
            directional_light light = instance_ubo.dir_light;
            vec3 light_direction = normalize(-light.direction.xyz);
            vec3 radiance = calculate_directional_light_radiance(light, view_direction);

            total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance);
        }

        // Point light radiance
        for(int i = 0; i < instance_ubo.num_p_lights; ++i) {
            point_light light = instance_ubo.p_lights[i];
            vec3 light_direction = normalize(light.position.xyz - in_dto.frag_position.xyz);
            vec3 radiance = calculate_point_light_radiance(light, view_direction, in_dto.frag_position.xyz);

            total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance);
        }

        // Irradiance holds all the scene's indirect diffuse light. Use the surface normal to sample from it.
        vec3 irradiance = texture(irradiance_texture, normal).rgb;

        // Generate shadow value based on current fragment position vs shadow map.
        // Light and normal are also taken in the case that a bias is to be used.
        // TODO: take point lights into account in shadows.
        vec4 frag_position_view_space = global_ubo.view * vec4(in_dto.frag_position, 1.0f);
        float depth = abs(frag_position_view_space).z;
        // Get the cascade index from the current fragment's position.
        int cascade_index = -1;
        for(int i = 0; i < MAX_SHADOW_CASCADES; ++i) {
            if(depth < in_dto.cascade_splits[i]) {
                cascade_index = i;
                break;
            }
        }
        if(cascade_index == -1) {
            cascade_index = MAX_SHADOW_CASCADES;
        }
        float shadow = calculate_shadow(in_dto.light_space_frag_pos[cascade_index], normal, instance_ubo.dir_light, cascade_index);

        // Combine irradiance with albedo and ambient occlusion. 
        // Also add in total accumulated reflectance.
        vec3 ambient = irradiance * albedo * ao;
        // Modify total reflectance by the generated shadow value.
        vec3 colour = ambient + total_reflectance * shadow;

        // HDR tonemapping
        colour = colour / (colour + vec3(1.0));
        // Gamma correction
        colour = pow(colour, vec3(1.0 / 2.2));

        if(in_mode == 3) {
            switch(cascade_index) {
                case 0:
                    colour *= vec3(1.0, 0.25, 0.25);
                    break;
                case 1:
                    colour *= vec3(0.25, 1.0, 0.25);
                    break;
                case 2:
                    colour *= vec3(0.25, 0.25, 1.0);
                    break;
                case 3:
                    colour *= vec3(1.0, 1.0, 0.25);
                    break;
            }
        }

        // Ensure the alpha is based on the albedo's original alpha  value.
        out_colour = vec4(colour, albedo_samp.a);
    } else if(in_mode == 2) {
        out_colour = vec4(abs(normal), 1.0);
    }
}

vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance) {
    vec3 halfway = normalize(view_direction + light_direction);

    // This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).

    // Normal distribution - approximates the amount of the surface's micro-facets that are aligned
    // to the halfway vector. This is directly influenced by the roughness of the surface. More aligned 
    // micro-facets = shiny, less = dull surface/less reflection.
    float roughness_sq = roughness*roughness;
    float roughness_sq_sq = roughness_sq * roughness_sq;
    float normal_dot_halfway = max(dot(normal, halfway), 0.0);
    float normal_dot_halfway_squared = normal_dot_halfway * normal_dot_halfway;
    float denom = (normal_dot_halfway_squared * (roughness_sq_sq - 1.0) + 1.0);
    denom = PI * denom * denom;
    float normal_distribution = (roughness_sq_sq / denom);

    // Geometry function which calculates self-shadowing on micro-facets (more pronounced on rough surfaces).
    float normal_dot_view_direction = max(dot(normal, view_direction), 0.0);
    // Scale the light by the dot product of normal and light_direction.
    float normal_dot_light_direction = max(dot(normal, light_direction), 0.0);
    float ggx_0 = geometry_schlick_ggx(normal_dot_view_direction, roughness);
    float ggx_1 = geometry_schlick_ggx(normal_dot_light_direction, roughness);
    float geometry = ggx_1 * ggx_0;

    // Fresnel-Schlick approximation for the fresnel. This generates a ratio of surface reflection 
    // at different surface angles. In many cases, reflectivity can be higher at more extreme angles.
    float cos_theta = max(dot(halfway, view_direction), 0.0);
    vec3 fresnel = base_reflectivity + (1.0 - base_reflectivity) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);

    // Take Normal distribution * geometry * fresnel and calculate specular reflection.
    vec3 numerator = normal_distribution * geometry * fresnel;
    float denominator = 4.0 * max(dot(normal, view_direction), 0.0) + 0.0001; // prevent div by 0 
    vec3 specular = numerator / denominator;

    // For energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component should equal 1.0 - fresnel.
    vec3 refraction_diffuse = vec3(1.0) - fresnel;
    // multiply diffuse by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    refraction_diffuse *= 1.0 - metallic;	  

    // The end result is the reflectance to be added to the overall, which is tracked by the caller.
    return (refraction_diffuse * albedo / PI + specular) * radiance * normal_dot_light_direction;  
}

vec3 calculate_point_light_radiance(point_light light, vec3 view_direction, vec3 frag_position_xyz) {
    // Per-light radiance based on the point light's attenuation.
    float distance = length(light.position.xyz - frag_position_xyz);
    float attenuation = 1.0 / (light.constant_f + light.linear * distance + light.quadratic * (distance * distance));
    return light.colour.rgb * attenuation;
}

vec3 calculate_directional_light_radiance(directional_light light, vec3 view_direction) {
    // For directional lights, radiance is just the same as the light colour itself.
    return light.colour.rgb;
}

