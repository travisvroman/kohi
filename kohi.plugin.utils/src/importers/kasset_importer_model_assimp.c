#include "kasset_importer_model_assimp.h"

#include <assimp/cimport.h>
#include <assimp/color4.h>
#include <assimp/defs.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>
#include <assimp/vector3.h>

#include "assets/kasset_types.h"
#include "assimp/anim.h"
#include "assimp/matrix4x4.h"

#include "containers/darray.h"
#include "core_render_types.h"
#include "debug/kassert.h"

#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"

#include "platform/filesystem.h"
#include "serializers/kasset_material_serializer.h"
#include "serializers/kasset_model_serializer.h"

#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kmodel_system.h"

static void skinned_vertex_3d_defaults (skinned_vertex_3d *vert);
static void get_material_texture_data_by_type (kname package_name, const struct aiMaterial *material, enum aiTextureType texture_type, kasset_material *new_material, kmaterial_texture_input_config *input);
static mat4 mat4_from_ai (const struct aiMatrix4x4 *source);
static b8 materials_from_assimp (const struct aiScene *scene, kname package_name, const char *output_directory, b8 force_pbr);
static const struct aiScene *assimp_open_file (const char *source_path);
static b8 anim_asset_from_assimp (const struct aiScene *scene, kname package_name, kasset_model *out_asset);
static void anim_asset_destroy (kasset_model *asset);

b8 kasset_model_assimp_import (const char *source_path, const char *target_path, const char *material_target_dir, const char *package_name) {
	kasset_model new_asset = {0};

	const struct aiScene *scene = assimp_open_file(source_path);

	kname pkg_name = kname_create(package_name);

	// Import mesh, animations, bones, etc.
	if (!anim_asset_from_assimp(scene, pkg_name, &new_asset)) {
		KERROR("Failed to import assimp asset '%s'.", source_path);
		return false;
	}

	b8 success = false;

	// Serialize animation asset.
	u64 serialized_size = 0;
	void *serialized_data = kasset_model_serialize(&new_asset, KASSET_EXPORTER_TYPE_KOHI_IMPORTER, KASSET_EXPORTER_TYPE_KOHI_IMPORTER_VERSION, &serialized_size);
	if (!serialized_size || !serialized_data) {
		KERROR("Failed to serialize animated mesh asset '%s'.", target_path);
		goto ai_import_cleanup;
	}

	// Write out .k3d file.
	if (!filesystem_write_entire_binary_file(target_path, serialized_size, serialized_data)) {
		KWARN("Failed to write .k3d file '%s'. See logs for details.", target_path);
		goto ai_import_cleanup;
	}

	// Import materials
	b8 force_pbr = true; // NOTE: for now, forcing all materials to import as PBR.
	if (!materials_from_assimp(scene, pkg_name, material_target_dir, force_pbr)) {
		KERROR("Failed to import materials from assimp asset '%s'.", source_path);
		goto ai_import_cleanup;
	}

	KINFO("Import complete.");
ai_import_cleanup:

	// Release all assimp resources.
	aiReleaseImport(scene);

	return success;
}

static void skinned_vertex_3d_defaults (skinned_vertex_3d *vert) {
	for (u8 i = 0; i < 4; ++i) {
		vert->bone_ids.elements[i] = -1;
		vert->weights.elements[i] = 0.0f;
	}
}

static void get_material_texture_data_by_type (kname package_name, const struct aiMaterial *material, enum aiTextureType texture_type, kasset_material *new_material, kmaterial_texture_input_config *input) {
	if (aiGetMaterialTextureCount(material, texture_type)) {
		const u32 index = 0; // NOTE: Only get the first one.
		struct aiString path;
		enum aiTextureMapping mapping;
		u32 uvindex;
		ai_real blend;
		enum aiTextureOp op;
		enum aiTextureMapMode mapmode;
		u32 flags;
		enum aiReturn result = aiGetMaterialTexture(material, texture_type, index, &path, &mapping, &uvindex, &blend, &op, &mapmode, &flags);
		if (result != aiReturn_SUCCESS) {
			KWARN("Failed reading base colour texture.");
		} else {
			const char *asset_name = string_filename_no_extension_from_path(path.data);
			input->resource_name = kname_create(asset_name);
			string_free(asset_name);
			input->package_name = package_name;

			texture_repeat repeat = TEXTURE_REPEAT_REPEAT;
			switch (mapmode) {
			case aiTextureMapMode_Wrap:
				repeat = TEXTURE_REPEAT_REPEAT;
				break;
			case aiTextureMapMode_Clamp:
				repeat = TEXTURE_REPEAT_CLAMP_TO_EDGE;
				break;
			case aiTextureMapMode_Mirror:
				repeat = TEXTURE_REPEAT_MIRRORED_REPEAT;
				break;
			default:
			case aiTextureMapMode_Decal:
				KWARN("Unsupported texture map mode found, defaulting to repeat.");
				break;
			}

			input->sampler.repeat_u = input->sampler.repeat_v = input->sampler.repeat_w = repeat;
			// NOTE: Since there is no way to obtain this, all maps will use linear min/mag.
			input->sampler.filter_min = input->sampler.filter_mag = TEXTURE_FILTER_MODE_LINEAR;
			// NOTE: Don't name the sampler here. Properties can be analyzed by the engine and a default sampler can be chosen based on it.
			input->sampler.name = INVALID_KNAME;
		}
	}
}

static mat4 mat4_from_ai (const struct aiMatrix4x4 *source) {
	// NOTE: Kohi expects column-major, and assimp uses row-major.
	// Therefore, transpose the matrix here before returning.
	mat4 m = {0};
	m.data[0] = source->a1;
	m.data[1] = source->b1;
	m.data[2] = source->c1;
	m.data[3] = source->d1;
	m.data[4] = source->a2;
	m.data[5] = source->b2;
	m.data[6] = source->c2;
	m.data[7] = source->d2;
	m.data[8] = source->a3;
	m.data[9] = source->b3;
	m.data[10] = source->c3;
	m.data[11] = source->d3;
	m.data[12] = source->a4;
	m.data[13] = source->b4;
	m.data[14] = source->c4;
	m.data[15] = source->d4;

	return m;
}

// Import all materials from an assimp scene.
static b8 materials_from_assimp (const struct aiScene *scene, kname package_name, const char *output_directory, b8 force_pbr) {

	for (u32 i = 0; i < scene->mNumMaterials; ++i) {
		struct aiMaterial *material = scene->mMaterials[i];
		enum aiReturn result;

		kasset_material new_material = {0};

		// Base properties
		struct aiString ai_name;
		aiGetMaterialString(material, AI_MATKEY_NAME, &ai_name);
		new_material.name = kname_create(ai_name.data);

		i32 double_sided = 0;
		aiGetMaterialInteger(material, AI_MATKEY_TWOSIDED, &double_sided);
		new_material.double_sided = (b8)double_sided;

		// NOTE: These properties are just assumed, and can be adjusted post-import.
		new_material.recieves_shadow = true;
		new_material.casts_shadow = true;

		// FIXME: use opacity or one of the transparency matkeys?

		// Imported materials are just treated as standard materials for now.
		new_material.type = KMATERIAL_TYPE_STANDARD;

		// Extract the shading model. Use this to determine what maps and properties to extract.
		i32 shading_model_int = 0;
		new_material.model = KMATERIAL_MODEL_PBR;
		if (!force_pbr) {
			result = aiGetMaterialInteger(material, AI_MATKEY_SHADING_MODEL, &shading_model_int);
			if (result == aiReturn_SUCCESS) {
				switch (((enum aiShadingMode)shading_model_int)) {
				case aiShadingMode_PBR_BRDF:
				case aiShadingMode_CookTorrance:
					new_material.model = KMATERIAL_MODEL_PBR;
					break;
				case aiShadingMode_Phong:
				case aiShadingMode_Blinn:
					new_material.model = KMATERIAL_MODEL_PHONG;
					break;
				case aiShadingMode_NoShading:
					new_material.model = KMATERIAL_MODEL_UNLIT;
					break;
				case aiShadingMode_Gouraud:
				case aiShadingMode_Flat:
				case aiShadingMode_Toon:
				case aiShadingMode_OrenNayar:
				case aiShadingMode_Minnaert:
				case aiShadingMode_Fresnel:
				default:
					KWARN("Shading model not supported, defaulting to PBR.");
					new_material.model = KMATERIAL_MODEL_PBR;
					break;
				}
			}
		}

		switch (new_material.model) {
		default:
		case KMATERIAL_MODEL_PBR:
			get_material_texture_data_by_type(package_name, material, aiTextureType_BASE_COLOR, &new_material, &new_material.base_colour_map);
			if (!new_material.base_colour_map.resource_name) {
				// Fall back to diffuse if basecolour isn't defined.
				get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE, &new_material, &new_material.base_colour_map);
			}

			get_material_texture_data_by_type(package_name, material, aiTextureType_NORMALS, &new_material, &new_material.normal_map);
			if (!new_material.normal_map.resource_name) {
				// Fall back to displacement if normals aren't defined.
				get_material_texture_data_by_type(package_name, material, aiTextureType_DISPLACEMENT, &new_material, &new_material.normal_map);
			}
			new_material.normal_enabled = new_material.normal_map.resource_name != INVALID_KNAME;

			get_material_texture_data_by_type(package_name, material, aiTextureType_METALNESS, &new_material, &new_material.metallic_map);
			get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE_ROUGHNESS, &new_material, &new_material.roughness_map);
			get_material_texture_data_by_type(package_name, material, aiTextureType_AMBIENT_OCCLUSION, &new_material, &new_material.ambient_occlusion_map);
			get_material_texture_data_by_type(package_name, material, aiTextureType_METALNESS, &new_material, &new_material.mra_map);
			get_material_texture_data_by_type(package_name, material, aiTextureType_EMISSIVE, &new_material, &new_material.emissive_map);
			new_material.emissive_enabled = new_material.emissive_map.resource_name != INVALID_KNAME;
			break;
		case KMATERIAL_MODEL_UNLIT: {
			get_material_texture_data_by_type(package_name, material, aiTextureType_BASE_COLOR, &new_material, &new_material.base_colour_map);
			if (!new_material.base_colour_map.resource_name) {
				get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE, &new_material, &new_material.base_colour_map);
			}

			// Also get diffuse colour, which might be defined.
			struct aiColor4D diffuse_c4d;
			new_material.base_colour = vec4_one();
			result = aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse_c4d);
			if (result == aiReturn_SUCCESS) {
				new_material.base_colour = (colour4){diffuse_c4d.r, diffuse_c4d.g, diffuse_c4d.b, diffuse_c4d.a};
			}
		} break;
		case KMATERIAL_MODEL_PHONG: {
			get_material_texture_data_by_type(package_name, material, aiTextureType_BASE_COLOR, &new_material, &new_material.base_colour_map);
			if (!new_material.base_colour_map.resource_name) {
				// Fall back to diffuse if basecolour isn't defined.
				get_material_texture_data_by_type(package_name, material, aiTextureType_DIFFUSE, &new_material, &new_material.base_colour_map);
			}

			get_material_texture_data_by_type(package_name, material, aiTextureType_NORMALS, &new_material, &new_material.normal_map);
			if (!new_material.normal_map.resource_name) {
				// Fall back to displacement if normals aren't defined.
				get_material_texture_data_by_type(package_name, material, aiTextureType_DISPLACEMENT, &new_material, &new_material.normal_map);
			}
			new_material.normal_enabled = new_material.normal_map.resource_name != INVALID_KNAME;

			get_material_texture_data_by_type(package_name, material, aiTextureType_SPECULAR, &new_material, &new_material.specular_colour_map);

			get_material_texture_data_by_type(package_name, material, aiTextureType_EMISSIVE, &new_material, &new_material.emissive_map);
			new_material.emissive_enabled = new_material.emissive_map.resource_name != INVALID_KNAME;

			// Phong-specific properties.

			/* // TODO: Phong Shininess
			ai_real shininess = 0.0f;
			result = aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &shininess);
			if (result == aiReturn_SUCCESS) {
				new_material.shininess = (f32)shininess;
			} */

			struct aiColor4D ambient_c4d;
			result = aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT, &ambient_c4d);
			if (result == aiReturn_SUCCESS) {
				new_material.base_colour = (colour4){ambient_c4d.r, ambient_c4d.g, ambient_c4d.b, ambient_c4d.a};
			} else {
				new_material.base_colour = vec4_zero();
			}

			struct aiColor4D diffuse_c4d;
			colour4 diffuse = vec4_one();
			result = aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse_c4d);
			if (result == aiReturn_SUCCESS) {
				diffuse = (colour4){diffuse_c4d.r, diffuse_c4d.g, diffuse_c4d.b, diffuse_c4d.a};
			}

			// For Phong, base colour is ambient + diffuse.
			new_material.base_colour = vec4_normalized(vec4_add(new_material.base_colour, diffuse));

			struct aiColor4D specular_c4d;
			result = aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &specular_c4d);
			if (result == aiReturn_SUCCESS) {
				new_material.specular_colour = (colour4){specular_c4d.r, specular_c4d.g, specular_c4d.b, specular_c4d.a};
			} else {
				new_material.specular_colour = vec4_zero();
			}
		} break;
		}

		// Serialize the material.
		const char *serialized_text = kasset_material_serialize(&new_material);
		if (!serialized_text) {
			KWARN("Failed to serialize material '%s'. See logs for details.", kname_string_get(new_material.name));
			string_free(serialized_text);
			continue;
		}

		// Write out kmt file.
		const char *out_path = string_format("%s/%s.%s", output_directory, kname_string_get(new_material.name), "kmt");
		if (!filesystem_write_entire_text_file(out_path, serialized_text)) {
			KERROR("Failed to write serialized material to disk. See logs for details.");
		}
		string_free(out_path);
		string_free(serialized_text);
	}

	return true;
}

static const struct aiScene *assimp_open_file (const char *source_path) {
	struct aiPropertyStore *store = aiCreatePropertyStore();
	aiSetImportPropertyFloat(store, AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, 60.0f); // less is sharper, more is smoother, 0-180 degrees.
	const struct aiScene *scene = aiImportFileExWithProperties(
		source_path,
		// aiProcess_GenNormals |
		aiProcess_Triangulate |
			aiProcess_GenSmoothNormals |
			aiProcess_JoinIdenticalVertices |
			aiProcess_LimitBoneWeights |
			aiProcess_CalcTangentSpace,
		KNULL, // filesystem callbacks
		store);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		KERROR("Error importing via assimp: %s", aiGetErrorString());
		return 0;
	}

	return scene;
}

typedef struct vertex_weight_accumulator {
	i32 bone_ids[4];
	f32 weights[4];
} vertex_weight_accumulator;

static void add_bone_weight (vertex_weight_accumulator *a, i32 bone_id, f32 weight) {
	for (u8 i = 0; i < 4; ++i) {
		if (a->weights[i] == 0.0f) {
			a->bone_ids[i] = bone_id;
			a->weights[i] = weight;
			return;
		}
	}

	// If full, replace the smallest weight.
	u8 smallest = 0;
	for (u8 i = 1; i < 4; ++i) {
		if (a->weights[i] < a->weights[smallest]) {
			smallest = i;
		}
	}
	if (weight > a->weights[smallest]) {
		a->bone_ids[smallest] = bone_id;
		a->weights[smallest] = weight;
	}
}

static b8 anim_asset_from_assimp (const struct aiScene *scene, kname package_name, kasset_model *out_asset) {
	KASSERT(scene);

	kzero_memory(out_asset, sizeof(kasset_model));
	out_asset->global_inverse_transform = mat4_from_ai(&scene->mRootNode->mTransformation);
	out_asset->global_inverse_transform = mat4_inverse(out_asset->global_inverse_transform);

	// Get all unique bones across all meshes.
	kasset_model_bone bones[KANIMATION_MAX_BONES] = {0};
	u32 bone_count = 0;
	for (u32 m = 0; m < scene->mNumMeshes; ++m) {
		struct aiMesh *mesh = scene->mMeshes[m];
		// Extract the bones from it.
		for (u32 b = 0; b < mesh->mNumBones; ++b) {
			struct aiBone *ai_bone = mesh->mBones[b];
			kasset_model_bone *bone = &bones[bone_count];
			kname ai_bone_name = kname_create(ai_bone->mName.data);

			b8 found = false;
			for (u32 i = 0; i < bone_count; ++i) {
				if (bones[i].name == ai_bone_name) {
					found = true;
					break;
				}
			}
			if (found) {
				KTRACE("Bone named '%s' already exists. Skipping.", ai_bone->mName.data);
				// Bone already exists, skip it.
				continue;
			}
			KASSERT(bone_count < KANIMATION_MAX_BONES);
			bone->name = ai_bone_name;
			bone->offset = mat4_from_ai(&ai_bone->mOffsetMatrix);
			bone->id = bone_count;

			bone_count++;
		}
	}

	out_asset->bone_count = bone_count;
	if (bone_count) {
		out_asset->bones = KALLOC_TYPE_CARRAY(kasset_model_bone, bone_count);
		KCOPY_TYPE_CARRAY(out_asset->bones, bones, kasset_model_bone, bone_count);
	}
	// Flatten the node structure into a single array and reference by index instead.
	kasset_model_node *nodes = darray_create(kasset_model_node);

	typedef struct node_map_entry {
		const struct aiNode *node;
		u16 index;
	} node_map_entry;

	node_map_entry *node_map = darray_create(node_map_entry);

	// Push the root first
	const struct aiNode *root = scene->mRootNode;
	const struct aiNode *stack_nodes[1024];
	u32 stack_top = 0;
	stack_nodes[stack_top++] = root;
	while (stack_top) {
		const struct aiNode *current = stack_nodes[--stack_top];

		// Add to flat nodes list.
		kasset_model_node new_node = {
			.name = kname_create(current->mName.data),
			.parent_index = INVALID_ID_U16,
			.children = KNULL,
			.child_count = 0,
			.local_transform = mat4_from_ai(&current->mTransformation)};
		u16 node_index = darray_length(nodes);
		darray_push(nodes, new_node);

		// Add it to the map
		node_map_entry new_map_entry = {
			.node = current,
			.index = node_index};
		darray_push(node_map, new_map_entry);

		// Push children onto the stack.
		for (u16 i = 0; i < current->mNumChildren; ++i) {
			stack_nodes[stack_top++] = current->mChildren[i];
		}
	}

	// Set parent/child by re-iterating the map.
	u16 node_map_count = darray_length(node_map);
	for (u16 i = 0; i < node_map_count; ++i) {
		const struct aiNode *current = node_map[i].node;
		u16 index = node_map[i].index;
		for (u16 c = 0; c < current->mNumChildren; c++) {
			const struct aiNode *child = current->mChildren[c];
			u16 child_index = INVALID_ID_U16;
			for (u16 s = 0; s < node_map_count; ++s) {
				if (node_map[s].node == child) {
					child_index = node_map[s].index;
					break;
				}
			}
			if (child_index != INVALID_ID_U16) {
				kasset_model_node *cn = &nodes[index];
				cn->children = KREALLOC_TYPE_CARRAY(cn->children, u16, cn->child_count + 1);
				cn->children[cn->child_count] = child_index;
				cn->child_count++;
				nodes[child_index].parent_index = index;
			}
		}
	}

	u32 node_count = darray_length(nodes);
	out_asset->node_count = node_count;
	if (node_count) {
		out_asset->nodes = KALLOC_TYPE_CARRAY(kasset_model_node, node_count);
		KCOPY_TYPE_CARRAY(out_asset->nodes, nodes, kasset_model_node, node_count);
	}
	darray_destroy(nodes);
	darray_destroy(node_map);

	// Copy channels and keys
	out_asset->animation_count = scene->mNumAnimations;
	if (out_asset->animation_count) {
		out_asset->animations = KALLOC_TYPE_CARRAY(kasset_model_animation, out_asset->animation_count);
		for (u16 a = 0; a < out_asset->animation_count; ++a) {
			struct aiAnimation *anim = scene->mAnimations[a];
			kasset_model_animation *out = &out_asset->animations[a];
			out->name = kname_create(anim->mName.data);
			out->duration = anim->mDuration;
			out->ticks_per_second = anim->mTicksPerSecond;
			out->channel_count = anim->mNumChannels;
			out->channels = KALLOC_TYPE_CARRAY(kasset_model_channel, out->channel_count);
			for (u16 c = 0; c < out->channel_count; c++) {
				struct aiNodeAnim *chn = anim->mChannels[c];
				kasset_model_channel *oc = &out->channels[c];
				kzero_memory(oc, sizeof(kmodel_channel));
				oc->name = kname_create(chn->mNodeName.data);

				// Positions
				oc->pos_count = chn->mNumPositionKeys;
				if (oc->pos_count) {
					oc->positions = KALLOC_TYPE_CARRAY(kasset_model_key_vec3, oc->pos_count);
					for (u32 k = 0; k < oc->pos_count; ++k) {
						struct aiVectorKey *vk = &chn->mPositionKeys[k];
						oc->positions[k].time = vk->mTime;
						oc->positions[k].value = (vec3){vk->mValue.x, vk->mValue.y, vk->mValue.z};
					}
				} else {
					oc->positions = KNULL;
				}

				// Rotations
				oc->rot_count = chn->mNumRotationKeys;
				if (oc->rot_count) {
					oc->rotations = KALLOC_TYPE_CARRAY(kasset_model_key_quat, oc->rot_count);
					for (u32 k = 0; k < oc->rot_count; ++k) {
						struct aiQuatKey *vk = &chn->mRotationKeys[k];
						oc->rotations[k].time = vk->mTime;
						oc->rotations[k].value = (quat){vk->mValue.x, vk->mValue.y, vk->mValue.z, vk->mValue.w};
					}
				} else {
					oc->rotations = KNULL;
				}

				// Scales
				oc->scale_count = chn->mNumScalingKeys;
				if (oc->scale_count) {
					oc->scales = KALLOC_TYPE_CARRAY(kasset_model_key_vec3, oc->scale_count);
					for (u32 k = 0; k < oc->scale_count; ++k) {
						struct aiVectorKey *vk = &chn->mScalingKeys[k];
						oc->scales[k].time = vk->mTime;
						oc->scales[k].value = (vec3){vk->mValue.x, vk->mValue.y, vk->mValue.z};
					}
				} else {
					oc->scales = KNULL;
				}
			}
		}
	}

	// Extract submeshes
	out_asset->submesh_count = scene->mNumMeshes;
	if (out_asset->submesh_count) {
		out_asset->submeshes = KALLOC_TYPE_CARRAY(kasset_model_submesh_data, out_asset->submesh_count);
		for (u32 m = 0; m < scene->mNumMeshes; ++m) {
			struct aiMesh *mesh = scene->mMeshes[m];
			kasset_model_submesh_data *target = &out_asset->submeshes[m];

			vertex_weight_accumulator *bone_data = 0;
			if (mesh->mNumBones) {
				bone_data = KALLOC_TYPE_CARRAY(vertex_weight_accumulator, mesh->mNumVertices);

				// Extract the bone weights from it.
				// NOTE: It's possible this might not line up index-wise to the global bones array.
				// May need to reconcile this later if this is an issue.
				for (u32 b = 0; b < mesh->mNumBones; ++b) {
					struct aiBone *ai_bone = mesh->mBones[b];
					kname ai_bone_name = kname_create(ai_bone->mName.data);

					for (u32 w = 0; w < ai_bone->mNumWeights; ++w) {
						const struct aiVertexWeight vw = ai_bone->mWeights[w];
						for (u32 bid = 0; bid < bone_count; ++bid) {
							if (bones[bid].name == ai_bone_name) {
								add_bone_weight(&bone_data[vw.mVertexId], bid, vw.mWeight);
								break;
							}
						}
						/* add_bone_weight(&bone_data[vw.mVertexId], b, vw.mWeight); */
					}
				}

				// HACK: Verify vertex weights.
				// for (u32 v = 0; v < mesh->mNumVertices; ++v) {
				//     f32 total_weight = 0.0f;
				//     for (u8 i = 0; i < 4; ++i) {
				//         total_weight += bone_data[v].weights[i];
				//     }
				//     KASSERT(kabs(total_weight - 1.0f) < 0.01f);
				// }
			}

			target->name = kname_create(mesh->mName.data);

			// Material name.
			struct aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

			// Base properties
			struct aiString ai_name;
			aiGetMaterialString(material, AI_MATKEY_NAME, &ai_name);
			target->material_name = kname_create(ai_name.data);

			target->vertex_count = mesh->mNumVertices;
			if (mesh->mNumBones) {
				target->vertices = KALLOC_TYPE_CARRAY(skinned_vertex_3d, target->vertex_count);
			} else {
				target->vertices = KALLOC_TYPE_CARRAY(vertex_3d, target->vertex_count);
			}
			target->index_count = mesh->mNumFaces * 3; // NOTE: assumes triangulated mesh, which should be fine here.
			target->indices = KALLOC_TYPE_CARRAY(u32, target->index_count);

			// Process all vertices
			for (u32 i = 0; i < mesh->mNumVertices; ++i) {
				struct aiVector3D *v = &mesh->mVertices[i];

				// Extract base vertex properties first.
				vertex_3d *target_vert = 0;
				if (mesh->mNumBones) {
					skinned_vertex_3d *sv = &((skinned_vertex_3d *)target->vertices)[i];
					target_vert = (vertex_3d *)sv;
				} else {
					target_vert = &((vertex_3d *)target->vertices)[i];
				}

				target_vert->position = vec3_create(v->x, v->y, v->z);
				if (mesh->mNormals) {
					struct aiVector3D *n = &mesh->mNormals[i];
					target_vert->normal = vec3_create(n->x, n->y, n->z);
				}
				if (mesh->mTangents && mesh->mBitangents) {
					struct aiVector3D *at = &mesh->mTangents[i];
					struct aiVector3D *ab = &mesh->mBitangents[i];
					vec3 t = vec3_create(at->x, at->y, at->z);
					vec3 b = vec3_create(ab->x, ab->y, ab->z);
					vec3 n = target_vert->normal;

					f32 handedness = (vec3_dot(vec3_cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
					target_vert->tangent = vec4_from_vec3(t, handedness);
				}

				if (mesh->mTextureCoords[0]) {
					target_vert->texcoord = vec2_create(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
				}

				if (mesh->mColors[0]) {
					struct aiColor4D *c = &mesh->mColors[0][i];
					target_vert->colour = vec4_create(c->r, c->g, c->b, c->a);
				} else {
					target_vert->colour = vec4_one();
				}

				// Extract bone data, if it exists.
				if (mesh->mNumBones) {
					skinned_vertex_3d *out_v = &((skinned_vertex_3d *)target->vertices)[i];
					kcopy_memory(out_v->bone_ids.elements, bone_data[i].bone_ids, sizeof(i32) * 4);
					kcopy_memory(out_v->weights.elements, bone_data[i].weights, sizeof(f32) * 4);

					// If the mesh has bones, it's skinned. Mark it as such.
					target->type = KASSET_MODEL_MESH_TYPE_SKINNED;
				}
			}

			if (bone_data) {
				kfree(bone_data);
			}

			// Process all Indices
			u32 idx = 0;
			for (u32 f = 0; f < mesh->mNumFaces; ++f) {
				const struct aiFace *face = &mesh->mFaces[f];
				for (u32 k = 0; k < face->mNumIndices; ++k) {
					target->indices[idx] = face->mIndices[k];
					idx++;
				}
			}
		}
	}

	return true;
}

static void anim_asset_destroy (kasset_model *asset) {
	if (!asset) {
		return;
	}

	for (u16 i = 0; i < asset->animation_count; ++i) {
		kasset_model_animation *a = &asset->animations[i];
		for (u16 c = 0; c < a->channel_count; c++) {
			kasset_model_channel *ch = &a->channels[c];
			kfree(ch->positions);
			kfree(ch->rotations);
			kfree(ch->scales);
		}
		kfree(a->channels);
	}
	kfree(asset->animations);
	kfree(asset->bones);
	for (u16 i = 0; i < asset->node_count; ++i) {
		kfree(asset->nodes[i].children);
	}
	kfree(asset->nodes);
}
