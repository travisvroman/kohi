#pragma once

#include "core_render_types.h"
#include "strings/kstring_id.h"
#include <math/math_types.h>

/**
 * Brainstorm/spec:
 *
 * Different from heightmap terrain. Core design points:
 *
 *  - No transform (always identity). Simplifies rendering and collision detection.
 *  - Height scale:
 *    - Sea water is exactly at y=0. Height min/max should probably be somewhere around
 *      +/- 2000 meters.
 *  - Splatmap resolution: 64x64 per chunk. Each chunk has its own. This gives a resolution of
 *    8 units per quad.
 *    Terrain is built from E->W, S->N.
 *
 *  - The scene defines a grid of "Kohi Terrain Tile" entities.
 *  - Allows holes to be defined so that player may pass through. Height = -1
 *  - Array of smaller blocks.
 *  - Each block:
 *    - Split into chunks, each 8x8=64 quads (grid of 9x9=89 vertices, can LOD in halves). Verts duplicated at joints.
 *    - 16x16 chunks make up a KTT
 *    - Each quad can have a max of 5 materials:
 *      - Base material that is always applied at 1.0 opacity.
 *      - Other 4 defined by RGBA splatmap.
 *      - Terrain materials for now will each support a lightweight PBR setup.
 *    - Visibility of chunks should be managed by a quadtree for culling.
 *    - Resolution of chunks: 2 quads per meter. Textures tile at 1 meter (so 2 quads)
 *    - Ideally, resolution of splat maps should be 4 px per quad (i.e. 4x8=48 per chunk)
 *  - LOD applied at KTT level to all tiles within it.
 *
 *  FUTURE: Can add one more vertex to the center of each quad for more resolution, and
 *  drop this at a distance for easy LOD.
 */

// How many quads in a single chunk.
#define HF_CHUNK_QUAD_COUNT 8
// How large a chunk is in the world.
#define HF_CHUNK_SIZE_WORLD 8.0f
// How many vertices per chunk on x and z axes
#define HF_VERTEX_STRIDE (HF_CHUNK_QUAD_COUNT + 1)
// How many chunks per block on x and z axes
#define HF_BLOCK_CHUNK_DIM 16

#define HF_BLOCK_SIZE_WORLD HF_CHUNK_SIZE_WORLD *HF_BLOCK_CHUNK_DIM;

// How many quads per block on x and z axes.
#define HF_BLOCK_QUAD_COUNT (HF_CHUNK_QUAD_COUNT * HF_BLOCK_CHUNK_DIM)
// How many vertices in a single chunk
#define HF_CHUNK_VERTEX_COUNT (HF_VERTEX_STRIDE * HF_VERTEX_STRIDE)

// The number of world-units taken up per quad of terrain.
#define HF_QUAD_SCALE 2

// Chunks per block
#define HF_BLOCK_CHUNK_COUNT 256

#define HF_BLOCK_VERTEX_COUNT (HF_CHUNK_VERTEX_COUNT * HF_BLOCK_CHUNK_COUNT)

#define HF_INDEX_COUNT 384

// Max number of materials allowed per chunk
#define HF_TERRAIN_CHUNK_MAX_MATERIALS 5
// Max number of materials across the whole terrain.
#define HF_TERRAIN_MAX_MATERIALS 16

#define HF_TERRAIN_SPLATMAP_RESOLUTION 1024

#define HF_TERRAIN_MAX_BOUND_POINT_LIGHTS 8

struct frame_data;
struct kasset_hf_terrain;

typedef enum hf_terrain_material_map {
	HF_TERRAIN_MATERIAL_MAP_ALBEDO = 0,
	HF_TERRAIN_MATERIAL_MAP_NORMAL = 1,
	HF_TERRAIN_MATERIAL_MAP_MRA = 2
} hf_terrain_material_map;

// Heightfield Vertex 3D
typedef struct hf_vertex_3d {
	vec4 position; // w holds texcoord x
	vec4 normal;   // w holds texcoord y
	vec4 tangent;  // NOTE: w is available
} hf_vertex_3d;

// 8 quads, 2 quads per meter, covers 4^2 meters.
typedef struct hf_chunk {
	u64 vertex_buffer_offset;
	u32 vertex_offset;

	u8 material_indices[HF_TERRAIN_CHUNK_MAX_MATERIALS];

	extents_3d aabb;
	// Separate AABB with greater extents on Y axis to prevent
	// actions from getting clipped.
	extents_3d editor_aabb;

	// array index.
	u16 index;
	// x index, not coordinate
	u16 x;
	// z index, not coordinate
	u16 z;
} hf_chunk;

// 16x16 chunks, covers 64^2 meters.
typedef struct hf_block {
	// 16x16
	hf_chunk chunks[HF_BLOCK_CHUNK_COUNT];

	extents_3d aabb;
	// Separate AABB with greater extents on Y axis to prevent
	// actions from getting clipped.
	extents_3d editor_aabb;

	// array index.
	u16 index;
	// x index, not coordinate
	u16 x;
	// z index, not coordinate
	u16 z;

	ktexture splatmap;
	u8 *splatmap_pixels;

	u32 shader_instance_id;

} hf_block;

// Represents the entire terrain.
typedef struct hf_terrain {
	// Number of blocks along x axis. Must be at least 1.
	u16 block_count_x;
	// Number of blocks along z axis. Must be at least 1.
	u16 block_count_z;

	// These are shared amongst all chunks. Technically should probably be
	// at a higher level than this even.
	u32 indices[HF_INDEX_COUNT];
	u64 index_buffer_offset;

	u32 vertex_count;
	u64 base_vertex_buffer_offset;
	hf_vertex_3d *vertices;

	extents_3d aabb;
	// Separate AABB with greater extents on Y axis to prevent
	// actions from getting clipped.
	extents_3d editor_aabb;

	hf_block *blocks;

	// Storing a reference to the shader for convenience.
	kshader hf_terrain_shader;

	u8 material_count;
	ktexture albedo_texture_array;
	ktexture normal_texture_array;
	ktexture mra_texture_array;

	// Case-sensitive material name
	kstring_id material_names[HF_TERRAIN_MAX_MATERIALS];

	kname albedo_asset_names[HF_TERRAIN_MAX_MATERIALS];
	kname albedo_package_names[HF_TERRAIN_MAX_MATERIALS];
	kname normal_asset_names[HF_TERRAIN_MAX_MATERIALS];
	kname normal_package_names[HF_TERRAIN_MAX_MATERIALS];
	kname mra_asset_names[HF_TERRAIN_MAX_MATERIALS];
	kname mra_package_names[HF_TERRAIN_MAX_MATERIALS];
} hf_terrain;

typedef struct hf_terrain_chunk_render_data {
	u64 vertex_buffer_offset;
	u64 vertex_count;
	u32 base_material_index;

	uvec4 material_indices;

	u8 bound_point_light_count;
	u8 bound_point_light_indices[HF_TERRAIN_MAX_BOUND_POINT_LIGHTS];
} hf_terrain_chunk_render_data;

typedef struct hf_terrain_block_render_data {
	u64 chunk_count;
	hf_terrain_chunk_render_data *chunks;
	u32 shader_instance_id;
	ktexture splatmap;
} hf_terrain_block_render_data;

typedef struct hf_terrain_render_data {
	u64 index_buffer_offset;
	u64 index_count;

	u32 block_count;
	hf_terrain_block_render_data *blocks;

	ktexture albedo_texture_array;
	ktexture normal_texture_array;
	ktexture mra_texture_array;

} hf_terrain_render_data;

KAPI hf_terrain hf_terrain_generate (u16 blocks_x, u16 blocks_z);
KAPI hf_terrain hf_terrain_create_from_asset (const struct kasset_hf_terrain *asset);

KAPI void hf_terrain_destroy (hf_terrain *t);

KAPI void hf_terrain_get_render_data (const hf_terrain *t, struct frame_data *p_frame_data, hf_terrain_render_data *render_data);

KAPI hf_block *hf_terrain_get_block_at (const hf_terrain *t, u8 x, u8 z);
KAPI hf_chunk *hf_terrain_block_get_chunk_at (hf_block *block, u8 x, u8 z);

KAPI i32 hf_terrain_chunk_get_vert_index_at (const hf_chunk *chunk, u8 x, u8 z);

// NOTE: recalculates ALL vertices (normals and tangents) for the entire terrain. Don't do this unless absolutely required.
KAPI void hf_terrain_recalculate_vertices (hf_terrain *t);

KAPI void hf_terrain_chunk_recalculate_vertices (hf_terrain *t, hf_chunk *chunk);

KAPI void hf_terrain_material_texture_set (hf_terrain *t, u8 material_index, hf_terrain_material_map map, ktexture texture);

/**
 * Attempts to retrieve the terrain height at the given location. A false result means there is no terrain at that
 * location, which can mean _either_ out-of-bounds or that there is a hole in the terrain at that location.
 *
 */
KAPI b8 hf_terrain_get_height_at (const hf_terrain *t, f32 world_x, f32 world_z, vec3 *out_pos, vec3 *out_normal);
KAPI b8 hf_terrain_get_height_at_fast (const hf_terrain *t, f32 world_x, f32 world_z, f32 *out_height);

KAPI hf_vertex_3d *hf_terrain_chunk_get_closest_vertex (const hf_terrain *terrain, const hf_block *block, const hf_chunk *chunk, vec3 pos, u32 *out_x, u32 *out_z, i64 *out_index);
