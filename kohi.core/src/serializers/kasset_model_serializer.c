#include "kasset_model_serializer.h"

#include "assets/kasset_types.h"
#include "containers/binary_string_table.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "strings/kstring.h"

typedef enum k3d_mesh_type {
	K3D_MESH_TYPE_STATIC = 0,  // maps to vertex_3d
	K3D_MESH_TYPE_SKINNED = 1, // maps to skinned_vertex_3d

	K3D_MESH_TYPE_MAX
} k3d_mesh_type;

typedef enum k3d_guard {
	K3D_GUARD_HEADER = 0x00000000,
	K3D_GUARD_SUBMESHES = 0x00000001,
	K3D_GUARD_BONES = 0x00000002,
	K3D_GUARD_NODES = 0x00000003,
	K3D_GUARD_ANIMATIONS = 0x00000004,
	K3D_GUARD_ANIM_CHANNELS = 0x00000005,
	K3D_GUARD_STRINGS = 0x00000006
} k3d_guard;

typedef struct k3d_header {
	// A magic number used to identify the binary block as a Kohi asset.
	u32 magic;
	// Indicates the asset type. Cast to kasset_type.
	u32 asset_type;
	// The asset type version, used for feature support checking for asset versions.
	u32 version;

	u32 exporter_type;
	u8 exporter_version;
	// The animated_mesh extents.
	extents_3d extents;
	// The animated_mesh center point.
	vec3 center;
	// The inverse global transform.
	mat4 inverse_global_transform;

	// The number of geometries in the animated_mesh.
	u16 submesh_count;
	// The number of bones.
	u16 bone_count;
	// The number of nodes.
	u16 node_count;
	// The number of animations.
	u16 animation_count;
	// The offset of the strings table in the file.
	u32 string_table_offset;
} k3d_header;

typedef struct k3d_submeshes {
	u16 *name_ids;
	u16 *material_name_ids;
	u32 *vertex_counts;
	u32 *index_counts;
	// Cast to k3d_mesh_type. Determines vertex format.
	u8 *mesh_types;
	vec3 *centers;
	extents_3d *extents;
	f32 *vertex_data_buffer;
	u32 *index_data_buffer;
} k3d_submeshes;

typedef struct k3d_bones {
	u16 *name_ids;
	mat4 *offset_matrices;
} k3d_bones;

typedef struct k3d_nodes {
	u16 *name_ids;
	u16 *parent_indices;
	mat4 *local_transforms;
} k3d_nodes;

typedef struct k3d_animations {
	u16 total_channel_count;
	u16 *name_ids;
	f32 *durations;
	f32 *ticks_per_seconds;
	u16 *channel_counts;
} k3d_animations;

typedef struct k3d_animation_channels {
	u16 *animation_ids;
	u16 *name_ids;
	u32 *pos_counts;
	u32 *pos_offsets;
	u32 *rot_counts;
	u32 *rot_offsets;
	u32 *scale_counts;
	u32 *scale_offsets;
	f32 *data_buffer;
} k3d_animation_channels;

static u64 write_binary (void *block, const void *source, u64 offset, u64 size);
static u64 write_binary_u32 (void *block, u32 value, u64 offset);
static u64 write_binary_u16 (void *block, u16 value, u64 offset);
static u64 write_binary_array (void *block, const void *source, u64 offset, u64 element_size, u32 count);

static u32 read_guard (const void *in_block, u64 *offset) {
	u32 guard = *(u32 *)(((u8 *)in_block) + *offset);
	*offset += sizeof(u32);
	return guard;
}

KAPI b8 kasset_model_deserialize (u64 size, const void *in_block, kasset_model *out_asset) {
	if (!size || !in_block || !out_asset) {
		KERROR("Cannot deserialize without a nonzero size, block of memory and an asset to write to.");
		return false;
	}

	u64 offset = 0;

	// Extract header info by casting the first bits of the block to the header.
	const k3d_header *header = (const k3d_header *)in_block;
	if (header->magic != ASSET_MAGIC) {
		KERROR("Memory is not a Kohi binary asset.");
		return false;
	}

	KASSERT(header->asset_type == KASSET_TYPE_MODEL);

	offset += sizeof(k3d_header);

	u32 guard = read_guard(in_block, &offset);

	k3d_submeshes submeshes = {0};
	if (header->submesh_count > 0) {
		// The guard should indicate the correct section.
		KASSERT(guard == K3D_GUARD_SUBMESHES);
		// Read the section.
		submeshes.name_ids = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(u16));

		submeshes.material_name_ids = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(u16));

		submeshes.vertex_counts = (u32 *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(u32));

		submeshes.index_counts = (u32 *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(u32));

		submeshes.mesh_types = (u8 *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(u8));

		submeshes.centers = (vec3 *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(vec3));

		submeshes.extents = (extents_3d *)(((u8 *)in_block) + offset);
		offset += (header->submesh_count * sizeof(extents_3d));

		u64 total_vert_buffer_size = 0;
		u64 total_index_buffer_size = 0;
		for (u32 i = 0; i < header->submesh_count; ++i) {
			KASSERT(submeshes.mesh_types[i] < K3D_MESH_TYPE_MAX);
			k3d_mesh_type mesh_type = (k3d_mesh_type)submeshes.mesh_types[i];
			switch (mesh_type) {
			default:
				total_vert_buffer_size += (sizeof(vertex_3d) * submeshes.vertex_counts[i]);
				break;
			case K3D_MESH_TYPE_SKINNED:
				total_vert_buffer_size += (sizeof(skinned_vertex_3d) * submeshes.vertex_counts[i]);
				break;
			}

			total_index_buffer_size += (sizeof(u32) * submeshes.index_counts[i]);
		}

		submeshes.vertex_data_buffer = (f32 *)(((u8 *)in_block) + offset);
		offset += total_vert_buffer_size;

		submeshes.index_data_buffer = (u32 *)(((u8 *)in_block) + offset);
		offset += total_index_buffer_size;

		// Read the next guard
		guard = read_guard(in_block, &offset);
	}

	k3d_bones bones = {0};
	if (header->bone_count > 0) {
		// The guard should indicate the correct section.
		KASSERT(guard == K3D_GUARD_BONES);
		// Read the section.
		bones.name_ids = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->bone_count * sizeof(u16));

		bones.offset_matrices = (mat4 *)(((u8 *)in_block) + offset);
		offset += (header->bone_count * sizeof(mat4));

		// Read the next guard
		guard = read_guard(in_block, &offset);
	}

	k3d_nodes nodes = {0};
	if (header->node_count > 0) {
		// The guard should indicate the correct section.
		KASSERT(guard == K3D_GUARD_NODES);
		// Read the section.
		nodes.name_ids = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->node_count * sizeof(u16));

		nodes.parent_indices = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->node_count * sizeof(u16));

		nodes.local_transforms = (mat4 *)(((u8 *)in_block) + offset);
		offset += (header->node_count * sizeof(mat4));

		// Read the next guard
		guard = read_guard(in_block, &offset);
	}

	k3d_animations animations = {0};
	k3d_animation_channels channels = {0};

	if (header->animation_count > 0) {
		// The guard should indicate the correct section.
		KASSERT(guard == K3D_GUARD_ANIMATIONS);
		// Read the section.
		animations.total_channel_count = *(u16 *)(((u8 *)in_block) + offset);
		offset += sizeof(u16);

		animations.name_ids = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->animation_count * sizeof(u16));

		animations.durations = (f32 *)(((u8 *)in_block) + offset);
		offset += (header->animation_count * sizeof(f32));

		animations.ticks_per_seconds = (f32 *)(((u8 *)in_block) + offset);
		offset += (header->animation_count * sizeof(f32));

		animations.channel_counts = (u16 *)(((u8 *)in_block) + offset);
		offset += (header->animation_count * sizeof(u16));

		// Read the next guard
		guard = read_guard(in_block, &offset);

		if (animations.total_channel_count > 0) {
			// Animation channels.
			// The guard should indicate the correct section.
			KASSERT(guard == K3D_GUARD_ANIM_CHANNELS);

			channels.animation_ids = (u16 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u16));

			channels.name_ids = (u16 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u16));

			channels.pos_counts = (u32 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u32));

			channels.pos_offsets = (u32 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u32));

			channels.rot_counts = (u32 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u32));

			channels.rot_offsets = (u32 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u32));

			channels.scale_counts = (u32 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u32));

			channels.scale_offsets = (u32 *)(((u8 *)in_block) + offset);
			offset += (animations.total_channel_count * sizeof(u32));

			u64 data_buffer_size = 0;
			for (u32 i = 0; i < animations.total_channel_count; ++i) {
				data_buffer_size += (sizeof(kasset_model_key_vec3) * channels.pos_counts[i]);
				data_buffer_size += (sizeof(kasset_model_key_quat) * channels.rot_counts[i]);
				data_buffer_size += (sizeof(kasset_model_key_vec3) * channels.scale_counts[i]);
			}
			channels.data_buffer = (f32 *)(((u8 *)in_block) + offset);
			offset += data_buffer_size;

			// Read the next guard
			guard = read_guard(in_block, &offset);
		}
	}

	// The guard should indicate the string section, which should always exist at this point in the file.
	KASSERT(guard == K3D_GUARD_STRINGS);

	KASSERT(offset == header->string_table_offset);

	// Extract the string table from the file.
	void *string_table_memory = (void *)(((u8 *)in_block) + header->string_table_offset);
	binary_string_table string_table = binary_string_table_from_block(string_table_memory);

	// Build out the asset structure(s)
	out_asset->submesh_count = header->submesh_count;
	if (out_asset->submesh_count) {
		out_asset->submeshes = KALLOC_TYPE_CARRAY(kasset_model_submesh_data, out_asset->submesh_count);

		u64 vertex_buffer_offset = 0;
		u64 index_buffer_offset = 0;

		for (u16 i = 0; i < out_asset->submesh_count; ++i) {

			// Ensure the mesh type is valid, and cast it to the enum
			KASSERT(submeshes.mesh_types[i] < K3D_MESH_TYPE_MAX);
			k3d_mesh_type mesh_type = (k3d_mesh_type)submeshes.mesh_types[i];

			kasset_model_submesh_data *submesh = &out_asset->submeshes[i];

			u64 vert_size = sizeof(vertex_3d);
			switch (mesh_type) {
			default:
				vert_size = sizeof(vertex_3d);
				submesh->type = KASSET_MODEL_MESH_TYPE_STATIC;
				break;
			case K3D_MESH_TYPE_SKINNED:
				vert_size = sizeof(skinned_vertex_3d);
				submesh->type = KASSET_MODEL_MESH_TYPE_SKINNED;
				break;
			}

			// Extract vertex/index data, etc.
			submesh->vertex_count = submeshes.vertex_counts[i];
			u64 submesh_vertex_buffer_size = vert_size * submeshes.vertex_counts[i];
			submesh->vertices = kallocate(submesh_vertex_buffer_size, MEMORY_TAG_BINARY_DATA);
			kcopy_memory(submesh->vertices, ((u8 *)submeshes.vertex_data_buffer) + vertex_buffer_offset, submesh_vertex_buffer_size);
			vertex_buffer_offset += submesh_vertex_buffer_size;

			submesh->index_count = submeshes.index_counts[i];
			u64 submesh_index_buffer_size = sizeof(u32) * submeshes.index_counts[i];
			submesh->indices = kallocate(submesh_index_buffer_size, MEMORY_TAG_BINARY_DATA);
			kcopy_memory(submesh->indices, ((u8 *)submeshes.index_data_buffer) + index_buffer_offset, submesh_index_buffer_size);
			index_buffer_offset += submesh_index_buffer_size;

			submesh->extents = submeshes.extents[i];
			submesh->center = submeshes.centers[i];

			if (submeshes.name_ids[i] != INVALID_ID_U16) {
				const char *name = binary_string_table_get(&string_table, submeshes.name_ids[i]);
				if (name) {
					submesh->name = kname_create(name);
					string_free(name);
				}
			}

			if (submeshes.material_name_ids[i] != INVALID_ID_U16) {
				const char *material_name = binary_string_table_get(&string_table, submeshes.material_name_ids[i]);
				if (material_name) {
					submesh->material_name = kname_create(material_name);
					string_free(material_name);
				}
			}
		}
	}

	out_asset->bone_count = header->bone_count;
	if (out_asset->bone_count) {
		out_asset->bones = KALLOC_TYPE_CARRAY(kasset_model_bone, out_asset->bone_count);
		for (u16 i = 0; i < out_asset->bone_count; ++i) {
			kasset_model_bone *bone = &out_asset->bones[i];
			bone->offset = bones.offset_matrices[i];
			bone->id = i;
			const char *name = binary_string_table_get(&string_table, bones.name_ids[i]);
			if (name) {
				bone->name = kname_create(name);
				string_free(name);
			}
		}
	}

	out_asset->node_count = header->node_count;
	if (out_asset->node_count) {
		out_asset->nodes = KALLOC_TYPE_CARRAY(kasset_model_node, out_asset->node_count);
		for (u16 i = 0; i < out_asset->node_count; ++i) {
			kasset_model_node *node = &out_asset->nodes[i];
			node->local_transform = nodes.local_transforms[i];
			node->parent_index = nodes.parent_indices[i];

			const char *name = binary_string_table_get(&string_table, nodes.name_ids[i]);
			if (name) {
				node->name = kname_create(name);
				string_free(name);
			}

			node->child_count = 0;
			for (u16 j = 0; j < out_asset->node_count; ++j) {
				if (nodes.parent_indices[j] == i) {
					node->child_count++;
				}
			}

			if (node->child_count > 0) {
				node->children = KALLOC_TYPE_CARRAY(u16, node->child_count);
				u16 idx = 0;
				for (u16 j = 0; j < out_asset->node_count; ++j) {
					if (nodes.parent_indices[j] == i) {
						node->children[idx] = j;
						idx++;
					}
				}
			}
		}
	}

	out_asset->animation_count = header->animation_count;
	if (out_asset->animation_count) {
		out_asset->animations = KALLOC_TYPE_CARRAY(kasset_model_animation, out_asset->animation_count);

		for (u16 i = 0; i < out_asset->animation_count; ++i) {
			kasset_model_animation *anim = &out_asset->animations[i];

			const char *name = binary_string_table_get(&string_table, animations.name_ids[i]);
			if (name) {
				anim->name = kname_create(name);
				string_free(name);
			}

			anim->channel_count = animations.channel_counts[i];
			anim->duration = animations.durations[i];
			anim->ticks_per_second = animations.ticks_per_seconds[i];

			anim->channels = KALLOC_TYPE_CARRAY(kasset_model_channel, anim->channel_count);

			// Iterate all channels and pick out the ones for this animation.
			for (u16 c = 0, cid = 0; c < animations.total_channel_count && cid < anim->channel_count; c++) {
				// This channel belongs to this animation.
				if (channels.animation_ids[c] == i) {
					kasset_model_channel *channel = &anim->channels[cid];

					const char *ch_name = binary_string_table_get(&string_table, channels.name_ids[c]);
					if (ch_name) {
						channel->name = kname_create(ch_name);
						string_free(ch_name);
					}

					channel->pos_count = channels.pos_counts[c];
					if (channel->pos_count) {
						channel->positions = KALLOC_TYPE_CARRAY(kasset_model_key_vec3, channel->pos_count);
						u64 size = sizeof(kasset_model_key_vec3) * channel->pos_count;
						kcopy_memory(channel->positions, ((u8 *)channels.data_buffer) + channels.pos_offsets[c], size);
					}

					channel->rot_count = channels.rot_counts[c];
					if (channel->rot_count) {
						channel->rotations = KALLOC_TYPE_CARRAY(kasset_model_key_quat, channel->rot_count);
						u64 size = sizeof(kasset_model_key_quat) * channel->rot_count;
						kcopy_memory(channel->rotations, ((u8 *)channels.data_buffer) + channels.rot_offsets[c], size);
					}

					channel->scale_count = channels.scale_counts[c];
					if (channel->scale_count) {
						channel->scales = KALLOC_TYPE_CARRAY(kasset_model_key_vec3, channel->scale_count);
						u64 size = sizeof(kasset_model_key_vec3) * channel->scale_count;
						kcopy_memory(channel->scales, ((u8 *)channels.data_buffer) + channels.scale_offsets[c], size);
					}

					cid++;
				}
			}
		}
	}

	out_asset->center = header->center;
	out_asset->extents = header->extents;
	out_asset->global_inverse_transform = header->inverse_global_transform;

	binary_string_table_destroy(&string_table);

	return true;
}

KAPI void *kasset_model_serialize (const kasset_model *asset, u32 exporter_type, u8 exporter_version, u64 *out_size) {
	KASSERT(out_size);

	if (!asset) {
		KERROR("Cannot serialize without an asset, ya dingus!");
		return 0;
	}

	// Create a binary string table for all strings, to be serialized at the end.
	binary_string_table string_table = binary_string_table_create();

	u64 total_block_size = 0;

	k3d_header header = {
		.magic = ASSET_MAGIC,
		.asset_type = KASSET_TYPE_MODEL,
		.version = KASSET_MODEL_CURRENT_VERSION,
		.exporter_type = exporter_type,
		.exporter_version = exporter_version,
		.extents = asset->extents,
		.center = asset->center,
		.inverse_global_transform = asset->global_inverse_transform,

		.submesh_count = asset->submesh_count,
		.bone_count = asset->bone_count,
		.node_count = asset->node_count,
		.animation_count = asset->animation_count,
		.string_table_offset = 0 // NOTE: This gets calculated at the very end.
	};
	total_block_size += sizeof(header);

	k3d_submeshes submeshes = {0};
	u32 total_submesh_vertex_buffer_size = 0;
	u32 total_submesh_index_buffer_size = 0;
	if (header.submesh_count) {
		KDEBUG("Submesh guard offset=%llu", total_block_size);
		total_block_size += sizeof(u32); // Submeshes begin guard

		// Submeshes
		submeshes = (k3d_submeshes){
			.name_ids = KALLOC_TYPE_CARRAY(u16, header.submesh_count),
			.material_name_ids = KALLOC_TYPE_CARRAY(u16, header.submesh_count),
			.vertex_counts = KALLOC_TYPE_CARRAY(u32, header.submesh_count),
			.index_counts = KALLOC_TYPE_CARRAY(u32, header.submesh_count),
			.mesh_types = KALLOC_TYPE_CARRAY(u8, header.submesh_count),
			.centers = KALLOC_TYPE_CARRAY(vec3, header.submesh_count),
			.extents = KALLOC_TYPE_CARRAY(extents_3d, header.submesh_count)};

		total_block_size += (sizeof(u16) * header.submesh_count);
		total_block_size += (sizeof(u16) * header.submesh_count);
		total_block_size += (sizeof(u32) * header.submesh_count);
		total_block_size += (sizeof(u32) * header.submesh_count);
		total_block_size += (sizeof(u8) * header.submesh_count);
		total_block_size += (sizeof(vec3) * header.submesh_count);
		total_block_size += (sizeof(extents_3d) * header.submesh_count);

		// Iterate once to get the total size of the buffer.
		for (u32 i = 0; i < header.submesh_count; ++i) {
			kasset_model_submesh_data *submesh = &asset->submeshes[i];
			u32 vert_size = sizeof(vertex_3d);
			switch (submesh->type) {
			default:
			case KASSET_MODEL_MESH_TYPE_STATIC:
				vert_size = sizeof(vertex_3d);
				break;
			case KASSET_MODEL_MESH_TYPE_SKINNED:
				vert_size = sizeof(skinned_vertex_3d);
				break;
			}
			total_submesh_vertex_buffer_size += (vert_size * submesh->vertex_count);
			total_submesh_index_buffer_size += (sizeof(u32) * submesh->index_count);
		}
		submeshes.vertex_data_buffer = kallocate(total_submesh_vertex_buffer_size, MEMORY_TAG_BINARY_DATA);
		submeshes.index_data_buffer = kallocate(total_submesh_index_buffer_size, MEMORY_TAG_BINARY_DATA);

		total_block_size += total_submesh_vertex_buffer_size;
		total_block_size += total_submesh_index_buffer_size;

		// Iterate again and actually fill out the data.
		u32 vert_offset = 0;
		u32 index_offset = 0;
		for (u32 i = 0; i < header.submesh_count; ++i) {
			kasset_model_submesh_data *submesh = &asset->submeshes[i];
			u32 vert_size = sizeof(vertex_3d);
			u8 mesh_type = 0;
			switch (submesh->type) {
			default:
			case KASSET_MODEL_MESH_TYPE_STATIC:
				vert_size = sizeof(vertex_3d);
				mesh_type = 0;
				break;
			case KASSET_MODEL_MESH_TYPE_SKINNED:
				vert_size = sizeof(skinned_vertex_3d);
				mesh_type = 1;
				break;
			}
			u32 vsize = (vert_size * submesh->vertex_count);
			u32 isize = (sizeof(u32) * submesh->index_count);
			kcopy_memory(((u8 *)submeshes.vertex_data_buffer) + vert_offset, submesh->vertices, vsize);
			kcopy_memory(((u8 *)submeshes.index_data_buffer) + index_offset, submesh->indices, isize);

			vert_offset += vsize;
			index_offset += isize;

			submeshes.vertex_counts[i] = submesh->vertex_count;
			submeshes.index_counts[i] = submesh->index_count;
			submeshes.centers[i] = submesh->center;
			submeshes.extents[i] = submesh->extents;
			submeshes.mesh_types[i] = mesh_type;

			const char *name = kname_string_get(submesh->name);
			submeshes.name_ids[i] = name ? binary_string_table_add(&string_table, name) : INVALID_ID_U16;

			const char *material_name = kname_string_get(submesh->material_name);
			submeshes.material_name_ids[i] = material_name ? binary_string_table_add(&string_table, material_name) : INVALID_ID_U16;
		}
	}

	// Bones
	k3d_bones bones = {0};
	if (header.bone_count) {
		KDEBUG("Bone guard offset=%llu", total_block_size);
		total_block_size += sizeof(u32); // Bones begin guard
		bones = (k3d_bones){
			.name_ids = KALLOC_TYPE_CARRAY(u16, header.bone_count),
			.offset_matrices = KALLOC_TYPE_CARRAY(mat4, header.bone_count)};
		for (u16 i = 0; i < header.bone_count; ++i) {
			const char *name = kname_string_get(asset->bones[i].name);
			bones.name_ids[i] = name ? binary_string_table_add(&string_table, name) : INVALID_ID_U16;

			bones.offset_matrices[i] = asset->bones[i].offset;
		}
		total_block_size += (sizeof(u16) * header.bone_count);
		total_block_size += (sizeof(mat4) * header.bone_count);
	}

	// Nodes
	k3d_nodes nodes = {0};
	if (header.node_count) {
		KDEBUG("Node guard offset=%llu", total_block_size);
		total_block_size += sizeof(u32); // Nodes begin guard
		nodes = (k3d_nodes){
			.name_ids = KALLOC_TYPE_CARRAY(u16, header.node_count),
			.local_transforms = KALLOC_TYPE_CARRAY(mat4, header.node_count),
			.parent_indices = KALLOC_TYPE_CARRAY(u16, header.node_count)};
		for (u16 i = 0; i < header.node_count; ++i) {
			const char *name = kname_string_get(asset->nodes[i].name);
			nodes.name_ids[i] = name ? binary_string_table_add(&string_table, name) : INVALID_ID_U16;

			nodes.local_transforms[i] = asset->nodes[i].local_transform;
			nodes.parent_indices[i] = asset->nodes[i].parent_index;
		}
		total_block_size += (sizeof(u16) * header.node_count);
		total_block_size += (sizeof(mat4) * header.node_count);
		total_block_size += (sizeof(u16) * header.node_count);
	}

	// Animations
	k3d_animations animations = {0};
	k3d_animation_channels channels = {0};
	u32 channel_buffer_size = 0;
	if (header.animation_count) {
		KDEBUG("Animation guard offset=%llu", total_block_size);
		total_block_size += sizeof(u32); // Animations begin guard
		animations = (k3d_animations){
			.name_ids = KALLOC_TYPE_CARRAY(u16, header.animation_count),
			.ticks_per_seconds = KALLOC_TYPE_CARRAY(f32, header.animation_count),
			.durations = KALLOC_TYPE_CARRAY(f32, header.animation_count),
			.channel_counts = KALLOC_TYPE_CARRAY(u16, header.animation_count),
			.total_channel_count = 0};
		for (u16 i = 0; i < header.animation_count; ++i) {
			animations.total_channel_count += asset->animations[i].channel_count;

			animations.channel_counts[i] = asset->animations[i].channel_count;
			animations.durations[i] = asset->animations[i].duration;
			animations.ticks_per_seconds[i] = asset->animations[i].ticks_per_second;

			const char *name = kname_string_get(asset->animations[i].name);
			animations.name_ids[i] = name ? binary_string_table_add(&string_table, name) : INVALID_ID_U16;
		}

		total_block_size += (sizeof(u16) * header.animation_count);
		total_block_size += (sizeof(f32) * header.animation_count);
		total_block_size += (sizeof(f32) * header.animation_count);
		total_block_size += (sizeof(u16) * header.animation_count);
		total_block_size += sizeof(u16);

		for (u16 i = 0; i < header.animation_count; ++i) {
			kasset_model_animation *anim = &asset->animations[i];
			for (u16 c = 0; c < animations.channel_counts[i]; c++) {
				kasset_model_channel *channel = &anim->channels[c];

				channel_buffer_size += (sizeof(kasset_model_key_vec3) * channel->pos_count);
				channel_buffer_size += (sizeof(kasset_model_key_vec3) * channel->scale_count);
				channel_buffer_size += (sizeof(kasset_model_key_quat) * channel->rot_count);
			}
		}

		// Animation Channels
		if (animations.total_channel_count) {
			KDEBUG("Animation Channels guard offset=%llu", total_block_size);
			total_block_size += sizeof(u32); // Animation channels begin guard
			channels = (k3d_animation_channels){
				.animation_ids = KALLOC_TYPE_CARRAY(u16, animations.total_channel_count),
				.name_ids = KALLOC_TYPE_CARRAY(u16, animations.total_channel_count),
				.pos_counts = KALLOC_TYPE_CARRAY(u32, animations.total_channel_count),
				.rot_counts = KALLOC_TYPE_CARRAY(u32, animations.total_channel_count),
				.scale_counts = KALLOC_TYPE_CARRAY(u32, animations.total_channel_count),
				.pos_offsets = KALLOC_TYPE_CARRAY(u32, animations.total_channel_count),
				.rot_offsets = KALLOC_TYPE_CARRAY(u32, animations.total_channel_count),
				.scale_offsets = KALLOC_TYPE_CARRAY(u32, animations.total_channel_count),
				.data_buffer = kallocate(channel_buffer_size, MEMORY_TAG_BINARY_DATA)};

			total_block_size += (sizeof(u16) * animations.total_channel_count);
			total_block_size += (sizeof(u16) * animations.total_channel_count);
			total_block_size += (sizeof(u32) * animations.total_channel_count);
			total_block_size += (sizeof(u32) * animations.total_channel_count);
			total_block_size += (sizeof(u32) * animations.total_channel_count);
			total_block_size += (sizeof(u32) * animations.total_channel_count);
			total_block_size += (sizeof(u32) * animations.total_channel_count);
			total_block_size += (sizeof(u32) * animations.total_channel_count);
			total_block_size += channel_buffer_size;

			u16 channel_id = 0;
			u32 channel_data_buffer_offset = 0;
			for (u16 i = 0; i < header.animation_count; ++i) {
				kasset_model_animation *anim = &asset->animations[i];
				for (u16 c = 0; c < animations.channel_counts[i]; c++) {
					kasset_model_channel *channel = &anim->channels[c];

					channels.animation_ids[channel_id] = i;

					const char *name = kname_string_get(channel->name);
					channels.name_ids[channel_id] = name ? binary_string_table_add(&string_table, name) : INVALID_ID_U16;

					channels.pos_counts[channel_id] = channel->pos_count;
					channels.rot_counts[channel_id] = channel->rot_count;
					channels.scale_counts[channel_id] = channel->scale_count;

					// NOTE: write position, rotation, then scale per channel
					u32 pos_size = sizeof(kasset_model_key_vec3) * channel->pos_count;
					u32 rot_size = sizeof(kasset_model_key_quat) * channel->rot_count;
					u32 scale_size = sizeof(kasset_model_key_vec3) * channel->scale_count;

					channels.pos_offsets[channel_id] = channel_data_buffer_offset;
					kcopy_memory(((u8 *)channels.data_buffer) + channel_data_buffer_offset, channel->positions, pos_size);
					channel_data_buffer_offset += pos_size;

					channels.rot_offsets[channel_id] = channel_data_buffer_offset;
					kcopy_memory(((u8 *)channels.data_buffer) + channel_data_buffer_offset, channel->rotations, rot_size);
					channel_data_buffer_offset += rot_size;

					channels.scale_offsets[channel_id] = channel_data_buffer_offset;
					kcopy_memory(((u8 *)channels.data_buffer) + channel_data_buffer_offset, channel->scales, scale_size);
					channel_data_buffer_offset += scale_size;

					channel_id++;
				}
			}
		}
	}

	// Strings
	KDEBUG("Strings guard offset=%llu", total_block_size);
	total_block_size += sizeof(u32); // strings table begin guard
	// Tell the header where the string table should begin.
	header.string_table_offset = total_block_size;
	u64 string_table_size = 0;
	void *string_table_serialized = binary_string_table_serialized(&string_table, &string_table_size);
	total_block_size += string_table_size;

	// ===========================
	// BEGIN WRITE TO BINARY BLOCK
	// ===========================

	// Allocate the data block and copy all data to it.
	void *block = kallocate(total_block_size, MEMORY_TAG_BINARY_DATA);
	*out_size = total_block_size;

	// Start with the header first.
	u64 offset = 0;
	offset = write_binary(block, &header, offset, sizeof(k3d_header));

	// Submeshes - Only write this if there are submeshes.
	if (header.submesh_count) {
		KDEBUG("->Submeshes guard offset=%llu", offset);
		offset = write_binary_u32(block, K3D_GUARD_SUBMESHES, offset);

		offset = write_binary_array(block, submeshes.name_ids, offset, sizeof(u16), header.submesh_count);
		offset = write_binary_array(block, submeshes.material_name_ids, offset, sizeof(u16), header.submesh_count);
		offset = write_binary_array(block, submeshes.vertex_counts, offset, sizeof(u32), header.submesh_count);
		offset = write_binary_array(block, submeshes.index_counts, offset, sizeof(u32), header.submesh_count);
		offset = write_binary_array(block, submeshes.mesh_types, offset, sizeof(u8), header.submesh_count);
		offset = write_binary_array(block, submeshes.centers, offset, sizeof(vec3), header.submesh_count);
		offset = write_binary_array(block, submeshes.extents, offset, sizeof(extents_3d), header.submesh_count);

		offset = write_binary(block, submeshes.vertex_data_buffer, offset, total_submesh_vertex_buffer_size);
		offset = write_binary(block, submeshes.index_data_buffer, offset, total_submesh_index_buffer_size);
	}

	if (header.bone_count) {
		KDEBUG("->Bones guard offset=%llu", offset);
		offset = write_binary_u32(block, K3D_GUARD_BONES, offset);

		offset = write_binary_array(block, bones.name_ids, offset, sizeof(u16), header.bone_count);
		offset = write_binary_array(block, bones.offset_matrices, offset, sizeof(mat4), header.bone_count);
	}

	if (header.node_count) {
		KDEBUG("->Nodes guard offset=%llu", offset);
		offset = write_binary_u32(block, K3D_GUARD_NODES, offset);

		offset = write_binary_array(block, nodes.name_ids, offset, sizeof(u16), header.node_count);
		offset = write_binary_array(block, nodes.parent_indices, offset, sizeof(u16), header.node_count);
		offset = write_binary_array(block, nodes.local_transforms, offset, sizeof(mat4), header.node_count);
	}

	if (header.animation_count) {
		KDEBUG("->Animation guard offset=%llu", offset);
		offset = write_binary_u32(block, K3D_GUARD_ANIMATIONS, offset);

		offset = write_binary_u16(block, animations.total_channel_count, offset);
		offset = write_binary_array(block, animations.name_ids, offset, sizeof(u16), header.animation_count);
		offset = write_binary_array(block, animations.durations, offset, sizeof(f32), header.animation_count);
		offset = write_binary_array(block, animations.ticks_per_seconds, offset, sizeof(f32), header.animation_count);
		offset = write_binary_array(block, animations.channel_counts, offset, sizeof(u16), header.animation_count);

		if (animations.total_channel_count) {
			KDEBUG("->Animation channels guard offset=%llu", offset);
			offset = write_binary_u32(block, K3D_GUARD_ANIM_CHANNELS, offset);
			offset = write_binary_array(block, channels.animation_ids, offset, sizeof(u16), animations.total_channel_count);
			offset = write_binary_array(block, channels.name_ids, offset, sizeof(u16), animations.total_channel_count);

			offset = write_binary_array(block, channels.pos_counts, offset, sizeof(u32), animations.total_channel_count);
			offset = write_binary_array(block, channels.pos_offsets, offset, sizeof(u32), animations.total_channel_count);
			offset = write_binary_array(block, channels.rot_counts, offset, sizeof(u32), animations.total_channel_count);
			offset = write_binary_array(block, channels.rot_offsets, offset, sizeof(u32), animations.total_channel_count);
			offset = write_binary_array(block, channels.scale_counts, offset, sizeof(u32), animations.total_channel_count);
			offset = write_binary_array(block, channels.scale_offsets, offset, sizeof(u32), animations.total_channel_count);

			offset = write_binary(block, channels.data_buffer, offset, channel_buffer_size);
		}
	}

	// Strings - always write the guard.
	KDEBUG("->String guard offset=%llu", offset);
	offset = write_binary_u32(block, K3D_GUARD_STRINGS, offset);

	// Write out the serialized string table.
	offset = write_binary(block, string_table_serialized, offset, string_table_size);

	// Cleanup submeshes
	kfree(submeshes.name_ids);
	kfree(submeshes.material_name_ids);
	kfree(submeshes.vertex_counts);
	kfree(submeshes.index_counts);
	kfree(submeshes.mesh_types);
	kfree(submeshes.centers);
	kfree(submeshes.extents);
	kfree(submeshes.vertex_data_buffer);
	kfree(submeshes.index_data_buffer);

	// cleanup bones
	kfree(bones.name_ids);
	kfree(bones.offset_matrices);

	// cleanup nodes
	kfree(nodes.name_ids);
	kfree(nodes.parent_indices);
	kfree(nodes.local_transforms);

	// cleanup animations
	kfree(animations.name_ids);
	kfree(animations.durations);
	kfree(animations.ticks_per_seconds);
	kfree(animations.channel_counts);

	// cleanup animation channels
	kfree(channels.animation_ids);
	kfree(channels.name_ids);
	kfree(channels.pos_counts);
	kfree(channels.pos_offsets);
	kfree(channels.rot_counts);
	kfree(channels.rot_offsets);
	kfree(channels.scale_counts);
	kfree(channels.scale_offsets);
	if (channels.data_buffer) {
		kfree(channels.data_buffer);
	}

	// Cleanup strings binary table.
	binary_string_table_destroy(&string_table);

	// Return the serialized block of memory.
	return block;
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

static u64 write_binary_array (void *block, const void *source, u64 offset, u64 element_size, u32 count) {
	return write_binary(block, source, offset, element_size * count);
}
