#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const uint KMATERIAL_UBO_MAX_SHADOW_CASCADES = 4;

const uint KANIMATION_SSBO_MAX_BONES_PER_MESH = 64;

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

// Vertex inputs

// Standard vertex data
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;
layout(location = 5) in ivec4 in_bone_ids;
layout(location = 6) in vec4 in_weights;

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

	vec3 fog_colour;
	float fog_start;
	vec3 padding;
	float fog_end;

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
	light_data lights[]; // indexed by immediate.packed_point_light_indices (needs unpacking to 16x u8s)
} global_lighting;

// All materials
layout(std430, set = 0, binding = 3) readonly buffer global_materials_ssbo {
	base_material_data base_materials[]; // indexed by immediate.transform_index
} global_materials;

// All animation data
layout(std430, set = 0, binding = 4) readonly buffer global_animations_ssbo {
	animation_skin_data animations[]; // indexed by immediate.animation_index;
} global_animations;


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

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
	vec4 frag_position;
	vec4 clip_space;
	vec4 light_space_frag_pos[KMATERIAL_UBO_MAX_SHADOW_CASCADES];
	vec4 vertex_colour;
	vec4 tangent;
	vec3 normal;
	float view_depth;
	vec3 world_to_camera;
	vec2 tex_coord;
} out_dto;

/** 
 * Used to convert from NDC -> UVW by taking the x/y components and transforming them:
 * 
 *   xy *= 0.5 + 0.5
 */
const mat4 ndc_to_uvw = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

void main() {
	mat4 model = global_matrices.matrices[global_settings.mat_ssbo_tran_offset + immediate.transform_index];
	mat4 view = global_matrices.matrices[global_settings.mat_ssbo_view_offset + immediate.view_index];
	mat4 projection = global_matrices.matrices[global_settings.mat_ssbo_proj_offset + immediate.projection_index];
	base_material_data base_material = global_materials.base_materials[immediate.base_material_index];
	animation_skin_data skin = global_animations.animations[immediate.animation_index];
	mat4 bones[] = skin.bones;

	mat4 inv_view = inverse(view);
	vec3 view_position = vec3(inv_view[3]);

	if(base_material.material_type == 0) {
		out_dto.tex_coord = in_texcoord;
	} else if (base_material.material_type == 1) {
		out_dto.tex_coord = vec2((in_position.x * 0.5) + 0.5, (in_position.z * 0.5) + 0.5) * immediate.tiling;
	}
	out_dto.vertex_colour = in_colour;

	// Accumulate bone transform.
	mat4 bone_transform = mat4(0.0);
	bone_transform += (bones[in_bone_ids[0]] * in_weights[0]);
	bone_transform += (bones[in_bone_ids[1]] * in_weights[1]);
	bone_transform += (bones[in_bone_ids[2]] * in_weights[2]);
	bone_transform += (bones[in_bone_ids[3]] * in_weights[3]);
	// Fragment position in world space.
	out_dto.frag_position = model * bone_transform * vec4(in_position, 1.0);
	// Copy the normal over.
	mat3 m3_model = mat3(model);
	vec4 normal4 = bone_transform * vec4(in_normal, 0.0);
	out_dto.normal = normalize(m3_model * normal4.xyz);
	out_dto.tangent.xyz = normalize(m3_model * vec3(in_tangent));
	out_dto.tangent.w = in_tangent.w;
	vec4 view_fragpos = view * out_dto.frag_position;
	out_dto.view_depth = -view_fragpos.z;
	out_dto.clip_space = projection * view_fragpos;
	gl_Position = out_dto.clip_space;

	// Apply clipping plane
	gl_ClipDistance[0] = dot(out_dto.frag_position, immediate.clipping_plane);

	// Get a light-space-transformed fragment positions.
	for(int i = 0; i < KMATERIAL_UBO_MAX_SHADOW_CASCADES; ++i) {
		out_dto.light_space_frag_pos[i] = (ndc_to_uvw * global_settings.directional_light_spaces[i]) * out_dto.frag_position;
	}

	out_dto.world_to_camera = view_position.xyz - out_dto.frag_position.xyz;
}

