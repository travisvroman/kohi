#version 450

const float PI = 3.14159265359;

const uint KMATERIAL_UBO_MAX_VIEWS = 16;
const uint HF_TERRAIN_MAX_MATERIAL_COUNT = 5;
const uint MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT = 4;
const uint KMATERIAL_UBO_MAX_SHADOW_CASCADES = 4;

struct light_data {
    // Directional light: .rgb = colour, .w = ignored - Point lights: .rgb = colour, .a = linear 
    vec4 colour;
    // Directional Light: .xyz = direction, .w = ignored - Point lights: .xyz = position, .w = quadratic
    vec4 position;
};

// =========================================================
// Inputs
// =========================================================

// Binding Set 0 - global-level stuff

// Global settings for the scene.
layout(std140, set = 0, binding = 0) uniform terrain_settings_ubo {
    mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes
    vec4 cascade_splits;                                         // 16 bytes

    float delta_time;
    float game_time;
    uint render_mode;
    uint use_pcf;

    // Shadow settings
    float shadow_bias;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_split_mult;

    vec4 fog_colour;
    float fog_start;
    float fog_end;
    float near_clip;

    float far_clip;
	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
	uint padding;
} global_settings;

// All matrices
layout(std430, set = 0, binding = 1) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

// All lighting
layout(std430, set = 0, binding = 2) readonly buffer global_lighting_ssbo {
    light_data lights[]; // indexed by immediate.packed_point_light_indices (needs unpacking to 16x u8s)
} global_lighting;

layout(set = 0, binding = 3) uniform texture2DArray shadow_texture;
layout(set = 0, binding = 4) uniform sampler shadow_sampler;
layout(set = 0, binding = 5) uniform textureCube irradiance_textures[MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];
layout(set = 0, binding = 6) uniform sampler irradiance_sampler;
layout(set = 0, binding = 7) uniform texture2DArray albedo_textures;
layout(set = 0, binding = 8) uniform sampler albedo_sampler;
layout(set = 0, binding = 9) uniform texture2DArray normal_textures;
layout(set = 0, binding = 10) uniform sampler normal_sampler;
layout(set = 0, binding = 11) uniform texture2DArray mra_textures;
layout(set = 0, binding = 12) uniform sampler mra_sampler;

// Set 1, per-block
layout(set = 1, binding = 0) uniform texture2D splatmap_texture;
layout(set = 1, binding = 1) uniform sampler splatmap_sampler;

// Immediate data
layout(push_constant) uniform immediate_data {
    // bytes 0-15
    uint view_index;
    uint projection_index;
    uint dir_light_index;
    uint base_material_index;

    // bytes 16-31
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
    uvec2 packed_point_light_indices; // 8 bytes
    uint num_p_lights;
    // Index into global irradiance cubemap texture array
    uint irradiance_cubemap_index;

    // bytes 32-47
	vec4 clipping_plane;

	// bytes 48-63
	uvec4 material_indices;
} immediate;

// Data Transfer Object
layout(location = 0) in dto {
	vec4 light_space_frag_pos[KMATERIAL_UBO_MAX_SHADOW_CASCADES];
	vec4 frag_position;
	vec4 tangent;
	vec3 normal;
    float view_depth;
    vec3 world_to_camera;
	float padding2;
	vec2 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance);
vec3 calculate_point_light_radiance(light_data point_light, vec3 view_direction, vec3 frag_position_xyz);
vec3 calculate_directional_light_radiance(vec3 colour, vec3 view_direction);
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias);
float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias);
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, int cascade_index);
float geometry_schlick_ggx(float normal_dot_direction, float roughness);
void unpack_u32_u8s(uint n, out uint x, out uint y, out uint z, out uint w);
void unpack_u32_u16s(uint n, out uint x, out uint y);

void main() {
	mat4 view = global_matrices.matrices[global_settings.mat_ssbo_view_offset + immediate.view_index];
	mat4 inv_view = inverse(view);
	vec3 view_position = vec3(inv_view[3]);

    vec3 normal= in_dto.normal;
    vec3 tangent = in_dto.tangent.xyz;
    vec2 tex_coord = in_dto.tex_coord;
    vec4 normal_colour = vec4(0, 1, 0, 1);

    tangent = (tangent - dot(tangent, normal) *  normal);
    vec3 bitangent = normalize(cross(normal, tangent.xyz) * in_dto.tangent.w);
    tangent = normalize(cross(bitangent, normal));
    mat3 TBN = mat3(tangent, bitangent, normal);

	// Determine what materials should be used/blended.
	vec2 splatmap_texcoord = vec2((in_dto.tex_coord.x) / (128.0), (in_dto.tex_coord.y) / (128.0));
	vec4 splat_tex = texture(sampler2D(splatmap_texture, splatmap_sampler), splatmap_texcoord);

    // Calculate "local normal".
    // If enabled, get the normal from the normal map if used, or the supplied vector if not.
    // Otherwise, just use a default z-up
    vec3 local_normal = vec3(0, 0, 1.0);

	// Normals
	vec3 normal_0 = texture(sampler2DArray(normal_textures, normal_sampler), vec3(in_dto.tex_coord, immediate.base_material_index)).rgb * 2.0 - 1.0;
	vec3 normal_1 = texture(sampler2DArray(normal_textures, normal_sampler), vec3(in_dto.tex_coord, immediate.material_indices[0])).rgb * 2.0 - 1.0;
	vec3 normal_2 = texture(sampler2DArray(normal_textures, normal_sampler), vec3(in_dto.tex_coord, immediate.material_indices[1])).rgb * 2.0 - 1.0;
	vec3 normal_3 = texture(sampler2DArray(normal_textures, normal_sampler), vec3(in_dto.tex_coord, immediate.material_indices[2])).rgb * 2.0 - 1.0;
	vec3 normal_4 = texture(sampler2DArray(normal_textures, normal_sampler), vec3(in_dto.tex_coord, immediate.material_indices[3])).rgb * 2.0 - 1.0;

    // Weights
	float w1 = splat_tex.r;
	float w2 = splat_tex.g;
	float w3 = splat_tex.b;
	float w4 = splat_tex.a;
    float w0 = 1.0 - (w1 + w2 + w3 + w4);
	w0 = max(w0, 0.0);

	vec3 normal_samp = 
		normal_0 * w0 +
		normal_1 * w1 +
		normal_2 * w2 +
		normal_3 * w3 +
		normal_4 * w4;
		normal_samp = normalize(normal_samp);

    local_normal = normal_samp;

    // Update the normal to use a sample from the normal map.
    normal = normalize(TBN * local_normal);

    vec3 cascade_colour = vec3(1.0);

    light_data directional_light = global_lighting.lights[immediate.dir_light_index];

    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    // Emissive - defaults to 0 if not used.
    vec3 emissive = vec3(0.0);
    // Alpha defaults to 1.0 if not used.
    float alpha = 1.0;

	// Base colour
	vec4 tex_0 = texture(sampler2DArray(albedo_textures, albedo_sampler), vec3(in_dto.tex_coord, immediate.base_material_index));
	vec4 tex_1 = texture(sampler2DArray(albedo_textures, albedo_sampler), vec3(in_dto.tex_coord, immediate.material_indices[0]));
	vec4 tex_2 = texture(sampler2DArray(albedo_textures, albedo_sampler), vec3(in_dto.tex_coord, immediate.material_indices[1]));
	vec4 tex_3 = texture(sampler2DArray(albedo_textures, albedo_sampler), vec3(in_dto.tex_coord, immediate.material_indices[2]));
	vec4 tex_4 = texture(sampler2DArray(albedo_textures, albedo_sampler), vec3(in_dto.tex_coord, immediate.material_indices[3]));

	vec4 base_colour_samp = 
		tex_0 * w0 +
		tex_1 * w1 +
		tex_2 * w2 +
		tex_3 * w3 +
		tex_4 * w4;

	base_colour_samp.a = 1.0;
	albedo = pow(base_colour_samp.rgb, vec3(2.2));

	// MRA
	vec4 mra_0 = texture(sampler2DArray(mra_textures, mra_sampler), vec3(in_dto.tex_coord, immediate.base_material_index));
	vec4 mra_1 = texture(sampler2DArray(mra_textures, mra_sampler), vec3(in_dto.tex_coord, immediate.material_indices[0]));
	vec4 mra_2 = texture(sampler2DArray(mra_textures, mra_sampler), vec3(in_dto.tex_coord, immediate.material_indices[1]));
	vec4 mra_3 = texture(sampler2DArray(mra_textures, mra_sampler), vec3(in_dto.tex_coord, immediate.material_indices[2]));
	vec4 mra_4 = texture(sampler2DArray(mra_textures, mra_sampler), vec3(in_dto.tex_coord, immediate.material_indices[3]));

	vec4 mra_samp = 
		mra_0 * w0 +
		mra_1 * w1 +
		mra_2 * w2 +
		mra_3 * w3 +
		mra_4 * w4;

	metallic = mra_samp.r;
	roughness = mra_samp.g;
	ao = mra_samp.b;

    // Shadows: 1.0 means NOT in shadow, which is the default.
    float shadow = 1.0;

	// Generate shadow value based on current fragment position vs shadow map.
	// Light and normal are also taken in the case that a bias is to be used.
	vec4 frag_position_view_space = view * in_dto.frag_position;
	float depth = abs(frag_position_view_space).z;
	// Get the cascade index from the current fragment's position.
	int cascade_index = -1;
	for(int i = 0; i < KMATERIAL_UBO_MAX_SHADOW_CASCADES; ++i) {
		if(depth < global_settings.cascade_splits[i]) {
			cascade_index = i;
			break;
		}
	}

	if(global_settings.render_mode == 3) {
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
    if(cascade_index != -1) {
	    shadow = calculate_shadow(in_dto.light_space_frag_pos[cascade_index], normal, cascade_index);
    
        // Fade out the shadow map past a certain distance.
        float fade_start = global_settings.shadow_distance;
        float fade_distance = global_settings.shadow_fade_distance;

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

    if(global_settings.render_mode == 0 || global_settings.render_mode == 1 || global_settings.render_mode == 3) {
        vec3 view_direction = normalize(view_position.xyz - in_dto.frag_position.xyz);

        // Don't include albedo in mode 1 (lighting-only). Do this by using white 
        // multiplied by mode (mode 1 will result in white, mode 0 will be black),
        // then add this colour to albedo and clamp it. This will result in pure 
        // white for the albedo in mode 1, and normal albedo in mode 0, all without
        // branching.
        albedo += (vec3(1.0) * global_settings.render_mode);         
        albedo = clamp(albedo, vec3(0.0), vec3(1.0));

        // This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).
        // This uses a micro-facet model to use roughness and metallic properties of materials to produce
        // physically accurate representation of material reflectance.

        // Overall reflectance.
        vec3 total_reflectance = vec3(0.0);

        // Directional light radiance.
        {
            vec3 light_direction = normalize(-directional_light.position.xyz); // position = direction for directional light
            vec3 radiance = calculate_directional_light_radiance(directional_light.colour.rgb, view_direction);

            // Only directional light should be affected by shadow map.
            total_reflectance += (shadow * calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance));
        }

        // Point light radiance
        // Get point light indices by unpacking each element of immediate.packed_point_light_indices
        uint plights_rendered = 0;
        for(uint ppli = 0; ppli < 2 && plights_rendered < immediate.num_p_lights; ++ppli) {
            uint packed = immediate.packed_point_light_indices[ppli];
            uint unpacked[4];
            unpack_u32_u8s(packed, unpacked[0], unpacked[1], unpacked[2], unpacked[3]);
            for(uint upi = 0; upi < 4 && plights_rendered < immediate.num_p_lights; ++upi) {
                light_data light = global_lighting.lights[unpacked[upi]];
                vec3 light_direction = normalize(light.position.xyz - in_dto.frag_position.xyz);
                vec3 radiance = calculate_point_light_radiance(light, view_direction, in_dto.frag_position.xyz);

                total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance);
                plights_rendered++;
            }
        }

        // Irradiance holds all the scene's indirect diffuse light. Use the surface normal to sample from it.
        vec3 irradiance = texture(samplerCube(irradiance_textures[immediate.irradiance_cubemap_index], irradiance_sampler), normal).rgb;

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
        colour.rgb += (emissive * 1.0); // adjust for intensity

        // Apply fog, but only in "regular" mode
        if(global_settings.render_mode == 0) {
            float f = clamp((in_dto.view_depth - global_settings.fog_start) / (global_settings.fog_end - global_settings.fog_start), 0.0, 1.0);
            colour = mix(colour.rgb, global_settings.fog_colour.rgb, global_settings.fog_colour.a * f);
        }

        out_colour = vec4(colour, alpha);
    } else if(global_settings.render_mode == 2) {
        out_colour = vec4(abs(normal), 1.0);
    } else if(global_settings.render_mode == 4) {
        // wireframe, just render a solid colour.
		out_colour = vec4(0.5, 1.0, 0.5, 1.0); 
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
    specular *= 0.2;

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

vec3 calculate_point_light_radiance(light_data light, vec3 view_direction, vec3 frag_position_xyz) {
    float constant_f = 1.0f;
    // Per-light radiance based on the point light's attenuation.
    float distance = length(light.position.xyz - frag_position_xyz);
    // NOTE: linear = colour.a, quadratic = position.w
    float attenuation = 1.0 / (constant_f + light.colour.a * distance + light.position.w * (distance * distance));
    // PBR lights are energy-based, so convert to a scale of 0-100.
    float energy_multiplier = 30.0;
    return (light.colour.rgb * energy_multiplier) * attenuation;
}

vec3 calculate_directional_light_radiance(vec3 colour, vec3 view_direction) {
    // For directional lights, radiance is just the same as the light colour itself.
    // PBR lights are energy-based, so convert to a scale of 0-100.
    float energy_multiplier = 30;// 100.0;
    return colour * energy_multiplier;
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
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, int cascade_index) {
    // Perspective divide - note that while this is pointless for ortho projection,
    // perspective will require this.
    vec3 projected = light_space_frag_pos.xyz / light_space_frag_pos.w;
    // Need to reverse y
    projected.y = 1.0 - projected.y;

    // NOTE: Transform to NDC not needed for Vulkan, but would be for OpenGL.
    // projected.xy = projected.xy * 0.5 + 0.5;

    if(global_settings.use_pcf == 1) {
        return calculate_pcf(projected, cascade_index, global_settings.shadow_bias);
    } 

    return calculate_unfiltered(projected, cascade_index, global_settings.shadow_bias);
}

// Based on a combination of GGX and Schlick-Beckmann approximation to calculate probability
// of overshadowing micro-facets.
float geometry_schlick_ggx(float normal_dot_direction, float roughness) {
    roughness += 1.0;
    float k = (roughness * roughness) / 8.0;
    return normal_dot_direction / (normal_dot_direction * (1.0 - k) + k);
}

void unpack_u32_u8s(uint n, out uint x, out uint y, out uint z, out uint w) {
    x = (n >> 24) & 0xFFu;
    y = (n >> 16) & 0xFFu;
    z = (n >> 8) & 0xFFu;
    w = n & 0xFFu;
}

void unpack_u32_u16s(uint n, out uint x, out uint y) {
    x = (n >> 16) & 0xFF;
    y = n & 0xFF;
}
