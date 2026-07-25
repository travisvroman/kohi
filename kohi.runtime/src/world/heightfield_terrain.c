#include "heightfield_terrain.h"

#include "assets/kasset_types.h"
#include "core/engine.h"
#include "core/frame_data.h"
#include "core_render_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "runtime_defines.h"
#include "strings/kname.h"
#include "strings/kstring_id.h"
#include "systems/kshader_system.h"
#include "systems/texture_system.h"
#include <debug/kassert.h>
#include <memory/kmemory.h>

// Uncomment this for debug tracing.
#define HF_TERRAIN_TRACE 0

static void generate_normals (u32 vertex_count, hf_vertex_3d *vertices, u32 index_count, u32 *indices) {
	for (u32 i = 0; i < index_count; i += 3) {
		u32 i0 = indices[i + 0];
		u32 i1 = indices[i + 1];
		u32 i2 = indices[i + 2];

		vec3 edge1 = vec3_from_vec4(vec4_sub(vertices[i1].position, vertices[i0].position));
		vec3 edge2 = vec3_from_vec4(vec4_sub(vertices[i2].position, vertices[i0].position));

		vec3 normal = vec3_normalized(vec3_cross(edge1, edge2));

		// NOTE: This just generates a face normal. Smoothing out should be done in
		// a separate pass if desired.
		vertices[i0].normal = vec4_from_vec3(normal, vertices[i0].normal.w);
		vertices[i1].normal = vec4_from_vec3(normal, vertices[i1].normal.w);
		vertices[i2].normal = vec4_from_vec3(normal, vertices[i2].normal.w);
	}
}

static void generate_tangents (u32 vertex_count, hf_vertex_3d *vertices, u32 index_count, u32 *indices) {
	for (u32 i = 0; i < index_count; i += 3) {
		u32 i0 = indices[i + 0];
		u32 i1 = indices[i + 1];
		u32 i2 = indices[i + 2];

		vec3 edge1 = vec3_from_vec4(vec4_sub(vertices[i1].position, vertices[i0].position));
		vec3 edge2 = vec3_from_vec4(vec4_sub(vertices[i2].position, vertices[i0].position));

		f32 deltaU1 = vertices[i1].position.w - vertices[i0].position.w; // texcoord x
		f32 deltaV1 = vertices[i1].normal.w - vertices[i0].normal.w;	 // texcoord y

		f32 deltaU2 = vertices[i2].position.w - vertices[i0].position.w; // texcoord x
		f32 deltaV2 = vertices[i2].normal.w - vertices[i0].normal.w;	 // texcoord y

		f32 dividend = (deltaU1 * deltaV2 - deltaU2 * deltaV1);
		f32 fc = 1.0f / dividend;

		vec3 tangent = (vec3){(fc * (deltaV2 * edge1.x - deltaV1 * edge2.x)),
							  (fc * (deltaV2 * edge1.y - deltaV1 * edge2.y)),
							  (fc * (deltaV2 * edge1.z - deltaV1 * edge2.z))};

		tangent = vec3_normalized(tangent);

		f32 sx = deltaU1, sy = deltaU2;
		f32 tx = deltaV1, ty = deltaV2;
		f32 handedness = ((tx * sy - ty * sx) < 0.0f) ? -1.0f : 1.0f;

		vec3 t4 = vec3_mul_scalar(tangent, handedness);
		// Encode handedness into w.
		vertices[i0].tangent = vec4_from_vec3(t4, handedness);
		vertices[i1].tangent = vec4_from_vec3(t4, handedness);
		vertices[i2].tangent = vec4_from_vec3(t4, handedness);
	}
}

void generate_chunk (hf_terrain *t, kasset_hf_terrain_vertex *asset_vertices, kasset_hf_terrain_chunk *asset_chunk, hf_chunk *chunk, u16 chunk_x, u16 chunk_z, u16 block_x, u16 block_z, u16 z_dim) {
	chunk->x = chunk_x;
	chunk->z = chunk_z;
	chunk->index = (chunk_z * HF_BLOCK_CHUNK_DIM) + chunk_x;

	// Get the offset into the array in elements.
	u64 block_offset = (block_z * HF_BLOCK_VERTEX_COUNT * z_dim) + (block_x * HF_BLOCK_VERTEX_COUNT);
	u64 chunk_offset = block_offset + (chunk_z * HF_CHUNK_VERTEX_COUNT * HF_BLOCK_CHUNK_DIM) + (HF_CHUNK_VERTEX_COUNT * chunk_x);
	chunk->vertex_offset = chunk_offset;
	chunk->vertex_buffer_offset = t->base_vertex_buffer_offset + (chunk_offset * sizeof(hf_vertex_3d));

	// Determines the size modification of each quad relative to a world unit.

	f32 block_base_z = t->aabb.min.z + (block_z * HF_BLOCK_QUAD_COUNT * HF_QUAD_SCALE);
	f32 block_base_x = t->aabb.min.x + (block_x * HF_BLOCK_QUAD_COUNT * HF_QUAD_SCALE);
	f32 chunk_base_z = block_base_z + (chunk_z * HF_CHUNK_QUAD_COUNT * HF_QUAD_SCALE);
	f32 chunk_base_x = block_base_x + (chunk_x * HF_CHUNK_QUAD_COUNT * HF_QUAD_SCALE);

	extents_3d extents = {
		.max = vec3_create(-999999.0f, -999999.0f, -999999.0f),
		.min = vec3_create(999999.0f, 999999.0f, 999999.0f)};

	for (u8 z = 0; z < HF_VERTEX_STRIDE; ++z) {
		for (u8 x = 0; x < HF_VERTEX_STRIDE; ++x) {
			u32 index = chunk_offset + (z * HF_VERTEX_STRIDE) + x;

			hf_vertex_3d *v = &t->vertices[index];

			v->position.x = chunk_base_x + (x * HF_QUAD_SCALE);
			v->position.z = chunk_base_z + (z * HF_QUAD_SCALE);

			v->position.y = asset_vertices ? asset_vertices[index].y_offset : -0.5f; // (ksin(v->position.x / HF_VERTEX_STRIDE) + kcos(v->position.z / HF_VERTEX_STRIDE)) * 2.0f;

			// Tex coords are encoded in normal/position.
			v->position.w = x + (chunk_x * HF_CHUNK_QUAD_COUNT);
			v->normal.w = z + (chunk_z * HF_CHUNK_QUAD_COUNT);

			// These will be calculated later.
			v->normal.x = 0;
			v->normal.y = 1.0f;
			v->normal.z = 0;

			extents.min = vec3_min(vec3_from_vec4(v->position), extents.min);
			extents.max = vec3_max(vec3_from_vec4(v->position), extents.max);
		}
	}

	generate_normals(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);
	generate_tangents(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);

	chunk->aabb = extents;
	// Pad y just a bit.
	chunk->aabb.max.y += 0.2f;
	chunk->aabb.min.y -= 0.2f;
	chunk->editor_aabb = chunk->aabb;
	chunk->editor_aabb.min.y = -999999.0;
	chunk->editor_aabb.max.y = 999999.0;

#if HF_TERRAIN_TRACE
	static u32 g_instance_count = 0;
	g_instance_count++;
	KTRACE("g_instance_count = %u", g_instance_count);
#endif

	if (asset_chunk) {
		for (u8 i = 0; i < HF_TERRAIN_CHUNK_MAX_MATERIALS; ++i) {
			chunk->material_indices[i] = asset_chunk->material_indices[i];
		}
	} else {

		// HACK: Hardcoding materials for now
		chunk->material_indices[0] = 0;
		chunk->material_indices[1] = 1;
		chunk->material_indices[2] = 2;
		chunk->material_indices[3] = 3;
		chunk->material_indices[4] = 4;

		// HACK: Showcasing different materials per chunk.
		if (chunk_x == 1 && chunk_z == 0) {
			// transition to new zone material set
			chunk->material_indices[0] = 0;
			chunk->material_indices[1] = 5;
			chunk->material_indices[2] = 2;
			chunk->material_indices[3] = 6;
			chunk->material_indices[4] = 3;
		}
		if (chunk_x == 2 && chunk_z == 0) {
			// new zone material set
			chunk->material_indices[0] = 5;
			chunk->material_indices[1] = 5;
			chunk->material_indices[2] = 2;
			chunk->material_indices[3] = 6;
			chunk->material_indices[4] = 3;
		}
	}
}

void generate_block (hf_terrain *t, kasset_hf_terrain_vertex *asset_vertices, kasset_hf_terrain_block *asset_block, hf_block *block, u16 block_x, u16 block_z, u16 z_dim) {

#if HF_TERRAIN_TRACE
	static u32 g_block_count = 0;
	g_block_count++;
	KTRACE("g_block_count = %u", g_block_count);
#endif

	extents_3d extents = {
		.max = vec3_create(-999999.0f, -999999.0f, -999999.0f),
		.min = vec3_create(999999.0f, 999999.0f, 999999.0f)};

	block->x = block_x;
	block->z = block_z;
	block->index = (block_z * z_dim) + block_x;

	for (u8 z = 0; z < HF_BLOCK_CHUNK_DIM; ++z) {
		for (u8 x = 0; x < HF_BLOCK_CHUNK_DIM; ++x) {
			u32 index = (z * HF_BLOCK_CHUNK_DIM) + x;
			generate_chunk(t, asset_vertices, asset_block ? &asset_block->chunks[index] : KNULL, &block->chunks[index], x, z, block_x, block_z, z_dim);

			extents.min = vec3_min(block->chunks[index].aabb.min, extents.min);
			extents.max = vec3_max(block->chunks[index].aabb.max, extents.max);
		}
	}

	// Create a splatmap for the block.
	kname name = kname_format("hf_terrain_splatmap_block_%u_%u", block_z, block_x);

	u64 pixel_array_size = HF_TERRAIN_SPLATMAP_RESOLUTION * HF_TERRAIN_SPLATMAP_RESOLUTION * 4;
	u8 *pixels = KALLOC_TYPE_CARRAY(u8, pixel_array_size);

	if (asset_block) {
		kcopy_memory(pixels, asset_block->splatmap_pixels, sizeof(u8) * pixel_array_size);
	} else {
		kzero_memory(pixels, sizeof(u8) * pixel_array_size);
	}
	KDUPLICATE_TYPE_CARRAY(block->splatmap_pixels, pixels, u8, pixel_array_size);
	block->splatmap = texture_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, pixels, HF_TERRAIN_SPLATMAP_RESOLUTION, HF_TERRAIN_SPLATMAP_RESOLUTION, name);

	kfree(pixels);

	// Acquire shader resources.
	block->shader_instance_id = kshader_acquire_binding_set_instance(t->hf_terrain_shader, 1);

	block->aabb = extents;
	// Pad y just a bit.
	block->aabb.max.y += 0.2f;
	block->aabb.min.y -= 0.2f;
	block->editor_aabb = block->aabb;
	block->editor_aabb.min.y = -999999.0;
	block->editor_aabb.max.y = 999999.0;
}

hf_terrain hf_terrain_generate (u16 blocks_x, u16 blocks_z) {
	KASSERT(blocks_x > 0 && blocks_z > 0);

	hf_terrain t = {0};

	t.block_count_x = blocks_x;
	t.block_count_z = blocks_z;

	// Ensure the terrain is centered in the world.
	t.aabb.max.z = (t.block_count_z * HF_BLOCK_QUAD_COUNT * HF_QUAD_SCALE) * 0.5f;
	t.aabb.max.x = (t.block_count_x * HF_BLOCK_QUAD_COUNT * HF_QUAD_SCALE) * 0.5f;
	t.aabb.min.z = t.aabb.max.z * -1.0f;
	t.aabb.min.x = t.aabb.max.x * -1.0f;

	KALLOC_TYPE_CARRAY(hf_block, blocks_x * blocks_z);

	// Each chunk uses the same indices.
	// Surface indices. Generate 1 set of 6 per tile.
	for (u32 row = 0, i = 0; row < HF_CHUNK_QUAD_COUNT; row += 1) {
		for (u32 col = 0; col < HF_CHUNK_QUAD_COUNT; col += 1, i += 6) {
			u32 next_row = row + 1;
			u32 next_col = col + 1;
			u32 v0 = (row * HF_VERTEX_STRIDE) + col;
			u32 v1 = (row * HF_VERTEX_STRIDE) + next_col;
			u32 v2 = (next_row * HF_VERTEX_STRIDE) + col;
			u32 v3 = (next_row * HF_VERTEX_STRIDE) + next_col;

			t.indices[i + 0] = v2;
			t.indices[i + 1] = v1;
			t.indices[i + 2] = v0;
			t.indices[i + 3] = v3;
			t.indices[i + 4] = v1;
			t.indices[i + 5] = v2;
		}
	}

	// Acquire the shader.
	t.hf_terrain_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_HF_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
	KASSERT_DEBUG(t.hf_terrain_shader != KSHADER_INVALID);

	// Acquire the vertex/index buffers.
	struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));

	// Allocate index buffer space.
	u64 index_buffer_size = sizeof(u32) * HF_INDEX_COUNT;
	KASSERT(renderer_renderbuffer_allocate(renderer_system, index_buffer, index_buffer_size, &t.index_buffer_offset));

	// Allocate vertexchunk_base_x buffer space.
	u32 block_count = (blocks_x * blocks_z);
	t.vertex_count = (HF_VERTEX_STRIDE * HF_VERTEX_STRIDE) * HF_BLOCK_CHUNK_COUNT * block_count;
	t.vertices = KALLOC_TYPE_CARRAY(hf_vertex_3d, t.vertex_count);
	u64 vertex_buffer_size = sizeof(hf_vertex_3d) * t.vertex_count;
	KASSERT(renderer_renderbuffer_allocate(renderer_system, vertex_buffer, vertex_buffer_size, &t.base_vertex_buffer_offset));

	// Setup materials.
	t.material_count = 7;

	for (u8 i = 0; i < HF_TERRAIN_MAX_MATERIALS; ++i) {
		t.albedo_asset_names[i] = INVALID_KNAME;
		t.albedo_package_names[i] = INVALID_KNAME;
		t.normal_asset_names[i] = INVALID_KNAME;
		t.normal_package_names[i] = INVALID_KNAME;
		t.mra_asset_names[i] = INVALID_KNAME;
		t.mra_package_names[i] = INVALID_KNAME;
	}

	// TODO: use some default textures that are included with the engine.
	t.albedo_asset_names[0] = kname_create("dirtRocks_grass_mix");
	t.normal_asset_names[0] = kname_create("dirtRocks_grass_mix_NORM");
	t.mra_asset_names[0] = kname_create("dirtRocks_grass_mix_MRA");

	t.albedo_asset_names[1] = kname_create("dirtRocks_dark");
	t.normal_asset_names[1] = kname_create("dirtRocks_dark_NORM");
	t.mra_asset_names[1] = kname_create("dirtRocks_dark_MRA");

	t.albedo_asset_names[2] = kname_create("paverPath");
	t.normal_asset_names[2] = kname_create("paverPath_NORM");
	t.mra_asset_names[2] = kname_create("paverPath_MRA");

	t.albedo_asset_names[3] = kname_create("lushGrass");
	t.normal_asset_names[3] = kname_create("lushGrass_NORM");
	t.mra_asset_names[3] = kname_create("lushGrass_MRA");

	t.albedo_asset_names[4] = kname_create("yellowFlowers");
	t.normal_asset_names[4] = kname_create("yellowFlowers_NORM");
	t.mra_asset_names[4] = kname_create("yellowFlowers_MRA");

	t.albedo_asset_names[5] = kname_create("lushGrass_dry");
	t.normal_asset_names[5] = kname_create("lushGrass_dry_NORM");
	t.mra_asset_names[5] = kname_create("lushGrass_dry_MRA");

	t.albedo_asset_names[6] = kname_create("squarepaverpath_angled");
	t.normal_asset_names[6] = kname_create("squarepaverpath_angled_NORM");
	t.mra_asset_names[6] = kname_create("squarepaverpath_angled_MRA");

	t.albedo_texture_array = texture_acquire_layered_sync(kname_create("hf_terrain_albedo_tex_array"), HF_TERRAIN_MAX_MATERIALS, t.albedo_asset_names, t.albedo_package_names);
	t.normal_texture_array = texture_acquire_layered_sync(kname_create("hf_terrain_normal_tex_array"), HF_TERRAIN_MAX_MATERIALS, t.normal_asset_names, t.normal_package_names);
	t.mra_texture_array = texture_acquire_layered_sync(kname_create("hf_terrain_mra_tex_array"), HF_TERRAIN_MAX_MATERIALS, t.mra_asset_names, t.mra_package_names);

	t.blocks = KALLOC_TYPE_CARRAY(hf_block, blocks_z * blocks_x);

	// Generate blocks.
	for (u8 z = 0; z < blocks_z; ++z) {
		for (u8 x = 0; x < blocks_x; ++x) {
			u32 index = (z * blocks_z) + x;
			hf_block *block = &t.blocks[index];
			generate_block(&t, KNULL, KNULL, block, x, z, blocks_z);

			t.aabb.min = vec3_min(block->aabb.min, t.aabb.min);
			t.aabb.max = vec3_max(block->aabb.max, t.aabb.max);
		}
	}

	// Pad y just a bit.
	t.aabb.max.y += 0.2f;
	t.aabb.min.y -= 0.2f;

	t.editor_aabb = t.aabb;
	t.editor_aabb.min.y = -999999.0;
	t.editor_aabb.max.y = 999999.0;

	// Upload vertices
	KASSERT(renderer_renderbuffer_load_range(renderer_system, vertex_buffer, t.base_vertex_buffer_offset, vertex_buffer_size, t.vertices, false));

	// Upload indices
	KASSERT(renderer_renderbuffer_load_range(renderer_system, index_buffer, t.index_buffer_offset, index_buffer_size, t.indices, false));

	return t;
}

hf_terrain hf_terrain_create_from_asset (const kasset_hf_terrain *asset) {
	KASSERT(asset->block_count_x > 0 && asset->block_count_z > 0);

	hf_terrain t = {0};

	t.block_count_x = asset->block_count_x;
	t.block_count_z = asset->block_count_z;

	// Ensure the terrain is centered in the world.
	t.aabb.max.z = (t.block_count_z * HF_BLOCK_QUAD_COUNT * HF_QUAD_SCALE) * 0.5f;
	t.aabb.max.x = (t.block_count_x * HF_BLOCK_QUAD_COUNT * HF_QUAD_SCALE) * 0.5f;
	t.aabb.min.z = t.aabb.max.z * -1.0f;
	t.aabb.min.x = t.aabb.max.x * -1.0f;

	// Each chunk uses the same indices.
	// Surface indices. Generate 1 set of 6 per tile.
	for (u32 row = 0, i = 0; row < HF_CHUNK_QUAD_COUNT; row += 1) {
		for (u32 col = 0; col < HF_CHUNK_QUAD_COUNT; col += 1, i += 6) {
			u32 next_row = row + 1;
			u32 next_col = col + 1;
			u32 v0 = (row * HF_VERTEX_STRIDE) + col;
			u32 v1 = (row * HF_VERTEX_STRIDE) + next_col;
			u32 v2 = (next_row * HF_VERTEX_STRIDE) + col;
			u32 v3 = (next_row * HF_VERTEX_STRIDE) + next_col;

			t.indices[i + 0] = v2;
			t.indices[i + 1] = v1;
			t.indices[i + 2] = v0;
			t.indices[i + 3] = v3;
			t.indices[i + 4] = v1;
			t.indices[i + 5] = v2;
		}
	}

	// Acquire the shader.
	t.hf_terrain_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_HF_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
	KASSERT_DEBUG(t.hf_terrain_shader != KSHADER_INVALID);

	// Acquire the vertex/index buffers.
	struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));

	// Allocate index buffer space.
	u64 index_buffer_size = sizeof(u32) * HF_INDEX_COUNT;
	KASSERT(renderer_renderbuffer_allocate(renderer_system, index_buffer, index_buffer_size, &t.index_buffer_offset));

	// Allocate vertexchunk_base_x buffer space.
	u32 block_count = (asset->block_count_x * asset->block_count_z);
	t.vertex_count = (HF_VERTEX_STRIDE * HF_VERTEX_STRIDE) * HF_BLOCK_CHUNK_COUNT * block_count;
	t.vertices = KALLOC_TYPE_CARRAY(hf_vertex_3d, t.vertex_count);
	u64 vertex_buffer_size = sizeof(hf_vertex_3d) * t.vertex_count;
	KASSERT(renderer_renderbuffer_allocate(renderer_system, vertex_buffer, vertex_buffer_size, &t.base_vertex_buffer_offset));

	// Setup materials.
	for (u8 i = 0; i < HF_TERRAIN_MAX_MATERIALS; ++i) {
		t.material_names[i] = INVALID_KSTRING_ID;
		t.albedo_asset_names[i] = INVALID_KNAME;
		t.albedo_package_names[i] = INVALID_KNAME;
		t.normal_asset_names[i] = INVALID_KNAME;
		t.normal_package_names[i] = INVALID_KNAME;
		t.mra_asset_names[i] = INVALID_KNAME;
		t.mra_package_names[i] = INVALID_KNAME;
	}

	t.material_count = asset->material_count;
	for (u8 i = 0; i < t.material_count; ++i) {
		t.material_names[i] = kstring_id_create(asset->material_names[i]);
		t.albedo_asset_names[i] = kname_create(asset->material_map_names[i].albedo_str);
		t.normal_asset_names[i] = kname_create(asset->material_map_names[i].normal_str);
		t.mra_asset_names[i] = kname_create(asset->material_map_names[i].mra_str);
	}

	t.albedo_texture_array = texture_acquire_layered_sync(kname_create("hf_terrain_albedo_tex_array"), HF_TERRAIN_MAX_MATERIALS, t.albedo_asset_names, t.albedo_package_names);
	t.normal_texture_array = texture_acquire_layered_sync(kname_create("hf_terrain_normal_tex_array"), HF_TERRAIN_MAX_MATERIALS, t.normal_asset_names, t.normal_package_names);
	t.mra_texture_array = texture_acquire_layered_sync(kname_create("hf_terrain_mra_tex_array"), HF_TERRAIN_MAX_MATERIALS, t.mra_asset_names, t.mra_package_names);

	t.blocks = KALLOC_TYPE_CARRAY(hf_block, asset->block_count_x * asset->block_count_z);

	// Generate blocks.
	for (u8 z = 0; z < asset->block_count_z; ++z) {
		for (u8 x = 0; x < asset->block_count_x; ++x) {
			u32 index = (z * asset->block_count_z) + x;
			hf_block *block = &t.blocks[index];
			generate_block(&t, asset->vertices, &asset->blocks[index], block, x, z, asset->block_count_z);

			t.aabb.min = vec3_min(block->aabb.min, t.aabb.min);
			t.aabb.max = vec3_max(block->aabb.max, t.aabb.max);
		}
	}

	// Upload vertices
	KASSERT(renderer_renderbuffer_load_range(renderer_system, vertex_buffer, t.base_vertex_buffer_offset, vertex_buffer_size, t.vertices, false));

	// Upload indices
	KASSERT(renderer_renderbuffer_load_range(renderer_system, index_buffer, t.index_buffer_offset, index_buffer_size, t.indices, false));

	return t;
}

void hf_terrain_destroy (hf_terrain *t) {
	if (t) {
		u32 block_count = t->block_count_z * t->block_count_x;
		for (u32 b = 0; b < block_count; ++b) {
			hf_block *block = &t->blocks[b];

			texture_release(block->splatmap);
			kfree(block->splatmap_pixels);

			kshader_release_binding_set_instance(t->hf_terrain_shader, 1, block->shader_instance_id);

			/* for (u32 c = 0; c < 256; ++c) {
				hf_chunk* chunk = &block->chunks[c];
			} */
		}
		kfree(t->blocks);

		// Free material array textures.
		texture_release(t->albedo_texture_array);
		texture_release(t->normal_texture_array);
		texture_release(t->mra_texture_array);

		struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
		krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
		krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));

		// Free vertices
		u64 vertex_buffer_size = sizeof(hf_vertex_3d) * t->vertex_count;
		kfree(t->vertices);
		renderer_renderbuffer_free(renderer_system, vertex_buffer, vertex_buffer_size, t->base_vertex_buffer_offset);

		// Free indices
		u64 index_buffer_size = sizeof(u32) * HF_INDEX_COUNT;
		renderer_renderbuffer_free(renderer_system, index_buffer, index_buffer_size, t->index_buffer_offset);

		kzero_memory(t, sizeof(hf_terrain));
	}
}

void hf_terrain_get_render_data (const hf_terrain *t, frame_data *p_frame_data, hf_terrain_render_data *render_data) {
	KASSERT(t);

	if (t->blocks) {
		render_data->block_count = t->block_count_z * t->block_count_x;
		render_data->blocks = p_frame_data->allocator.allocate(sizeof(hf_terrain_block_render_data) * render_data->block_count);
		render_data->index_count = HF_INDEX_COUNT;
		render_data->index_buffer_offset = t->index_buffer_offset;

		// Array textures.
		render_data->albedo_texture_array = t->albedo_texture_array;
		render_data->normal_texture_array = t->normal_texture_array;
		render_data->mra_texture_array = t->mra_texture_array;

		for (u32 z = 0; z < t->block_count_z; ++z) {
			for (u32 x = 0; x < t->block_count_z; ++x) {
				// TODO: frustum cull the block
				u32 block_index = (t->block_count_z * z) + x;
				hf_block *block = &t->blocks[block_index];
				hf_terrain_block_render_data *brd = &render_data->blocks[block_index];

				brd->chunk_count = HF_BLOCK_CHUNK_COUNT;
				brd->chunks = p_frame_data->allocator.allocate(sizeof(hf_terrain_chunk_render_data) * brd->chunk_count);
				brd->splatmap = block->splatmap;
				brd->shader_instance_id = block->shader_instance_id;

				// TODO: frustum culling within this block
				for (u32 i = 0; i < brd->chunk_count; ++i) {
					hf_chunk *chunk = &block->chunks[i];
					hf_terrain_chunk_render_data *crd = &brd->chunks[i];

					// TODO: LOD
					crd->vertex_count = HF_CHUNK_VERTEX_COUNT;
					crd->vertex_buffer_offset = chunk->vertex_buffer_offset;

					crd->base_material_index = chunk->material_indices[0];
					for (u8 a = 0; a < HF_TERRAIN_CHUNK_MAX_MATERIALS - 1; ++a) {
						crd->material_indices.elements[i] = chunk->material_indices[i + 1];
					}
				}
			}
		}
	}
}

hf_block *hf_terrain_get_block_at (const hf_terrain *t, u8 x, u8 z) {
	if (!t || x >= t->block_count_x || z >= t->block_count_z) {
		return KNULL;
	}
	return &t->blocks[(z * t->block_count_z) + x];
}

hf_chunk *hf_terrain_block_get_chunk_at (hf_block *block, u8 x, u8 z) {
	if (!block) {
		return KNULL;
	}
	return &block->chunks[(z * HF_BLOCK_CHUNK_DIM) + x];
}

i32 hf_terrain_chunk_get_vert_index_at (const hf_chunk *chunk, u8 x, u8 z) {
	if (!chunk || x >= HF_VERTEX_STRIDE || z >= HF_VERTEX_STRIDE) {
		return -1;
	}

	return chunk->vertex_offset + (z * HF_VERTEX_STRIDE) + x;
}

void hf_terrain_recalculate_vertices (hf_terrain *t) {
	u16 block_count = t->block_count_z * t->block_count_x;
	for (u16 b = 0; b < block_count; ++b) {
		hf_block *block = &t->blocks[b];
		for (u16 c = 0; c < HF_BLOCK_CHUNK_COUNT; ++c) {
			u32 chunk_offset = block->chunks[c].vertex_offset;
			generate_normals(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);
			generate_tangents(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);
		}
	}
}

void hf_terrain_chunk_recalculate_vertices (hf_terrain *t, hf_chunk *chunk) {
	u32 chunk_offset = chunk->vertex_offset;
	generate_normals(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);
	generate_tangents(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);
	// AABB needs updating too
	for (u32 i = 0; i < HF_CHUNK_VERTEX_COUNT; ++i) {
		chunk->aabb.max.y = KMAX(chunk->aabb.max.y, t->vertices[chunk_offset + i].position.y);
		chunk->aabb.min.y = KMIN(chunk->aabb.min.y, t->vertices[chunk_offset + i].position.y);
	}
}

void hf_terrain_material_texture_set (hf_terrain *t, u8 material_index, hf_terrain_material_map map, ktexture texture) {
	ktexture array_texture = INVALID_KTEXTURE;
	switch (map) {
	case HF_TERRAIN_MATERIAL_MAP_ALBEDO:
		array_texture = t->albedo_texture_array;
		t->albedo_asset_names[material_index] = texture_name_get(texture);
		// TODO: package name
		break;
	case HF_TERRAIN_MATERIAL_MAP_NORMAL:
		array_texture = t->normal_texture_array;
		t->normal_asset_names[material_index] = texture_name_get(texture);
		// TODO: package name
		break;
	case HF_TERRAIN_MATERIAL_MAP_MRA:
		array_texture = t->mra_texture_array;
		t->mra_asset_names[material_index] = texture_name_get(texture);
		// TODO: package name
		break;
	}

	if (!texture_set_layer_data_from_texture(array_texture, material_index, texture)) {
		KERROR("%s - Failed to update terrain texture layer", __FUNCTION__);
	}
}
b8 hf_terrain_get_height_at (const hf_terrain *t, f32 world_x, f32 world_z, vec3 *out_pos, vec3 *out_normal) {
	if (!t || !aabb_contains_point((vec3){world_x, 0.0f, world_z}, t->aabb)) {
		return false;
	}

	ray r = ray_create((vec3){world_x, 99999.0, world_z}, vec3_down(), 999999.0, RAY_FLAG_NONE);

	// FIXME: Brute-forced like a dingus...
	u32 block_count = t->block_count_z * t->block_count_x;
	for (u32 b = 0; b < block_count; ++b) {
		hf_block *block = &t->blocks[b];

		f32 tmin = 0.0f;
		f32 tmaxi = r.max_distance;
		b8 block_hit = ray_intersects_aabb(block->aabb, r.origin, r.direction, r.max_distance, &tmin, &tmaxi);
		if (block_hit) {
			// Iterate the chunks and check for a aabb hit there.
			for (u32 c = 0; c < HF_BLOCK_CHUNK_COUNT; ++c) {
				tmin = 0.0f;
				tmaxi = r.max_distance;
				b8 chunk_hit = ray_intersects_aabb(block->chunks[c].aabb, r.origin, r.direction, r.max_distance, &tmin, &tmaxi);
				if (chunk_hit) {
					u64 vertex_offset = (block->chunks[c].vertex_buffer_offset - t->base_vertex_buffer_offset) / sizeof(hf_vertex_3d);
					triangle tri;
					vec3 hit_pos;
					vec3 hit_normal;
					b8 triangle_hit = ray_pick_triangle(&r, false, HF_CHUNK_VERTEX_COUNT, sizeof(hf_vertex_3d), t->vertices + vertex_offset, HF_INDEX_COUNT, t->indices, &tri, &hit_pos, &hit_normal);
					if (triangle_hit) {
						// Collision! Yay
						/* KTRACE("Terrain hit: pos=%V3.3, normal=%V3.3", &hit_pos, &hit_normal); */
						*out_pos = hit_pos;
						*out_normal = hit_normal;
						return true;
					}
				}
			}
		}
	}

	return false;
}

b8 hf_terrain_get_height_at_fast (const hf_terrain *t, f32 world_x, f32 world_z, f32 *out_height) {
	if (!t || !aabb_contains_point((vec3){world_x, 0.0f, world_z}, t->aabb)) {
		return false;
	}

	ray r = ray_create(vec3_create(world_x, t->aabb.max.y + 1.0f, world_z), vec3_down(), 1000.0f, RAY_FLAG_NONE);

	// FIXME: Brute-forced like a dingus...
	u32 block_count = t->block_count_z * t->block_count_x;
	for (u32 b = 0; b < block_count; ++b) {
		hf_block *block = &t->blocks[b];

		if (aabb_contains_point((vec3){world_x, block->aabb.min.y + 0.01f, world_z}, block->aabb)) {

			// Iterate the chunks and check for a aabb hit there.
			for (u32 c = 0; c < HF_BLOCK_CHUNK_COUNT; ++c) {
				if (aabb_contains_point((vec3){world_x, block->chunks[c].aabb.min.y + 0.01f, world_z}, block->chunks[c].aabb)) {
					u64 vertex_offset = (block->chunks[c].vertex_buffer_offset - t->base_vertex_buffer_offset) / sizeof(hf_vertex_3d);
					triangle tri;
					vec3 hit_pos;
					vec3 hit_normal;
					b8 triangle_hit = ray_pick_triangle(&r, false, HF_CHUNK_VERTEX_COUNT, sizeof(hf_vertex_3d), t->vertices + vertex_offset, HF_INDEX_COUNT, t->indices, &tri, &hit_pos, &hit_normal);
					if (triangle_hit) {
						// Collision! Yay
						/* KTRACE("Terrain hit: pos=%V3.3, normal=%V3.3", &hit_pos, &hit_normal); */
						*out_height = hit_pos.y;
						return true;
					}
				}
			}
		}
	}

	return false;
}

hf_vertex_3d *hf_terrain_chunk_get_closest_vertex (const hf_terrain *terrain, const hf_block *block, const hf_chunk *chunk, vec3 pos, u32 *out_x, u32 *out_z, i64 *out_index) {
	// Get closest vertex.
	// FIXME: optimize this.
	f32 shortest_dist = 9999990.0f;
	i64 index = -1;

	hf_vertex_3d *v = KNULL;

	u32 z = 0;
	u32 x = 0;
	for (u32 i = 0; i < HF_CHUNK_VERTEX_COUNT; ++i) {
		hf_vertex_3d cv = terrain->vertices[chunk->vertex_offset + i];
		f32 dist = vec3_distance_squared(vec3_from_vec4(cv.position), pos);
		if (dist < shortest_dist) {
			v = &terrain->vertices[chunk->vertex_offset + i];
			shortest_dist = dist;
			index = (block->index * HF_BLOCK_VERTEX_COUNT) + (chunk->index * HF_CHUNK_VERTEX_COUNT) + i;
			z = i / HF_VERTEX_STRIDE;
			x = i % HF_VERTEX_STRIDE;
		}
	}

	*out_index = index;
	*out_x = x;
	*out_z = z;

	return v;
}
