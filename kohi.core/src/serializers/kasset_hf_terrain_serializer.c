#include "kasset_hf_terrain_serializer.h"

#include "assets/kasset_types.h"
#include "containers/binary_string_table.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"

static u64 read_binary (void *dest, const void *source, u64 offset, u64 size);
static u64 read_binary_u32 (u32 *dest, const void *source, u64 offset);
static u64 read_binary_u16 (u16 *dest, const void *source, u64 offset);
static u64 read_binary_u8 (u8 *dest, const void *source, u64 offset);
static u64 read_binary_array (void *dest, const void *source, u64 offset, u64 element_size, u32 count);
static u64 write_binary (void *block, const void *source, u64 offset, u64 size);
static u64 write_binary_u32 (void *block, u32 value, u64 offset);
static u64 write_binary_u16 (void *block, u16 value, u64 offset);
static u64 write_binary_u8 (void *block, u8 value, u64 offset);
static u64 write_binary_array (void *block, const void *source, u64 offset, u64 element_size, u32 count);

KAPI b8 kasset_hf_terrain_deserialize (u64 size, const void *in_block, kasset_hf_terrain *out_asset) {
	if (!size || !in_block || !out_asset) {
		KERROR("Cannot deserialize without a nonzero size, block of memory and an asset to write to.");
		return false;
	}

	u64 offset = 0;

	offset = read_binary_u16(&out_asset->version, in_block, offset);
	offset = read_binary_u16(&out_asset->block_count_x, in_block, offset);
	offset = read_binary_u16(&out_asset->block_count_z, in_block, offset);

	u32 block_count = out_asset->block_count_x * out_asset->block_count_z;
	out_asset->blocks = KALLOC_TYPE_CARRAY(kasset_hf_terrain_block, block_count);
	offset = read_binary(out_asset->blocks, in_block, offset, sizeof(kasset_hf_terrain_block) * block_count);

	offset = read_binary_u32(&out_asset->vertex_count, in_block, offset);
	out_asset->vertices = KALLOC_TYPE_CARRAY(kasset_hf_terrain_vertex, out_asset->vertex_count);
	offset = read_binary(out_asset->vertices, in_block, offset, sizeof(kasset_hf_terrain_vertex) * out_asset->vertex_count);

	// Material count
	offset = read_binary_u8(&out_asset->material_count, in_block, offset);
	out_asset->materials = KALLOC_TYPE_CARRAY(kasset_hf_terrain_material, out_asset->material_count);
	offset = read_binary(out_asset->materials, in_block, offset, sizeof(kasset_hf_terrain_material) * out_asset->material_count);

	binary_string_table string_table = binary_string_table_from_block((((u8 *)in_block) + offset));

	out_asset->material_names = KALLOC_TYPE_CARRAY(const char *, out_asset->material_count);
	out_asset->material_map_names = KALLOC_TYPE_CARRAY(kasset_hf_terrain_material_map_names, out_asset->material_count);
	for (u8 i = 0; i < out_asset->material_count; ++i) {
		kasset_hf_terrain_material *m = &out_asset->materials[i];
		kasset_hf_terrain_material_map_names *n = &out_asset->material_map_names[i];

		n->albedo_str = binary_string_table_get(&string_table, m->albedo_str_index);
		n->normal_str = binary_string_table_get(&string_table, m->normal_str_index);
		n->mra_str = binary_string_table_get(&string_table, m->mra_str_index);

		out_asset->material_names[i] = binary_string_table_get(&string_table, m->name_str_index);
	}

	binary_string_table_destroy(&string_table);

	return true;
}

KAPI void *kasset_hf_terrain_serialize (const kasset_hf_terrain *asset, u64 *out_size) {
	KASSERT(out_size);

	if (!asset) {
		KERROR("Cannot serialize without an asset, ya dingus!");
		return 0;
	}

	// Create a binary string table for all strings, to be serialized at the end.
	binary_string_table string_table = binary_string_table_create();

	u64 total_block_size = 0;

	total_block_size += sizeof(u16);
	total_block_size += sizeof(u16);
	total_block_size += sizeof(u16);

	u16 block_count = asset->block_count_x * asset->block_count_z;
	total_block_size += sizeof(kasset_hf_terrain_block) * block_count;

	total_block_size += sizeof(u32);
	total_block_size += sizeof(kasset_hf_terrain_vertex) * asset->vertex_count;

	total_block_size += sizeof(u8);
	total_block_size += sizeof(kasset_hf_terrain_material) * asset->material_count;

	for (u8 i = 0; i < asset->material_count; ++i) {
		kasset_hf_terrain_material *m = &asset->materials[i];
		kasset_hf_terrain_material_map_names *mn = &asset->material_map_names[i];

		m->name_str_index = binary_string_table_add(&string_table, asset->material_names[i]);
		m->albedo_str_index = binary_string_table_add(&string_table, mn->albedo_str);
		m->normal_str_index = binary_string_table_add(&string_table, mn->normal_str);
		m->mra_str_index = binary_string_table_add(&string_table, mn->mra_str);
	}

	// Tell the header where the string table should begin.
	u64 string_table_size = 0;
	void *string_table_serialized = binary_string_table_serialized(&string_table, &string_table_size);
	total_block_size += string_table_size;

	// ===========================
	// BEGIN WRITE TO BINARY BLOCK
	// ===========================

	// Allocate the data block and copy all data to it.
	void *block = kallocate(total_block_size, MEMORY_TAG_BINARY_DATA);
	*out_size = total_block_size;

	u64 offset = 0;
	offset = write_binary_u16(block, asset->version, offset);
	offset = write_binary_u16(block, asset->block_count_x, offset);
	offset = write_binary_u16(block, asset->block_count_z, offset);

	offset = write_binary(block, asset->blocks, offset, sizeof(kasset_hf_terrain_block) * block_count);

	offset = write_binary_u32(block, asset->vertex_count, offset);
	offset = write_binary(block, asset->vertices, offset, sizeof(kasset_hf_terrain_vertex) * asset->vertex_count);

	offset = write_binary_u8(block, asset->material_count, offset);
	offset = write_binary(block, asset->materials, offset, sizeof(kasset_hf_terrain_material) * asset->material_count);

	// Write out the serialized string table.
	offset = write_binary(block, string_table_serialized, offset, string_table_size);

	// Cleanup strings binary table.
	binary_string_table_destroy(&string_table);

	// Return the serialized block of memory.
	return block;
}

static u64 read_binary (void *dest, const void *source, u64 offset, u64 size) {
	kcopy_memory(dest, (void *)((u64)source + offset), size);
	return offset + size;
}

static u64 read_binary_u32 (u32 *dest, const void *source, u64 offset) {
	return read_binary(dest, source, offset, sizeof(u32));
}

static u64 read_binary_u16 (u16 *dest, const void *source, u64 offset) {
	return read_binary(dest, source, offset, sizeof(u16));
}

static u64 read_binary_u8 (u8 *dest, const void *source, u64 offset) {
	return read_binary(dest, source, offset, sizeof(u8));
}

static u64 read_binary_array (void *dest, const void *source, u64 offset, u64 element_size, u32 count) {
	return read_binary(dest, source, offset, element_size * count);
}

static u64 write_binary (void *block, const void *source, u64 offset, u64 size) {
	kcopy_memory((void *)((u8 *)block + offset), source, size);
	return offset + size;
}

static u64 write_binary_u32 (void *block, u32 value, u64 offset) {
	return write_binary(block, &value, offset, sizeof(u32));
}

static u64 write_binary_u16 (void *block, u16 value, u64 offset) {
	return write_binary(block, &value, offset, sizeof(u16));
}

static u64 write_binary_u8 (void *block, u8 value, u64 offset) {
	return write_binary(block, &value, offset, sizeof(u8));
}

static u64 write_binary_array (void *block, const void *source, u64 offset, u64 element_size, u32 count) {
	return write_binary(block, source, offset, element_size * count);
}
