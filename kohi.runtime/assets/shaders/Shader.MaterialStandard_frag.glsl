#version 450


// TODO: All these types should be defined in some #include file when #includes are implemented.

const float PI = 3.14159265359;

const uint MATERIAL_MAX_SHADOW_CASCADES = 4;
const uint MATERIAL_MAX_POINT_LIGHTS = 10;
const uint MATERIAL_MAX_VIEWS = 4;
const uint MATERIAL_STANDARD_TEXTURE_COUNT = 7;
const uint MATERIAL_STANDARD_SAMPLER_COUNT = 7;
const uint MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT = 4;

// Material texture indices
const uint MAT_STANDARD_IDX_BASE_COLOUR = 0;
const uint MAT_STANDARD_IDX_NORMAL = 1;
const uint MAT_STANDARD_IDX_METALLIC = 2;
const uint MAT_STANDARD_IDX_ROUGHNESS = 3;
const uint MAT_STANDARD_IDX_AO = 4;
const uint MAT_STANDARD_IDX_MRA = 5;
const uint MAT_STANDARD_IDX_EMISSIVE = 6;

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

const uint KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT = 0x0001;
const uint KMATERIAL_FLAG_DOUBLE_SIDED_BIT = 0x0002;
const uint KMATERIAL_FLAG_RECIEVES_SHADOW_BIT = 0x0004;
const uint KMATERIAL_FLAG_CASTS_SHADOW_BIT = 0x0008;
const uint KMATERIAL_FLAG_NORMAL_ENABLED_BIT = 0x0010;
const uint KMATERIAL_FLAG_AO_ENABLED_BIT = 0x0020;
const uint KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT = 0x0040;
const uint KMATERIAL_FLAG_MRA_ENABLED_BIT = 0x0080;
const uint KMATERIAL_FLAG_REFRACTION_ENABLED_BIT = 0x0100;
const uint KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT = 0x0200;

const uint MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX = 0x0001;
const uint MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX = 0x0002;
const uint MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX = 0x0004;
const uint MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX = 0x0008;
const uint MATERIAL_STANDARD_FLAG_USE_AO_TEX = 0x0010;
const uint MATERIAL_STANDARD_FLAG_USE_MRA_TEX = 0x0020;
const uint MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX = 0x0040;

struct directional_light {
    vec4 colour;
    vec4 direction;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_split_mult;
    float padding;
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

// =========================================================
// Inputs
// =========================================================

// per-frame
layout(std140, set = 0, binding = 0) uniform per_frame_ubo {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 views[MATERIAL_MAX_VIEWS];
    mat4 projection;
    vec4 view_positions[MATERIAL_MAX_VIEWS];
    vec4 cascade_splits;// TODO: support for something other than 4[MATERIAL_MAX_SHADOW_CASCADES];
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
    directional_light dir_light;            // 48 bytes
    point_light p_lights[MATERIAL_MAX_POINT_LIGHTS]; // 48 bytes each
    uint num_p_lights;
    /** @brief The material lighting model. */
    uint lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    uint flags;
    // Texture use flags
    uint tex_flags;

    vec4 base_colour;
    vec4 emissive;
    vec3 normal;
    float metallic;
    vec3 mra;
    float roughness;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    float ao;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;
    float emissive_texture_intensity;

    float refraction_scale;
    // Packed texture channels for various maps requiring it.
    uint texture_channels; // [metallic, roughness, ao, unused]
    vec2 padding;
    vec4 padding2;
    vec4 padding3;
    vec4 padding4;
} material_group_ubo;
layout(set = 1, binding = 1) uniform texture2D material_textures[MATERIAL_STANDARD_TEXTURE_COUNT];
layout(set = 1, binding = 2) uniform sampler material_samplers[MATERIAL_STANDARD_SAMPLER_COUNT];

// per-draw
layout(push_constant) uniform per_draw_ubo {
    mat4 model;
    vec4 clipping_plane;
    uint view_index;
    uint irradiance_cubemap_index;
    vec2 padding;
} material_draw_ubo;

// Data Transfer Object
layout(location = 0) in dto {
	vec4 frag_position;
	vec4 light_space_frag_pos[MATERIAL_MAX_SHADOW_CASCADES];
    vec4 vertex_colour;
	vec3 normal;
    uint metallic_texture_channel;
	vec3 tangent;
    uint roughness_texture_channel;
	vec2 tex_coord;
    uint ao_texture_channel;
    uint unused_texture_channel;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance);
vec3 calculate_point_light_radiance(point_light light, vec3 view_direction, vec3 frag_position_xyz);
vec3 calculate_directional_light_radiance(directional_light light, vec3 view_direction);
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias);
float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias);
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, directional_light light, int cascade_index);
float geometry_schlick_ggx(float normal_dot_direction, float roughness);
void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w);
bool flag_get(uint flags, uint flag);
uint flag_set(uint flags, uint flag, bool enabled);

void main() {
    uint render_mode = material_frame_ubo.options[MAT_OPTION_IDX_RENDER_MODE];
	vec4 view_position = material_frame_ubo.view_positions[material_draw_ubo.view_index];
    vec3 cascade_colour = vec3(1.0);

    vec3 normal = in_dto.normal;
    vec3 tangent = in_dto.tangent;
    tangent = (tangent - dot(tangent, normal) *  normal);
    vec3 bitangent = cross(in_dto.normal, in_dto.tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Base colour
    vec4 base_colour_samp;
    if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT)) {
        // Pass through the vertex colour
        base_colour_samp = in_dto.vertex_colour;
    } else {
        // Use base colour texture if provided; otherwise use the colour.
        if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX)) {
            base_colour_samp = texture(sampler2D(material_textures[MAT_STANDARD_IDX_BASE_COLOUR], material_samplers[MAT_STANDARD_IDX_BASE_COLOUR]), in_dto.tex_coord);
        } else {
            base_colour_samp = material_group_ubo.base_colour;
        }
    }
    vec3 albedo = pow(base_colour_samp.rgb, vec3(2.2));

    // Calculate "local normal".
    // If enabled, get the normal from the normal map if used, or the supplied vector if not.
    // Otherwise, just use a default z-up
    vec3 local_normal;
    if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT)){
        if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX)) {
            local_normal = texture(sampler2D(material_textures[MAT_STANDARD_IDX_NORMAL], material_samplers[MAT_STANDARD_IDX_NORMAL]), in_dto.tex_coord).rgb;
        } else {
            local_normal = material_group_ubo.normal;
        }
    } else {
        local_normal = vec3(0, 1.0, 0);
    }
    // Update the normal to use a sample from the normal map.
    normal = normalize(TBN * (2.0 * local_normal - 1.0));

    // Either use combined MRA (metallic/roughness/ao) or individual maps, depending on settings.
    vec3 mra;
    if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_MRA_ENABLED_BIT)) { 
        if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_MRA_TEX)) {
            mra = texture(sampler2D(material_textures[MAT_STANDARD_IDX_MRA], material_samplers[MAT_STANDARD_IDX_MRA]), in_dto.tex_coord).rgb;
        } else {
            mra = material_group_ubo.mra;
        }
    } else {
        // Sample individual maps.

        // Metallic 
        if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX)) {
            vec4 sampled = texture(sampler2D(material_textures[MAT_STANDARD_IDX_METALLIC], material_samplers[MAT_STANDARD_IDX_METALLIC]), in_dto.tex_coord);
            // Load metallic into the red channel from the configured source texture channel.
            mra.r = sampled[in_dto.metallic_texture_channel];
        } else {
            mra.r = material_group_ubo.metallic;
        }

        // Roughness 
        if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX)) {
            vec4 sampled = texture(sampler2D(material_textures[MAT_STANDARD_IDX_ROUGHNESS], material_samplers[MAT_STANDARD_IDX_ROUGHNESS]), in_dto.tex_coord);
            // Load roughness into the green channel from the configured source texture channel.
            mra.g = sampled[in_dto.roughness_texture_channel];
        } else {
            mra.g = material_group_ubo.roughness;
        }

        // AO - default to 1.0 (i.e. no effect), and only read in a value if this is enabled.
        mra.b = 1.0;
        if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_AO_ENABLED_BIT)) { 
            if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_AO_TEX)) {
                vec4 sampled = texture(sampler2D(material_textures[MAT_STANDARD_IDX_AO], material_samplers[MAT_STANDARD_IDX_AO]), in_dto.tex_coord);
                // Load AO into the blue channel from the configured source texture channel.
                mra.b = sampled[in_dto.ao_texture_channel];
            } else {
                mra.b = material_group_ubo.ao;
            }
        }
    }

    // Emissive - defaults to 0 if not used.
    vec3 emissive = vec3(0.0);
    if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT)) { 
        if(flag_get(material_group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX)) {
            emissive = texture(sampler2D(material_textures[MAT_STANDARD_IDX_EMISSIVE], material_samplers[MAT_STANDARD_IDX_EMISSIVE]), in_dto.tex_coord).rgb;
        } else {
            emissive = material_group_ubo.emissive.rgb;
        }
    }

    float metallic = mra.r;
    float roughness = mra.g;
    float ao = mra.b;

    // Shadows: 1.0 means NOT in shadow, which is the default.
    float shadow = 1.0;
    // Only perform shadow calculations if this receives shadows.
    if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT)) { 

        // Generate shadow value based on current fragment position vs shadow map.
        // Light and normal are also taken in the case that a bias is to be used.
        vec4 frag_position_view_space = material_frame_ubo.views[material_draw_ubo.view_index] * in_dto.frag_position;
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

        if(render_mode == 3) {
            switch(cascade_index) {
                case 0:
                    cascade_colour = vec3(1.0, 0.25, 0.25);
                    break;
                case 1:
                    cascade_colour = vec3(0.25, 1.0, 0.25);
                    break;
                case 2:
                    cascade_colour = vec3(0.25, 0.25, 1.0);
                    break;
                case 3:
                    cascade_colour = vec3(1.0, 1.0, 0.25);
                    break;
            }
        }
        shadow = calculate_shadow(in_dto.light_space_frag_pos[cascade_index], normal, material_group_ubo.dir_light, cascade_index);

        // Fade out the shadow map past a certain distance.
        float fade_start = material_group_ubo.dir_light.shadow_distance;
        float fade_distance = material_group_ubo.dir_light.shadow_fade_distance;

        // The end of the fade-out range.
        float fade_end = fade_start + fade_distance;

        float zclamp = clamp(length(view_position.xyz - in_dto.frag_position.xyz), fade_start, fade_end);
        float fade_factor = (fade_end - zclamp) / (fade_end - fade_start + 0.00001); // Avoid divide by 0

        shadow = clamp(shadow + (1.0 - fade_factor), 0.0, 1.0);
    } 

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use base_reflectivity 
    // of 0.04 and if it's a metal, use the albedo color as base_reflectivity (metallic workflow)    
    vec3 base_reflectivity = vec3(0.04); 
    base_reflectivity = mix(base_reflectivity, albedo, metallic);

    if(render_mode == 0 || render_mode == 1 || render_mode == 3) {
        vec3 view_direction = normalize(view_position.xyz - in_dto.frag_position.xyz);

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
            directional_light light = material_group_ubo.dir_light;
            vec3 light_direction = normalize(-light.direction.xyz);
            vec3 radiance = calculate_directional_light_radiance(light, view_direction);

            // Only directional light should be affected by shadow map.
            total_reflectance += (shadow * calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance));
        }

        // Point light radiance
        for(int i = 0; i < material_group_ubo.num_p_lights; ++i) {
            point_light light = material_group_ubo.p_lights[i];
            vec3 light_direction = normalize(light.position.xyz - in_dto.frag_position.xyz);
            vec3 radiance = calculate_point_light_radiance(light, view_direction, in_dto.frag_position.xyz);

            total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance);
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

        // Apply cascade_colour if relevant.
        colour *= cascade_colour;

        // Apply emissive at the end.
        colour.rgb += emissive;

        // Ensure the alpha is based on the albedo's original alpha value if transparency is enabled.
        // If it's not enabled, just use 1.0.
        float alpha = 1.0;
        if(flag_get(material_group_ubo.flags, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT)) {
            alpha = base_colour_samp.a;
        }
        out_colour = vec4(colour, alpha);
    } else if(render_mode == 2) {
        out_colour = vec4(abs(normal), 1.0);
    } else if(render_mode == 4) {
        // wireframe, just render a solid colour.
        out_colour = vec4(0.0, 1.0, 1.0, 1.0); // cyan
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

/**
 * @brief Indicates if the provided flag is set in the given flags int.
 */
bool flag_get(uint flags, uint flag) {
    return (flags & flag) == flag;
}

/**
 * @brief Sets a flag within the flags int to enabled/disabled.
 *
 * @param flags The flags int to write to.
 * @param flag The flag to set.
 * @param enabled Indicates if the flag is enabled or not.
 */
uint flag_set(uint flags, uint flag, bool enabled) {
    return enabled ? (flags | flag) : (flags & ~flag);
}
