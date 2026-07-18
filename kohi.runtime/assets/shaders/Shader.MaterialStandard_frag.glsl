#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const float PI = 3.14159265359;

const uint KMATERIAL_UBO_MAX_SHADOW_CASCADES = 4;

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

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

const uint MAT_WATER_IDX_NORMAL = 1; // MAT_STANDARD_IDX_NORMAL;
const uint MAT_WATER_IDX_REFLECTION = 2; // MAT_STANDARD_IDX_METALLIC;
const uint MAT_WATER_IDX_REFRACTION = 3; // MAT_STANDARD_IDX_ROUGHNESS;
const uint MAT_WATER_IDX_REFRACTION_DEPTH = 4; // MAT_STANDARD_IDX_AO;
const uint MAT_WATER_IDX_DUDV = 5; // MAT_STANDARD_IDX_MRA;

const uint MAT_TYPE_STANDARD = 0;
const uint MAT_TYPE_WATER = 1;

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
const uint KMATERIAL_FLAG_MASKED_BIT = 0x0400;

const uint MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX = 0x0001;
const uint MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX = 0x0002;
const uint MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX = 0x0004;
const uint MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX = 0x0008;
const uint MATERIAL_STANDARD_FLAG_USE_AO_TEX = 0x0010;
const uint MATERIAL_STANDARD_FLAG_USE_MRA_TEX = 0x0020;
const uint MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX = 0x0040;

const uint KMATERIAL_DATA_INDEX_VIEW = 0;
const uint KMATERIAL_DATA_INDEX_PROJECTION = 1;

const uint KMATERIAL_DATA_INDEX2_ANIMATION = 0;
const uint KMATERIAL_DATA_INDEX2_BASE_MATERIAL = 1;

struct light_data {
	// Directional light: .rgb = colour, .w = ignored - Point lights: .rgb = colour, .a = linear 
	vec4 colour;
	// Directional Light: .xyz = direction, .w = ignored - Point lights: .xyz = position, .w = quadratic
	vec4 position;
};

struct base_material_data {
	uint metallic_texture_channel;
	uint roughness_texture_channel;
	uint ao_texture_channel;
	/** @brief The material lighting model. */
	uint lighting_model;

	// Base set of flags for the material. Copied to the material instance when created.
	uint flags;
	// Texture use flags
	uint tex_flags;
	float refraction_scale;
	uint material_type; // 0 = standard, 1 = water

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
};

struct animation_skin_data {
	mat4 bones[KANIMATION_SSBO_MAX_BONES_PER_MESH];
};


// =========================================================
// Inputs
// =========================================================

// Binding Set 0

// Global settings for the scene.
layout(std140, set = 0, binding = 0) uniform kmaterial_settings_ubo {
	mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes
	vec4 cascade_splits;										 // 16 bytes

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
	vec2 padding;

	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
	uint mat_ssbo_tran_offset;
	uint mat_ssbo_genc_offset;
} global_settings;

// All matrices
layout(std430, set = 0, binding = 1) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

// All lighting
layout(std430, set = 0, binding = 2) readonly buffer global_lighting_ssbo {
	light_data lights[]; // indexed by in_dto.packed_point_light_indices (needs unpacking to 16x u8s)
} global_lighting;

// All materials
layout(std430, set = 0, binding = 3) readonly buffer global_materials_ssbo {
	base_material_data base_materials[]; // indexed by in_dto.transform_index
} global_materials;

// All animation data
layout(std430, set = 0, binding = 4) readonly buffer global_animations_ssbo {
	animation_skin_data animations[]; // indexed by in_dto.animation_index;
} global_animations;

layout(set = 0, binding = 5) uniform texture2DArray shadow_texture;
layout(set = 0, binding = 6) uniform sampler shadow_sampler;
layout(set = 0, binding = 7) uniform textureCube irradiance_textures[MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];
layout(set = 0, binding = 8) uniform sampler irradiance_sampler;

// Binding Set 1 
layout(set = 1, binding = 0) uniform texture2D material_textures[MATERIAL_STANDARD_TEXTURE_COUNT];
layout(set = 1, binding = 1) uniform sampler material_samplers[MATERIAL_STANDARD_SAMPLER_COUNT];

// Immediate data
layout(push_constant) uniform immediate_data {
	// bytes 0-15
	uint view_index;
	uint projection_index;
	uint animation_index;
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
	uint dir_light_index; // probably zero
	float tiling;		  // only used for water materials
	float wave_strength;   // only used for water materials
	float wave_speed;	  // only used for water materials

	// bytes 64-79
	uint transform_index;
	uint geo_type; // 0=static, 1=animated
	float near_clip;
	float far_clip;
	// 80-128 available
} immediate;

// Data Transfer Object
layout(location = 0) in dto {
	vec4 frag_position;
	vec4 clip_space;
	vec4 light_space_frag_pos[KMATERIAL_UBO_MAX_SHADOW_CASCADES];
	vec4 vertex_colour;
	vec4 tangent;
	vec3 normal;
	float view_depth;
	vec3 world_to_camera;
	vec2 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance, float diffuse_mult);
vec3 calculate_point_light_radiance(light_data point_light, vec3 view_direction, vec3 frag_position_xyz);
vec3 calculate_directional_light_radiance(vec3 colour, vec3 view_direction);
float calculate_pcf(vec3 projected, int cascade_index, float shadow_bias);
float calculate_unfiltered(vec3 projected, int cascade_index, float shadow_bias);
float calculate_shadow(vec4 light_space_frag_pos, vec3 normal, int cascade_index);
float geometry_schlick_ggx(float normal_dot_direction, float roughness);
void unpack_u32_u8s(uint n, out uint x, out uint y, out uint z, out uint w);
void unpack_u32_u16s(uint n, out uint x, out uint y);
bool flag_get(uint flags, uint flag);
uint flag_set(uint flags, uint flag, bool enabled);

void main() {
	mat4 view = global_matrices.matrices[global_settings.mat_ssbo_view_offset + immediate.view_index];
	mat4 inv_view = inverse(view);
	vec3 view_position = vec3(inv_view[3]);
	base_material_data base_material = global_materials.base_materials[immediate.base_material_index];

	vec2 distorted_texcoords;
	vec3 normal;
	vec3 tangent;
	vec2 tex_coord;
	vec4 normal_colour = vec4(0, 1, 0, 1);

	if(base_material.material_type == MAT_TYPE_STANDARD) {
		normal = in_dto.normal;
		tangent = in_dto.tangent.xyz;
		tex_coord = in_dto.tex_coord;
	} else if (base_material.material_type == MAT_TYPE_WATER) {
		normal = in_dto.normal;
		tangent = in_dto.tangent.xyz;
		float move_factor = immediate.wave_speed * global_settings.game_time;
		// Calculate surface distortion and bring it into [-1.0 - 1.0] range
		distorted_texcoords = texture(sampler2D(material_textures[MAT_WATER_IDX_DUDV], material_samplers[MAT_WATER_IDX_DUDV]), vec2(in_dto.tex_coord.x + move_factor, in_dto.tex_coord.y)).rg * 0.1;
		distorted_texcoords = in_dto.tex_coord + vec2(distorted_texcoords.x, distorted_texcoords.y + move_factor);

		tex_coord = distorted_texcoords;
	}

	tangent = (tangent - dot(tangent, normal) *  normal);
	vec3 bitangent = normalize(cross(normal, tangent.xyz) * in_dto.tangent.w);
	tangent = normalize(cross(bitangent, normal));
	mat3 TBN = mat3(tangent, bitangent, normal);

	// Calculate "local normal".
	// If enabled, get the normal from the normal map if used, or the supplied vector if not.
	// Otherwise, just use a default z-up
	vec3 local_normal = vec3(0, 0, 1.0);
	if(flag_get(base_material.flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT)){
		if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX)) {
			normal_colour = texture(sampler2D(material_textures[MAT_STANDARD_IDX_NORMAL], material_samplers[MAT_STANDARD_IDX_NORMAL]), tex_coord);
			local_normal = normal_colour.rgb * 2.0 - 1.0;
		} else {
			// local_normal = base_material.normal * 2.0 - 1.0;
		}
	} 

	// Update the normal to use a sample from the normal map.
	normal = normalize(TBN * local_normal);

	vec3 cascade_colour = vec3(1.0);

	light_data directional_light = global_lighting.lights[immediate.dir_light_index];

	base_material_data material = global_materials.base_materials[immediate.base_material_index];

	vec3 albedo;
	float metallic;
	float roughness;
	float ao;
	// Emissive - defaults to 0 if not used.
	vec3 emissive = vec3(0.0);
	// Alpha defaults to 1.0 if not used.
	float alpha = 1.0;

	vec3 base_reflectivity;

	// Shadows: 1.0 means NOT in shadow, which is the default.
	float shadow = 1.0;
	// Only perform shadow calculations if this receives shadows.
	if(flag_get(base_material.flags, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT)) { 

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
		}
		// Fade out the shadow map past a certain distance.
		float fade_start = global_settings.shadow_distance;
		float fade_distance = global_settings.shadow_fade_distance;

		// The end of the fade-out range.
		float fade_end = fade_start + fade_distance;

		float zclamp = clamp(length(view_position.xyz - in_dto.frag_position.xyz), fade_start, fade_end);
		float fade_factor = (fade_end - zclamp) / (fade_end - fade_start + 0.00001); // Avoid divide by 0

		shadow = clamp(shadow + (1.0 - fade_factor), 0.0, 1.0);
	} 

	if(base_material.material_type == MAT_TYPE_STANDARD) {

		// Base colour
		vec4 base_colour_samp;
		if(flag_get(base_material.flags, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT)) {
			// Pass through the vertex colour
			base_colour_samp = in_dto.vertex_colour;
		} else {
			// Use base colour texture if provided; otherwise use the colour.
			if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX)) {
				base_colour_samp = texture(sampler2D(material_textures[MAT_STANDARD_IDX_BASE_COLOUR], material_samplers[MAT_STANDARD_IDX_BASE_COLOUR]), in_dto.tex_coord);
				// Use this to force LOD
				// base_colour_samp = textureLod(sampler2D(material_textures[MAT_STANDARD_IDX_BASE_COLOUR], material_samplers[MAT_STANDARD_IDX_BASE_COLOUR]), in_dto.tex_coord, 0.0);
			} else {
				base_colour_samp = base_material.base_colour;
			}
		}

		// discard the fragment if using transparency and masking, and the alpha falls below a given threshold.
		if(base_colour_samp.a < 0.1 && flag_get(base_material.flags, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT) && flag_get(base_material.flags, KMATERIAL_FLAG_MASKED_BIT)) {
			discard;
		}
		if(global_settings.render_mode != 0) {
			base_colour_samp = vec4(1.0);
		}
		albedo = pow(base_colour_samp.rgb, vec3(2.2));

		// Either use combined MRA (metallic/roughness/ao) or individual maps, depending on settings.
		vec3 mra;
		if(flag_get(base_material.flags, KMATERIAL_FLAG_MRA_ENABLED_BIT)) { 
			if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_MRA_TEX)) {
				mra = texture(sampler2D(material_textures[MAT_STANDARD_IDX_MRA], material_samplers[MAT_STANDARD_IDX_MRA]), in_dto.tex_coord).rgb;
			} else {
				mra = base_material.mra;
			}
		} else {
			// Sample individual maps.

			// Metallic 
			if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX)) {
				vec4 sampled = texture(sampler2D(material_textures[MAT_STANDARD_IDX_METALLIC], material_samplers[MAT_STANDARD_IDX_METALLIC]), in_dto.tex_coord);
				// Load metallic into the red channel from the configured source texture channel.
				mra.r = sampled[material.metallic_texture_channel];
			} else {
				mra.r = base_material.metallic;
			}

			// Roughness 
			if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX)) {
				vec4 sampled = texture(sampler2D(material_textures[MAT_STANDARD_IDX_ROUGHNESS], material_samplers[MAT_STANDARD_IDX_ROUGHNESS]), in_dto.tex_coord);
				// Load roughness into the green channel from the configured source texture channel.
				mra.g = sampled[material.roughness_texture_channel];
			} else {
				mra.g = base_material.roughness;
			}

			// AO - default to 1.0 (i.e. no effect), and only read in a value if this is enabled.
			mra.b = 1.0;
			if(flag_get(base_material.flags, KMATERIAL_FLAG_AO_ENABLED_BIT)) { 
				if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_AO_TEX)) {
					vec4 sampled = texture(sampler2D(material_textures[MAT_STANDARD_IDX_AO], material_samplers[MAT_STANDARD_IDX_AO]), in_dto.tex_coord);
					// Load AO into the blue channel from the configured source texture channel.
					mra.b = sampled[material.ao_texture_channel];
				} else {
					mra.b = base_material.ao;
				}
			}
		}

		// Only use emissive in render modes 0 and 1
		if(global_settings.render_mode == 0 || global_settings.render_mode == 1) {
			if(flag_get(base_material.flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT)) { 
				if(flag_get(base_material.tex_flags, MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX)) {
					emissive = texture(sampler2D(material_textures[MAT_STANDARD_IDX_EMISSIVE], material_samplers[MAT_STANDARD_IDX_EMISSIVE]), in_dto.tex_coord).rgb;
				} else {
					emissive = base_material.emissive.rgb;
				}
			}
		}

		// Ensure the alpha is based on the albedo's original alpha value if transparency is enabled.
		// If it's not enabled, just use 1.0.
		if(flag_get(base_material.flags, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT)) {
			alpha = base_colour_samp.a;
		}

		metallic = mra.r;
		roughness = mra.g;
		ao = mra.b;

		// calculate reflectance at normal incidence; if dia-electric (like plastic) use base_reflectivity 
		// of 0.04 and if it's a metal, use the albedo color as base_reflectivity (metallic workflow)	
		base_reflectivity = vec3(0.02); 
		base_reflectivity = mix(base_reflectivity, albedo, metallic);
	} else if (base_material.material_type == MAT_TYPE_WATER) {
		// These can be hardcoded for water surfaces.
		metallic = 0.0;
		roughness = 0.02;
		ao = 1.0;

		float water_depth = 0;

		// Perspective division to NDC for texture projection, then to screen space.
		vec2 ndc = (in_dto.clip_space.xy / in_dto.clip_space.w) / 2.0 + 0.5;
		vec2 reflect_texcoord = vec2(ndc.x, ndc.y);
		vec2 refract_texcoord = vec2(ndc.x, -ndc.y);

		float near = immediate.near_clip;
		float far = immediate.far_clip;
		// Depth of underlying geometry to screen, converted to linear distance.
		float depth = texture(sampler2D(material_textures[MAT_WATER_IDX_REFRACTION_DEPTH], material_samplers[MAT_WATER_IDX_REFRACTION_DEPTH]), refract_texcoord).r;
		float floor_distance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));

		// Distance of the water plane fragment from the screen, converted to linear distance.
		depth = gl_FragCoord.z;
		float water_distance = 2.0 * near * far / (far + near - (2.0 * depth - 1.0) * (far - near));

		// Distance between water surface and underlying geometry.
		water_depth = floor_distance - water_distance;

		float distortion_edge_depth_falloff = 0.25;
		float distortion_depth_influence = clamp((water_depth / distortion_edge_depth_falloff), 0.0, 1.0);

		// Distort further
		vec2 distortion_total = (texture(sampler2D(material_textures[MAT_WATER_IDX_DUDV], material_samplers[MAT_WATER_IDX_DUDV]), distorted_texcoords).rg * 2.0 - 1.0) * immediate.wave_strength;
		distortion_total *= distortion_depth_influence;

		reflect_texcoord += distortion_total;
		// Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
		reflect_texcoord = clamp(reflect_texcoord, 0.001, 0.999);

		refract_texcoord += distortion_total;
		// Avoid edge artifacts by clamping slightly inward to prevent texture wrapping.
		refract_texcoord.x = clamp(refract_texcoord.x, 0.001, 0.999);
		refract_texcoord.y = clamp(refract_texcoord.y, -0.999, -0.001); // Account for flipped y-axis

		float wet_depth_falloff = 0.025;
		float wet_depth_influence = clamp((water_depth / wet_depth_falloff), 0.0, 1.0);

		vec4 reflect_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_REFLECTION], material_samplers[MAT_WATER_IDX_REFLECTION]), reflect_texcoord);
		vec4 refract_colour = texture(sampler2D(material_textures[MAT_WATER_IDX_REFRACTION], material_samplers[MAT_WATER_IDX_REFRACTION]), refract_texcoord);
		// Refract should be slightly darker since it's wet.
		refract_colour.rgb = clamp(refract_colour.rgb - (vec3(0.075) * wet_depth_influence), vec3(0.0), vec3(1.0));

		// Base the fresnel effect more on the calculated normal up close, but more on the geometric
		// normal in the distance to smooth things out.
		vec3 geo_normal = in_dto.normal;
		float surf_norm_max_dist = 100.0; // TODO: configurable?
		float dist_fade = clamp(distance(view_position.xyz, in_dto.frag_position.xyz) / surf_norm_max_dist, 0.0, 1.0);
		float wave_influence = 0.2 * (1.0 - dist_fade);
		vec3 n_fresnel = normalize(mix(geo_normal, normal, wave_influence));

		float cos_theta = clamp(dot(normalize(n_fresnel), normalize(in_dto.world_to_camera)), 0.0, 1.0);
		base_reflectivity = vec3(0.02); // base reflectivity
		float F0 = base_reflectivity.r;
		float fresnel_factor = F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
		fresnel_factor = clamp(fresnel_factor, 0.0, 1.0);

		float tint_edge_depth_falloff = 10.0; // TODO: configurable.
		float tint_depth_influence = clamp((water_depth / tint_edge_depth_falloff), 0.0, 1.0);

		// Apply tint to refraction only. Alpha is the "strength"
		if(global_settings.render_mode == 0) {
			vec4 tint = vec4(0.0, 0.3, 0.5, 1.0); // TODO: configurable.
			// If in shadow, reduce the tint colour some.
			tint -= (vec4(0.1, 0.05, 0.01, 0.0) * (1.0 - shadow));
			tint = clamp(tint, vec4(0, 0, 0, 1), vec4(1));
			refract_colour = mix(refract_colour, tint, tint.a * (tint_depth_influence));
		}

		float reflection_edge_depth_falloff = 0.5;
		albedo = mix(refract_colour, reflect_colour, fresnel_factor * clamp((water_depth / reflection_edge_depth_falloff), 0.0, 1.0)).rgb;
		alpha = 1.0;
	}

	if(global_settings.render_mode == 0 || global_settings.render_mode == 1 || global_settings.render_mode == 3) {
		vec3 view_direction = normalize(view_position.xyz - in_dto.frag_position.xyz);

		// Don't include albedo in mode 1 (lighting-only). Do this by using white 
		// multiplied by mode (mode 1 will result in white, mode 0 will be black),
		// then add this colour to albedo and clamp it. This will result in pure 
		// white for the albedo in mode 1, and normal albedo in mode 0, all without
		// branching.
		if (base_material.material_type == MAT_TYPE_STANDARD) {
			albedo += (vec3(1.0) * global_settings.render_mode);		 
			albedo = clamp(albedo, vec3(0.0), vec3(1.0));
		}

		// This is based off the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function).
		// This uses a micro-facet model to use roughness and metallic properties of materials to produce
		// physically accurate representation of material reflectance.

		// Overall reflectance.
		vec3 total_reflectance = vec3(0.0);

		float diffuse_mult = 1.0;
		if (base_material.material_type == MAT_TYPE_WATER) {
			diffuse_mult = 0.0;
		}

		// Directional light radiance.
		{
			vec3 light_direction = normalize(-directional_light.position.xyz); // position = direction for directional light
			vec3 radiance = calculate_directional_light_radiance(directional_light.colour.rgb, view_direction);

			// Only directional light should be affected by shadow map.
			total_reflectance += (shadow * calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance, diffuse_mult));
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

				total_reflectance += calculate_reflectance(albedo, normal, view_direction, light_direction, metallic, roughness, base_reflectivity, radiance, diffuse_mult);
				plights_rendered++;
			}
		}

		// Irradiance holds all the scene's indirect diffuse light. Use the surface normal to sample from it.
		vec3 irradiance = texture(samplerCube(irradiance_textures[immediate.irradiance_cubemap_index], irradiance_sampler), normal).rgb;
		// Combine irradiance with albedo and ambient occlusion. 
		// Also add in total accumulated reflectance.
		vec3 ambient = irradiance * albedo * ao;
		if (base_material.material_type == MAT_TYPE_WATER) {
			ambient =  albedo * ao;
		}
		// Modify total reflectance by the ambient colour.
		vec3 colour = ambient + total_reflectance;

		// HDR tonemapping - only on standard materials.
		if (base_material.material_type == MAT_TYPE_STANDARD) {
			colour = colour / (colour + vec3(1.0));
			// Gamma correction
			colour = pow(colour, vec3(1.0 / 2.2));
		}

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
		// LEFTOFF: Why this no worky?
		// wireframe, just render a solid colour.
		if(immediate.geo_type == 0) {
			out_colour = vec4(0.0, 1.0, 1.0, 1.0); // cyan
		} else if(immediate.geo_type == 1) {
			out_colour = vec4(1.0, 0.0, 1.0, 1.0); // magenta
		} else {
			out_colour = vec4(1.0, 0.0, 0.0, 1.0); // red - what are you doing, you dingus?
		}
	}
}

vec3 calculate_reflectance(vec3 albedo, vec3 normal, vec3 view_direction, vec3 light_direction, float metallic, float roughness, vec3 base_reflectivity, vec3 radiance, float diffuse_mult) {
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
	refraction_diffuse *= diffuse_mult;

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
	float energy_multiplier = 30.0;
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
