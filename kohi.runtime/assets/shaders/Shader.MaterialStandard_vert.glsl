#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const uint MATERIAL_MAX_SHADOW_CASCADES = 4;
const uint MATERIAL_MAX_POINT_LIGHTS = 10;
const uint MATERIAL_MAX_VIEWS = 4;

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
layout(location = 4) in vec3 in_tangent;

// per-frame
layout(set = 0, binding = 0) uniform per_frame_ubo {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 projection;
    mat4 views[MATERIAL_MAX_VIEWS];
    vec4 view_positions[MATERIAL_MAX_VIEWS];
    float cascade_splits[MATERIAL_MAX_SHADOW_CASCADES];
    float shadow_bias;
    uint render_mode;
    uint use_pcf;
    float delta_time;
    float game_time;
    vec2 padding;
} material_frame_ubo;

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
} material_group_ubo;

// per-draw
layout(push_constant) uniform per_draw_ubo {
    mat4 model;
    vec4 clipping_plane;
    uint view_index;
    uint irradiance_cubemap_index;
    vec2 padding;
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
	out_dto.tangent = normalize(m3_model * in_tangent);
    gl_Position = material_frame_ubo.projection * material_frame_ubo.views[material_draw_ubo.view_index] * material_draw_ubo.model * vec4(in_position, 1.0);

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

