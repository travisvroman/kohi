#version 450

layout(location = 0) out vec4 out_colour;

// TODO: All these types should be defined in some #include file when #includes are implemented.

const float PI = 3.14159265359;

const uint KMATERIAL_MAX_GLOBAL_POINT_LIGHTS = 64;
const uint MATERIAL_MAX_SHADOW_CASCADES = 4;
const uint MATERIAL_MAX_VIEWS = 4;
const uint MATERIAL_WATER_TEXTURE_COUNT = 5;
const uint MATERIAL_WATER_SAMPLER_COUNT = 5;
const uint MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT = 4;

const uint MAT_WATER_IDX_REFLECTION = 0;
const uint MAT_WATER_IDX_REFRACTION = 1;
const uint MAT_WATER_IDX_REFRACTION_DEPTH = 2;
const uint MAT_WATER_IDX_DUDV = 3;
const uint MAT_WATER_IDX_NORMAL = 4;

// Option indices
const uint MAT_OPTION_IDX_RENDER_MODE = 0;
const uint MAT_OPTION_IDX_USE_PCF = 1;
const uint MAT_OPTION_IDX_UNUSED_0 = 2;
const uint MAT_OPTION_IDX_UNUSED_1 = 3;

// Param indices
const uint MAT_PARAM_IDX_SHADOW_BIAS = 0;
const uint MAT_PARAM_IDX_DELTA_TIME = 1;
const uint MAT_PARAM_IDX_GAME_TIME = 2;
const uint MAT_PARAM_IDX_UNUSED_0 = 3;

struct directional_light {
    vec4 colour;
    vec4 direction;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_split_mult;
    float padding;
};

struct point_light {
    // .rgb = colour, .a = linear 
    vec4 colour;
    // .xyz = position, .w = quadratic
    vec4 position;
};

// =========================================================
// Inputs
// =========================================================

// per-frame, "global" data
layout(std140, set = 0, binding = 0) uniform kmaterial_global_uniform_data {
    point_light p_lights[KMATERIAL_MAX_GLOBAL_POINT_LIGHTS]; // 2048 bytes @ 32 bytes each
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 projection;                                             // 64 bytes
    mat4 view;                                                   // 64 bytes
    directional_light dir_light;                                 // 48 bytes
    vec4 view_position;                                          // 16 bytes
    vec4 cascade_splits;                                         // 16 bytes
    // [shadow_bias, delta_time, game_time, padding]
    vec4 params;
    // [render_mode, use_pcf, padding, padding]
    uvec4 options;
    vec4 padding;  // 16 bytes
} material_frame_ubo;
layout(set = 0, binding = 1) uniform texture2DArray shadow_texture;
layout(set = 0, binding = 2) uniform textureCube irradiance_textures[MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];
layout(set = 0, binding = 3) uniform sampler shadow_sampler;
layout(set = 0, binding = 4) uniform sampler irradiance_sampler;

// per-group
layout(set = 1, binding = 0) uniform per_group_ubo {
    /** @brief The material lighting model. */
    uint lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    uint flags;
    vec2 padding;
} material_group_ubo;

layout(set = 1, binding = 1) uniform texture2D material_textures[MATERIAL_WATER_TEXTURE_COUNT];
layout(set = 1, binding = 2) uniform sampler material_samplers[MATERIAL_WATER_SAMPLER_COUNT];

// per-draw
layout(push_constant) uniform per_draw_ubo {
    mat4 model;
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 u32s.
    uvec2 packed_point_light_indices; // 8 bytes
    uint num_p_lights;
    uint irradiance_cubemap_index;
    float tiling;
    float wave_strength;
    float wave_speed;
    float padding;
} material_draw_ubo;

// Data Transfer Object from vertex shader.
layout(location = 0) in dto {
    vec4 frag_position;
    vec4 light_space_frag_pos[MATERIAL_MAX_SHADOW_CASCADES];
	vec4 clip_space;
    vec3 world_to_camera;
    float padding;
	vec2 texcoord;
    vec2 padding2;
} in_dto;


vec4 do_lighting(mat4 view, vec3 frag_position, vec3 albedo, vec3 normal, uint render_mode);
vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance);
vec3 calculate_point_light_radiance(point_light light, vec3 view_direction, vec3 frag_position_xyz);
vec3 calculate_directional_light_radiance(directional_light light, vec3 view_direction);
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias);
float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias);
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, directional_light light, int cascade_index);
float geometry_schlick_ggx(float normal_dot_direction, float roughness);
void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w);

// Entry point
void main() {
    uint render_mode = material_frame_ubo.options[MAT_OPTION_IDX_RENDER_MODE];
    float game_time = material_frame_ubo.params[MAT_PARAM_IDX_GAME_TIME];
    float move_factor = material_draw_ubo.wave_speed * game_time;
    // Calculate surface distortion and bring it into [-1.0 - 1.0] range
    vec2 distorted_texcoords = texture(sampler2D(material_textures[MAT_WATER_IDX_DUDV], material_samplers[MAT_WATER_IDX_DUDV]), vec2(in_dto.texcoord.x + move_factor, in_dto.texcoord.y)).rg * 0.1;
    distorted_texcoords = in_dto.texcoord + vec2(distorted_texcoords.x, distorted_texcoords.y + move_factor);

    // Compute the surface normal using the normal map.
    vec4 normal_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_NORMAL], material_samplers[MAT_WATER_IDX_NORMAL]), distorted_texcoords);
    // Extract the normal, shifting to a range of [-1 - 1]
    vec3 normal = vec3(normal_colour.r * 2.0 - 1.0, normal_colour.g * 2.5, normal_colour.b * 2.0 - 1.0);
    
    vec3 original_tangent = normalize(vec3(1, 0, -0));
    vec3 tangent = original_tangent; // TODO: take from actual plane
    tangent = (tangent - dot(tangent, normal) *  normal);
    vec3 bitangent = cross(vec3(0,1,0), original_tangent);// TODO: take from actual plane
    mat3 TBN = mat3(tangent, bitangent, normal);

    normal = normalize(TBN*normal);

    float water_depth = 0;

    if(render_mode == 0) {
        // Perspective division to NDC for texture projection, then to screen space.
        vec2 ndc = (in_dto.clip_space.xy / in_dto.clip_space.w) / 2.0 + 0.5;
        vec2 reflect_texcoord = vec2(ndc.x, ndc.y);
        vec2 refract_texcoord = vec2(ndc.x, -ndc.y);

        // TODO: Should come as uniforms from the viewport's near/far.
        float near = 0.1;
        float far = 1000.0;
        float depth = texture(sampler2D(material_textures[MAT_WATER_IDX_REFRACTION_DEPTH], material_samplers[MAT_WATER_IDX_REFRACTION_DEPTH]), refract_texcoord).r;
        // Convert depth to linear distance.
        float floor_distance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));
        depth = gl_FragCoord.z;
        float water_distance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));
        water_depth = floor_distance - water_distance;

        // Distort further
        vec2 distortion_total = (texture(sampler2D(material_textures[MAT_WATER_IDX_DUDV], material_samplers[MAT_WATER_IDX_DUDV]), distorted_texcoords).rg * 2.0 - 1.0) * material_draw_ubo.wave_strength;

        reflect_texcoord += distortion_total;
        // Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
        reflect_texcoord = clamp(reflect_texcoord, 0.001, 0.999);

        refract_texcoord += distortion_total;
        // Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
        refract_texcoord.x = clamp(refract_texcoord.x, 0.001, 0.999);
        refract_texcoord.y = clamp(refract_texcoord.y, -0.999, -0.001); // Account for flipped y-axis

        vec4 reflect_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_REFLECTION], material_samplers[MAT_WATER_IDX_REFLECTION]), reflect_texcoord);
        vec4 refract_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_REFRACTION], material_samplers[MAT_WATER_IDX_REFRACTION]), refract_texcoord);
        // Refract should be slightly darker since it's wet.
        refract_colour.rgb = clamp(refract_colour.rgb - vec3(0.2), vec3(0.0), vec3(1.0));

        // Calculate the fresnel effect.
        float fresnel_factor = dot(normalize(in_dto.world_to_camera), normal);
        fresnel_factor = clamp(fresnel_factor, 0.0, 1.0);

        out_colour = mix(reflect_colour, refract_colour, fresnel_factor);
        vec4 tint = vec4(0.0, 0.3, 0.5, 1.0); // TODO: configurable.
        float tint_strength = 0.5; // TODO: configurable.
        out_colour = mix(out_colour, tint, tint_strength);
    } else {
        // Other modes should just use white.
        out_colour = vec4(1.0);
    }

    // Apply lighting
    vec4 lighting = do_lighting(material_frame_ubo.view, in_dto.frag_position.xyz, out_colour.rgb, normal, render_mode);
    out_colour = lighting;

    if(render_mode == 0) {
        // Falloff depth of the water at the edge.
        float edge_depth_falloff = 0.5; // TODO: configurable
        out_colour.a = clamp(water_depth / edge_depth_falloff, 0.0, 1.0);
    }
}

vec4 do_lighting(mat4 view, vec3 frag_position, vec3 albedo, vec3 normal, uint render_mode) {
    vec4 light_colour;

    // These can be hardcoded for water surfaces.
    float metallic = 0.9;
    float roughness = 1.0 - metallic;
    float ao = 1.0;

    // Generate shadow value based on current fragment position vs shadow map.
    // Light and normal are also taken in the case that a bias is to be used.
    vec4 frag_position_view_space = view * vec4(frag_position, 1.0);
    float depth = abs(frag_position_view_space).z;
    // Get the cascade index from the current fragment's position.
    int cascade_index = -1;
    for(int i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
        if(depth < material_frame_ubo.cascade_splits[i]) {
            cascade_index = i;
            break;
        }
    }
    if(cascade_index == -1) {
        cascade_index = int(MATERIAL_MAX_SHADOW_CASCADES);
    }
    float shadow = calculate_shadow(in_dto.light_space_frag_pos[cascade_index], normal, material_frame_ubo.dir_light, cascade_index);

    // Fade out the shadow map past a certain distance.
    float fade_start = material_frame_ubo.dir_light.shadow_distance;
    float fade_distance = material_frame_ubo.dir_light.shadow_fade_distance;

    // The end of the fade-out range.
    float fade_end = fade_start + fade_distance;

    float zclamp = clamp(length(material_frame_ubo.view_position.xyz - frag_position), fade_start, fade_end);
    float fade_factor = (fade_end - zclamp) / (fade_end - fade_start + 0.00001); // Avoid divide by 0

    shadow = clamp(shadow + (1.0 - fade_factor), 0.0, 1.0);

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use base_reflectivity 
    // of 0.04 and if it's a metal, use the albedo color as base_reflectivity (metallic workflow)    
    vec3 base_reflectivity = vec3(0.04); 
    base_reflectivity = mix(base_reflectivity, albedo, metallic);

    if(render_mode == 0 || render_mode == 1 || render_mode == 3) {
        vec3 view_direction = normalize(material_frame_ubo.view_position.xyz - frag_position);

        // Don't include albedo in mode 1 (lighting-only). Do this by using white 
        // multiplied by mode (mode 1 will result in white, mode 0 will be black),
        // then add this colour to albedo and clamp it. This will result in pure 
        // white for the albedo in mode 1, and normal albedo in mode 0, all without
        // branching.
        albedo += (vec3(1.0) * render_mode);         
        albedo = clamp(albedo, vec3(0.0), vec3(1.0));

        // This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).
        // This uses a micro-facet model to use roughness and metallic properties of materials to produce
        // physically accurate representation of material reflectance.

        // Overall reflectance.
        vec3 total_reflectance = vec3(0.0);

        // Directional light radiance.
        {
            directional_light light = material_frame_ubo.dir_light;
            vec3 light_direction = normalize(-light.direction.xyz);
            vec3 radiance = calculate_directional_light_radiance(light, view_direction);

            // Only directional light should be affected by shadow map.
            total_reflectance += (shadow * calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance));
        }

        // Point light radiance
        // Get point light indices by unpacking each element of material_draw_ubo.packed_point_light_indices
        uint plights_rendered = 0;
        for(uint ppli = 0; ppli < 2 && plights_rendered < material_draw_ubo.num_p_lights; ++ppli) {
            uint packed = material_draw_ubo.packed_point_light_indices[ppli];
            uint unpacked[4];
            unpack_u32(packed, unpacked[0], unpacked[1], unpacked[2], unpacked[3]);
            for(uint upi = 0; upi < 4 && plights_rendered < material_draw_ubo.num_p_lights; ++upi) {
                point_light light = material_frame_ubo.p_lights[unpacked[upi]];
                vec3 light_direction = normalize(light.position.xyz - in_dto.frag_position.xyz);
                vec3 radiance = calculate_point_light_radiance(light, view_direction, in_dto.frag_position.xyz);

                total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance);
            }
        }

        // Irradiance holds all the scene's indirect diffuse light. Use the surface normal to sample from it.
        vec3 irradiance = texture(samplerCube(irradiance_textures[material_draw_ubo.irradiance_cubemap_index], irradiance_sampler), normal).rgb;

        // Combine irradiance with albedo and ambient occlusion. 
        // Also add in total accumulated reflectance.
        vec3 ambient = irradiance * albedo * ao;
        // Modify total reflectance by the ambient colour.
        vec3 colour = ambient + total_reflectance;

        // HDR tonemapping
        colour = colour / (colour + vec3(1.0));
        // Gamma correction
        colour = pow(colour, vec3(1.0 / 2.2));

        if(render_mode == 3) {
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

        // Don't add alpha, that will be taken from the water itself.
        light_colour = vec4(colour, 1.0);
    } else if(render_mode == 2) {
        light_colour = vec4(abs(normal), 1.0);
    } else if(render_mode == 4) {
        // wireframe, just render a solid colour.
        light_colour = vec4(0.0, 0.0, 1.0, 1.0); // blue
    }

    return light_colour;
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
    // constant is 1.0f, first one in the () below, linear is stored in colour.a, quadratic is stored in position.w
    float attenuation = 1.0 / (1.0 + light.colour.a * distance + light.position.w * (distance * distance));
    // PBR lights are energy-based, so convert to a scale of 0-100.
    float energy_multiplier = 100.0;
    return (light.colour.rgb * energy_multiplier) * attenuation;
}

vec3 calculate_directional_light_radiance(directional_light light, vec3 view_direction) {
    // For directional lights, radiance is just the same as the light colour itself.
    // PBR lights are energy-based, so convert to a scale of 0-100.
    float energy_multiplier = 100.0;
    return light.colour.rgb * energy_multiplier;
}

// Percentage-Closer Filtering
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias) {
    float shadow = 0.0;
    vec2 texel_size = 1.0 / textureSize(sampler2DArray(shadow_texture, shadow_sampler), 0).xy;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcf_depth = texture(sampler2DArray(shadow_texture, shadow_sampler), vec3(projected.xy + vec2(x, y) * texel_size, cascade_index)).r;
            shadow += projected.z - shadow_bias > pcf_depth ? 1.0 : 0.0;
        }
    }
    shadow /= 9;
    return 1.0 - shadow;
}

float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias) {
    // Sample the shadow map.
    float map_depth = texture(sampler2DArray(shadow_texture, shadow_sampler), vec3(projected.xy, cascade_index)).r;

    // TODO: cast/get rid of branch.
    float shadow = projected.z - shadow_bias > map_depth ? 0.0 : 1.0;
    return shadow;
}

// Compare the fragment position against the depth buffer, and if it is further 
// back than the shadow map, it's in shadow. 0.0 = in shadow, 1.0 = not
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, directional_light light, int cascade_index) {
    // Perspective divide - note that while this is pointless for ortho projection,
    // perspective will require this.
    vec3 projected = light_space_frag_pos.xyz / light_space_frag_pos.w;
    // Need to reverse y
    projected.y = 1.0 - projected.y;

    // NOTE: Transform to NDC not needed for Vulkan, but would be for OpenGL.
    // projected.xy = projected.xy * 0.5 + 0.5;

    uint use_pcf = material_frame_ubo.options[MAT_OPTION_IDX_USE_PCF];
    float shadow_bias = material_frame_ubo.params[MAT_PARAM_IDX_SHADOW_BIAS];
    if(use_pcf == 1) {
        return calculate_pcf(projected, cascade_index, shadow_bias);
    } 

    return calculate_unfiltered(projected, cascade_index, shadow_bias);
}

// Based on a combination of GGX and Schlick-Beckmann approximation to calculate probability
// of overshadowing micro-facets.
float geometry_schlick_ggx(float normal_dot_direction, float roughness) {
    roughness += 1.0;
    float k = (roughness * roughness) / 8.0;
    return normal_dot_direction / (normal_dot_direction * (1.0 - k) + k);
}

void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w) {
    x = (n >> 24) & 0xFF;
    y = (n >> 16) & 0xFF;
    z = (n >> 8) & 0xFF;
    w = n & 0xFF;
}
