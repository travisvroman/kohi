#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const uint MATERIAL_MAX_SHADOW_CASCADES = 4;
const uint KMATERIAL_MAX_GLOBAL_POINT_LIGHTS = 64;
const uint MATERIAL_MAX_VIEWS = 4;

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

// =========================================================
// Inputs
// =========================================================

// Vertex inputs
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

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

// per-group, "base material" data
layout(set = 1, binding = 0) uniform kmaterial_standard_base_uniform_data {
    // Packed texture channels for various maps requiring it.
    uint texture_channels; // [metallic, roughness, ao, unused]
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
    vec3 padding;
} material_group_ubo;

// per-draw, "material instance" data
layout(push_constant) uniform per_draw_ubo {
    mat4 model;
    vec4 clipping_plane;
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
    uvec2 packed_point_light_indices; // 8 bytes
    uint num_p_lights;
    uint irradiance_cubemap_index;
} material_draw_ubo;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
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
} out_dto;

void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w);

void main() {
	out_dto.tex_coord = in_texcoord;
    out_dto.vertex_colour = in_colour;
	// Fragment position in world space.
	out_dto.frag_position = material_draw_ubo.model * vec4(in_position, 1.0);
	// Copy the normal over.
	mat3 m3_model = mat3(material_draw_ubo.model);
	out_dto.normal = normalize(m3_model * in_normal);
	out_dto.tangent = normalize(m3_model * vec3(in_tangent));
    gl_Position = material_frame_ubo.projection * material_frame_ubo.view * material_draw_ubo.model * vec4(in_position, 1.0);

	// Apply clipping plane
	vec4 world_position = material_draw_ubo.model * vec4(in_position, 1.0);
	gl_ClipDistance[0] = dot(world_position, material_draw_ubo.clipping_plane);

	// Get a light-space-transformed fragment positions.
    for(int i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (ndc_to_uvw * material_frame_ubo.directional_light_spaces[i]) * out_dto.frag_position;
    }

    // Unpack texture map channels
    unpack_u32(material_group_ubo.texture_channels, out_dto.metallic_texture_channel, out_dto.roughness_texture_channel, out_dto.ao_texture_channel, out_dto.unused_texture_channel);
}

void unpack_u32(uint n, out uint x, out uint y, out uint z, out uint w) {
    x = (n >> 24) & 0xFF;
    y = (n >> 16) & 0xFF;
    z = (n >> 8) & 0xFF;
    w = n & 0xFF;
}

