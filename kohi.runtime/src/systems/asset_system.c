#include "asset_system.h"

#include <assets/kasset_types.h>
#include <assets/kasset_utils.h>
#include <containers/darray.h>
#include <containers/u64_bst.h>
#include <core/event.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/identifier.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/platform.h>
#include <serializers/kasset_audio_serializer.h>
#include <serializers/kasset_bitmap_font_serializer.h>
#include <serializers/kasset_heightmap_terrain_serializer.h>
#include <serializers/kasset_hf_terrain_serializer.h>
#include <serializers/kasset_image_serializer.h>
#include <serializers/kasset_material_serializer.h>
#include <serializers/kasset_model_serializer.h>
#include <serializers/kasset_shader_serializer.h>
#include <serializers/kasset_system_font_serializer.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include "core/engine.h"
#include "platform/vfs.h"

typedef struct asset_watch {
	kasset_type type;
	u32 file_watch_id;
	kname asset_name;
	kname package_name;
} asset_watch;

typedef struct asset_system_state {
	vfs_state *vfs;

	// The name of the default package to use (i.e, the game's package name)
	kname default_package_name;
	const char *default_package_name_str;

	// Max number of assets that can be loaded at any given time.
	u32 max_asset_count;

#if KOHI_HOT_RELOAD
	// An array of watches which contain name, type, etc.
	asset_watch *watches;
	// A BST to use for lookups of asset watches by file_watch_id.
	bt_node *lookup_tree;
#endif
} asset_system_state;

#if KOHI_HOT_RELOAD
static asset_watch *get_watch (asset_system_state *state, u32 watch_id);
static b8 vfs_file_written (u16 code, void *sender, void *listener_inst, event_context data);
static b8 vfs_file_deleted (u16 code, void *sender, void *listener_inst, event_context data);
#endif

b8 asset_system_deserialize_config (const char *config_str, asset_system_config *out_config) {
	if (!config_str || !out_config) {
		KERROR("asset_system_deserialize_config requires a valid string and a pointer to hold the config.");
		return false;
	}

	kson_tree tree = {0};
	if (!kson_tree_from_string(config_str, &tree)) {
		KERROR("Failed to parse asset system configuration.");
		return false;
	}

	// max_asset_count
	if (!kson_object_property_value_get_int(&tree.root, "max_asset_count", (i64 *)&out_config->max_asset_count)) {
		KERROR("max_asset_count is a required field and was not provided.");
		return false;
	}

	kson_tree_cleanup(&tree);

	return true;
}

b8 asset_system_initialize (u64 *memory_requirement, struct asset_system_state *state, const asset_system_config *config) {
	if (!memory_requirement) {
		KERROR("asset_system_initialize requires a valid pointer to memory_requirement.");
		return false;
	}

	*memory_requirement = sizeof(asset_system_state);

	// Just doing a memory size lookup, don't count as a failure.
	if (!state) {
		return true;
	} else if (!config) {
		KERROR("asset_system_initialize: A pointer to valid configuration is required. Initialization failed.");
		return false;
	}

	state->default_package_name = config->default_package_name;
	state->default_package_name_str = kname_string_get(config->default_package_name);

	state->max_asset_count = config->max_asset_count;

	state->vfs = engine_systems_get()->vfs_system_state;

#if KOHI_HOT_RELOAD
	state->watches = kallocate(sizeof(asset_watch) * state->max_asset_count, MEMORY_TAG_ENGINE);

	// Asset lookup tree.
	{
		// NOTE: BST node created when first asset is watched.
		state->lookup_tree = 0;

		// Invalidate all lookups. Unknown = free slot.
		for (u32 i = 0; i < state->max_asset_count; ++i) {
			state->watches[i].type = KASSET_TYPE_UNKNOWN;
			state->watches[i].file_watch_id = INVALID_ID_U32;
		}
	}

	// Register for vfs load/delete events.
	event_register(EVENT_CODE_VFS_FILE_WRITTEN_TO_DISK, state, vfs_file_written);
	event_register(EVENT_CODE_VFS_FILE_DELETED_FROM_DISK, state, vfs_file_deleted);
#endif

	return true;
}

void asset_system_shutdown (struct asset_system_state *state) {
	if (state) {
#if KOHI_HOT_RELOAD
		if (state->watches) {
			// Unload all currently-held lookups.
			for (u32 i = 0; i < state->max_asset_count; ++i) {
				asset_watch *lookup = &state->watches[i];
				if (lookup->file_watch_id != INVALID_ID_U32) {
					platform_unwatch_file(lookup->file_watch_id);
				}
			}
			kfree(state->watches, sizeof(asset_watch) * state->max_asset_count, MEMORY_TAG_ARRAY);
		}

		// Destroy the BST.
		u64_bst_cleanup(state->lookup_tree);
#endif

		kzero_memory(state, sizeof(asset_system_state));
	}
}

#if KOHI_HOT_RELOAD
u32 _asset_system_watch_for_reload (struct asset_system_state *state, kasset_type type, kname asset_name, kname package_name) {
	if (state && asset_name != INVALID_KNAME) {

		if (package_name == INVALID_KNAME) {
			package_name = state->default_package_name;
		}

		b8 is_binary = kasset_type_is_binary(type);
		u32 file_watch_id = vfs_asset_watch(engine_systems_get()->vfs_system_state, asset_name, package_name, is_binary);

		// Add entry into the 'watch' list, using a pointer to the asset as the base.
		u32 index = INVALID_ID_U32;
		for (u32 i = 0; i < state->max_asset_count; ++i) {
			if (state->watches[i].type == KASSET_TYPE_UNKNOWN) {
				state->watches[i].type = type;
				state->watches[i].file_watch_id = file_watch_id;
				state->watches[i].package_name = package_name;
				state->watches[i].asset_name = asset_name;
				index = i;
				break;
			}
		}
		if (index == INVALID_ID_U32) {
			KFATAL("No space left in the watch cache.");
			return INVALID_ID_U32;
		}
		bt_node_value v = {
			.u32 = index};
		bt_node *new_node = u64_bst_insert(state->lookup_tree, file_watch_id, v);
		if (!state->lookup_tree) {
			state->lookup_tree = new_node;
		}

		return file_watch_id;
	}

	return INVALID_ID_U32;
}

void _asset_system_stop_watch (struct asset_system_state *state, u32 watch_id) {
	asset_watch *watch = get_watch(state, watch_id);

	KTRACE("Asset System: Watch for asset '%s' has been removed.", kname_string_get(watch->asset_name));

	// The watch is removed simply by resetting it, marking the slot as 'free'.
	watch->type = KASSET_TYPE_UNKNOWN;
	watch->file_watch_id = INVALID_ID_U32;
	watch->asset_name = INVALID_KNAME;
	watch->package_name = INVALID_KNAME;
}
#endif

// ////////////////////////////////////
// BINARY ASSETS
// ////////////////////////////////////

typedef struct kasset_binary_vfs_context {
	void *listener;
	PFN_kasset_binary_loaded_callback callback;
	kasset_binary *asset;
} kasset_binary_vfs_context;

static void vfs_on_binary_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_binary_vfs_context *context = asset_data.context;
	kasset_binary *out_asset = context->asset;
	out_asset->size = asset_data.size;
	void *content = kallocate(out_asset->size, MEMORY_TAG_ASSET);
	kcopy_memory(content, asset_data.bytes, out_asset->size);
	out_asset->content = content;

	KFREE_TYPE(context, kasset_binary_vfs_context, MEMORY_TAG_ASSET);
}

kname *asset_system_names_by_type (struct asset_system_state *state, kasset_type type, kname package_name, u32 *out_count) {
	return vfs_asset_names_by_type(state->vfs, type, package_name, out_count);
}

// async load from game package.
kasset_binary *asset_system_request_binary (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_binary_loaded_callback callback) {
	return asset_system_request_binary_from_package(state, state->default_package_name_str, name, listener, callback);
}

// sync load from game package.
kasset_binary *asset_system_request_binary_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_binary_from_package_sync(state, state->default_package_name_str, name);
}

// async load from specific package.
kasset_binary *asset_system_request_binary_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_binary_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_binary *out_asset = KALLOC_TYPE(kasset_binary, MEMORY_TAG_ASSET);

	kasset_binary_vfs_context *context = KALLOC_TYPE(kasset_binary_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = state->default_package_name,
		.is_binary = true,
		.vfs_callback = vfs_on_binary_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_binary_vfs_context)};
	vfs_request_asset(state->vfs, info);

	return out_asset;
}
// sync load from specific package.
kasset_binary *asset_system_request_binary_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_binary *out_asset = KALLOC_TYPE(kasset_binary, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	out_asset->size = data.size;
	void *content = kallocate(out_asset->size, MEMORY_TAG_ASSET);
	kcopy_memory(content, data.bytes, out_asset->size);
	vfs_asset_data_cleanup(&data);
	out_asset->content = content;

	return out_asset;
}

void asset_system_release_binary (struct asset_system_state *state, kasset_binary *asset) {
	if (state && asset) {
		if (asset->content && asset->size) {
			kfree((void *)asset->content, asset->size, MEMORY_TAG_ASSET);
		}
		KFREE_TYPE(asset, kasset_binary, MEMORY_TAG_ASSET);
	}
}

b8 asset_system_write_binary (struct asset_system_state *state, kname package_name, kname asset_name, u64 size, const void *data) {
	KASSERT(state && asset_name);

	return vfs_asset_write_binary(state->vfs, asset_name, package_name == INVALID_KNAME ? state->default_package_name : package_name, size, data);
}

// ////////////////////////////////////
// TEXT ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_text *asset_system_request_text_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_text_from_package_sync(state, state->default_package_name_str, name);
}
// sync load from specific package.
kasset_text *asset_system_request_text_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_text *out_asset = KALLOC_TYPE(kasset_text, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = false,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	out_asset->content = string_duplicate(data.text);

	vfs_asset_data_cleanup(&data);

	return out_asset;
}

void asset_system_release_text (struct asset_system_state *state, kasset_text *asset) {
	if (state && asset) {
		if (asset->content) {
			string_free(asset->content);
		}
		KFREE_TYPE(asset, kasset_text, MEMORY_TAG_ASSET);
	}
}

b8 asset_system_write_text (struct asset_system_state *state, kname package_name, kname asset_name, const char *content) {
	KASSERT(state && package_name && asset_name);

	return vfs_asset_write_text(state->vfs, asset_name, package_name, content);
}

// ////////////////////////////////////
// IMAGE ASSETS
// ////////////////////////////////////

typedef struct kasset_image_vfs_context {
	void *listener;
	PFN_kasset_image_loaded_callback callback;
	kasset_image *asset;
} kasset_image_vfs_context;

static void vfs_on_image_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_image_vfs_context *context = asset_data.context;
	kasset_image *out_asset = context->asset;
	b8 result = kasset_image_deserialize(asset_data.size, asset_data.bytes, out_asset);
	if (!result) {
		KERROR("Failed to deserialize image asset. See logs for details.");
	}

	if (context->callback) {
		context->callback(context->listener, out_asset);
	}
}

// async load from game package.
kasset_image *asset_system_request_image (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_image_loaded_callback callback) {
	return asset_system_request_image_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_image *asset_system_request_image_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_image_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_image *asset_system_request_image_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_image_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_image *out_asset = KALLOC_TYPE(kasset_image, MEMORY_TAG_ASSET);

	kasset_image_vfs_context *context = KALLOC_TYPE(kasset_image_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
		.vfs_callback = vfs_on_image_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_image_vfs_context)};
	vfs_request_asset(state->vfs, info);

	out_asset->name = kname_create(name);

	return out_asset;
}
// sync load from specific package.
kasset_image *asset_system_request_image_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_image *out_asset = KALLOC_TYPE(kasset_image, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_image_deserialize(data.size, data.bytes, out_asset);
	vfs_asset_data_cleanup(&data);
	if (!result) {
		KERROR("Failed to deserialize image asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_image, MEMORY_TAG_ASSET);
		return 0;
	}

	out_asset->name = kname_create(name);

	return out_asset;
}

void asset_system_release_image (struct asset_system_state *state, kasset_image *asset) {
	if (state && asset) {
		KTRACE("Releasing image asset '%s'.", kname_string_get(asset->name));
		if (asset->pixel_array_size && asset->pixels) {
			kfree((void *)asset->pixels, asset->pixel_array_size, MEMORY_TAG_ASSET);
		}
		KFREE_TYPE(asset, kasset_image, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// BITMAP FONT ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_bitmap_font *asset_system_request_bitmap_font_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_bitmap_font_from_package_sync(state, state->default_package_name_str, name);
}

// sync load from specific package.
kasset_bitmap_font *asset_system_request_bitmap_font_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_bitmap_font *out_asset = KALLOC_TYPE(kasset_bitmap_font, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_bitmap_font_deserialize(data.size, data.bytes, out_asset);
	vfs_asset_data_cleanup(&data);
	if (!result) {
		KERROR("Failed to deserialize bitmap font asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_bitmap_font, MEMORY_TAG_ASSET);
		return 0;
	}

	return out_asset;
}

void asset_system_release_bitmap_font (struct asset_system_state *state, kasset_bitmap_font *asset) {
	if (state && asset) {
		KFREE_TYPE_CARRAY(asset->kernings, kasset_bitmap_font_kerning, asset->kerning_count);
		KFREE_TYPE_CARRAY(asset->glyphs, kasset_bitmap_font_glyph, asset->glyph_count);
		KFREE_TYPE_CARRAY(asset->pages, kasset_bitmap_font_page, asset->page_count);

		KFREE_TYPE(asset, kasset_bitmap_font, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// SYSTEM FONT ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_system_font *asset_system_request_system_font_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_system_font_from_package_sync(state, state->default_package_name_str, name);
}

// sync load from specific package.
kasset_system_font *asset_system_request_system_font_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_system_font *out_asset = KALLOC_TYPE(kasset_system_font, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = false,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_system_font_deserialize(data.text, out_asset);
	vfs_asset_data_cleanup(&data);
	if (!result) {
		KERROR("Failed to deserialize system font asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_system_font, MEMORY_TAG_ASSET);
		return 0;
	}

	// Load the font binary file.
	kasset_binary *ttf_binary_asset = asset_system_request_binary_from_package_sync(
		state,
		kname_string_get(out_asset->ttf_asset_package_name),
		kname_string_get(out_asset->ttf_asset_name));

	// Take a copy of the binary asset's data.
	out_asset->font_binary_size = ttf_binary_asset->size;
	out_asset->font_binary = kallocate(out_asset->font_binary_size, MEMORY_TAG_ASSET);
	kcopy_memory(out_asset->font_binary, ttf_binary_asset->content, out_asset->font_binary_size);

	// Release the binary asset.
	asset_system_release_binary(state, ttf_binary_asset);

	return out_asset;
}

void asset_system_release_system_font (struct asset_system_state *state, kasset_system_font *asset) {
	if (state && asset) {
		if (asset->faces && asset->face_count) {
			KFREE_TYPE_CARRAY(asset->faces, kasset_system_font_face, asset->face_count);
		}

		if (asset->font_binary && asset->font_binary_size) {
			kfree(asset->font_binary, asset->font_binary_size, MEMORY_TAG_ASSET);
		}

		KFREE_TYPE(asset, kasset_system_font, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// MODEL ASSETS
// ////////////////////////////////////

typedef struct kasset_model_vfs_context {
	void *listener;
	PFN_kasset_model_loaded_callback callback;
	kasset_model *asset;
} kasset_model_vfs_context;

static void vfs_on_model_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_model_vfs_context *context = asset_data.context;
	kasset_model *out_asset = context->asset;
	b8 result = kasset_model_deserialize(asset_data.size, asset_data.bytes, out_asset);
	if (!result) {
		KERROR("Failed to deserialize model asset. See logs for details.");
	}

	if (context->callback) {
		context->callback(context->listener, out_asset);
	}
}

// async load from game package.
kasset_model *asset_system_request_model (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_model_loaded_callback callback) {
	return asset_system_request_model_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_model *asset_system_request_model_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_model_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_model *asset_system_request_model_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_model_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_model *out_asset = KALLOC_TYPE(kasset_model, MEMORY_TAG_ASSET);

	kasset_model_vfs_context *context = KALLOC_TYPE(kasset_model_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = state->default_package_name,
		.is_binary = true,
		.vfs_callback = vfs_on_model_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_model_vfs_context)};
	vfs_request_asset(state->vfs, info);

	return out_asset;
}
// sync load from specific package.
kasset_model *asset_system_request_model_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_model *out_asset = KALLOC_TYPE(kasset_model, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_model_deserialize(data.size, data.bytes, out_asset);
	if (!result) {
		KERROR("Failed to deserialize model asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_model, MEMORY_TAG_ASSET);
		return KNULL;
	}

	return out_asset;
}

void asset_system_release_model (struct asset_system_state *state, kasset_model *asset) {
	if (state && asset) {
		// Asset type-specific data cleanup
		if (asset->submeshes && asset->submesh_count) {
			for (u32 i = 0; i < asset->submesh_count; ++i) {
				kasset_model_submesh_data *submesh = &asset->submeshes[i];
				u64 vs = submesh->type == KASSET_MODEL_MESH_TYPE_STATIC ? sizeof(vertex_3d) : sizeof(skinned_vertex_3d);
				if (submesh->vertices && submesh->vertex_count) {
					kfree(submesh->vertices, vs * submesh->vertex_count, MEMORY_TAG_BINARY_DATA);
				}
				if (submesh->indices && submesh->index_count) {
					kfree(submesh->indices, sizeof(u32) * submesh->index_count, MEMORY_TAG_BINARY_DATA);
				}
			}
			kfree(asset->submeshes, sizeof(asset->submeshes[0]) * asset->submesh_count, MEMORY_TAG_ARRAY);
			asset->submeshes = KNULL;
			asset->submesh_count = 0;
		}
		if (asset->animations && asset->animation_count) {
			for (u32 i = 0; i < asset->animation_count; ++i) {
				for (u32 c = 0; c < asset->animations[i].channel_count; c++) {
					kasset_model_channel *ch = &asset->animations[i].channels[c];

					if (ch->pos_count && ch->positions) {
						KFREE_TYPE_CARRAY(ch->positions, kasset_model_key_vec3, ch->pos_count);
						ch->pos_count = 0;
						ch->positions = KNULL;
					}

					if (ch->scale_count && ch->scales) {
						KFREE_TYPE_CARRAY(ch->scales, kasset_model_key_vec3, ch->scale_count);
						ch->scale_count = 0;
						ch->scales = KNULL;
					}

					if (ch->rot_count && ch->rotations) {
						KFREE_TYPE_CARRAY(ch->rotations, kasset_model_key_quat, ch->rot_count);
						ch->rot_count = 0;
						ch->rotations = KNULL;
					}
				}
				KFREE_TYPE_CARRAY(asset->animations[i].channels, kasset_model_channel, asset->animations[i].channel_count);
				asset->animations[i].channels = KNULL;
			}
			KFREE_TYPE_CARRAY(asset->animations, kasset_model_animation, asset->animation_count);
			asset->animations = KNULL;
		}

		if (asset->bone_count && asset->bones) {
			KFREE_TYPE_CARRAY(asset->bones, kasset_model_bone, asset->bone_count);
			asset->bones = KNULL;
			asset->bone_count = 0;
		}

		if (asset->nodes && asset->node_count) {
			for (u16 i = 0; i < asset->node_count; ++i) {
				kasset_model_node *node = &asset->nodes[i];
				if (node->child_count && node->children) {
					KFREE_TYPE_CARRAY(node->children, u16, node->child_count);
				}
			}
			KFREE_TYPE_CARRAY(asset->nodes, kasset_model_node, asset->node_count);
			asset->nodes = KNULL;
			asset->node_count = 0;
		}

		KFREE_TYPE(asset, kasset_model, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// HEIGHTMAP TERRAIN ASSETS
// ////////////////////////////////////

typedef struct kasset_heightmap_terrain_vfs_context {
	void *listener;
	PFN_kasset_heightmap_terrain_loaded_callback callback;
	kasset_heightmap_terrain *asset;
} kasset_heightmap_terrain_vfs_context;

static void vfs_on_heightmap_terrain_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_heightmap_terrain_vfs_context *context = asset_data.context;
	b8 result = kasset_heightmap_terrain_deserialize(asset_data.text, context->asset);
	if (!result) {
		KERROR("Failed to deserialize heightmap_terrain asset. See logs for details.");
	}

	KFREE_TYPE(context, kasset_heightmap_terrain_vfs_context, MEMORY_TAG_ASSET);
}

// async load from game package.
kasset_heightmap_terrain *asset_system_request_heightmap_terrain (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_heightmap_terrain_loaded_callback callback) {
	return asset_system_request_heightmap_terrain_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_heightmap_terrain *asset_system_request_heightmap_terrain_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_heightmap_terrain_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_heightmap_terrain *asset_system_request_heightmap_terrain_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_heightmap_terrain_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_heightmap_terrain *out_asset = KALLOC_TYPE(kasset_heightmap_terrain, MEMORY_TAG_ASSET);

	kasset_heightmap_terrain_vfs_context *context = KALLOC_TYPE(kasset_heightmap_terrain_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = state->default_package_name,
		.is_binary = false,
		.vfs_callback = vfs_on_heightmap_terrain_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_heightmap_terrain_vfs_context)};
	vfs_request_asset(state->vfs, info);

	return out_asset;
}
// sync load from specific package.
kasset_heightmap_terrain *asset_system_request_heightmap_terrain_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_heightmap_terrain *out_asset = KALLOC_TYPE(kasset_heightmap_terrain, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = false,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_heightmap_terrain_deserialize(data.text, out_asset);
	if (!result) {
		KERROR("Failed to deserialize heightmap_terrain asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_heightmap_terrain, MEMORY_TAG_ASSET);
		return 0;
	}

	return out_asset;
}

void asset_system_release_heightmap_terrain (struct asset_system_state *state, kasset_heightmap_terrain *asset) {
	if (state && asset) {
		// Asset type-specific data cleanup
		if (asset->material_count && asset->material_names) {
			kfree(asset->material_names, sizeof(kname) * asset->material_count, MEMORY_TAG_ARRAY);
			asset->material_names = 0;
			asset->material_count = 0;
		}
		KFREE_TYPE(asset, kasset_heightmap_terrain, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// HEIGHTFIELD TERRAIN ASSETS
// ////////////////////////////////////

typedef struct kasset_hf_terrain_vfs_context {
	void *listener;
	PFN_kasset_hf_terrain_loaded_callback callback;
	kasset_hf_terrain *asset;
} kasset_hf_terrain_vfs_context;

static void vfs_on_hf_terrain_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_hf_terrain_vfs_context *context = asset_data.context;
	kasset_hf_terrain *out_asset = context->asset;
	b8 result = kasset_hf_terrain_deserialize(asset_data.size, asset_data.bytes, out_asset);
	if (!result) {
		KERROR("Failed to deserialize hf_terrain asset. See logs for details.");
	}

	if (context->callback) {
		context->callback(context->listener, out_asset);
	}
}

// async load from game package.
kasset_hf_terrain *asset_system_request_hf_terrain (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_hf_terrain_loaded_callback callback) {
	return asset_system_request_hf_terrain_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_hf_terrain *asset_system_request_hf_terrain_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_hf_terrain_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_hf_terrain *asset_system_request_hf_terrain_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_hf_terrain_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_hf_terrain *out_asset = KALLOC_TYPE(kasset_hf_terrain, MEMORY_TAG_ASSET);

	kasset_hf_terrain_vfs_context *context = KALLOC_TYPE(kasset_hf_terrain_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = state->default_package_name,
		.is_binary = true,
		.vfs_callback = vfs_on_hf_terrain_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_hf_terrain_vfs_context)};
	vfs_request_asset(state->vfs, info);

	return out_asset;
}
// sync load from specific package.
kasset_hf_terrain *asset_system_request_hf_terrain_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_hf_terrain *out_asset = KALLOC_TYPE(kasset_hf_terrain, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_hf_terrain_deserialize(data.size, data.bytes, out_asset);
	vfs_asset_data_cleanup(&data);
	if (!result) {
		KERROR("Failed to deserialize hf_terrain asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_hf_terrain, MEMORY_TAG_ASSET);
		return KNULL;
	}

	return out_asset;
}

void asset_system_release_hf_terrain (struct asset_system_state *state, kasset_hf_terrain *asset) {
	if (state && asset) {
		KFREE_TYPE_CARRAY(asset->blocks, kasset_hf_terrain_block, asset->block_count_x * asset->block_count_z);
		KFREE_TYPE_CARRAY(asset->vertices, kasset_hf_terrain_vertex, asset->vertex_count);
		KFREE_TYPE_CARRAY(asset->materials, kasset_hf_terrain_material, asset->material_count);
		for (u32 i = 0; i < asset->material_count; ++i) {
			string_free(asset->material_map_names[i].albedo_str);
			string_free(asset->material_map_names[i].normal_str);
			string_free(asset->material_map_names[i].mra_str);
		}
		KFREE_TYPE_CARRAY(asset->material_map_names, kasset_hf_terrain_material_map_names, asset->material_count);
		KFREE_TYPE_CARRAY(asset->material_names, const char *, asset->material_count);

		KFREE_TYPE(asset, kasset_hf_terrain, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// MATERIAL ASSETS
// ////////////////////////////////////

typedef struct kasset_material_vfs_context {
	void *listener;
	PFN_kasset_material_loaded_callback callback;
	kasset_material *asset;
} kasset_material_vfs_context;

static void vfs_on_material_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_material_vfs_context *context = asset_data.context;
	b8 result = kasset_material_deserialize(asset_data.text, context->asset);
	if (!result) {
		KERROR("Failed to deserialize material asset. See logs for details.");
	}

	context->asset->name = asset_data.asset_name;

	if (context->callback) {
		context->callback(context->listener, context->asset);
	}
}

// async load from game package.
kasset_material *asset_system_request_material (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_material_loaded_callback callback) {
	return asset_system_request_material_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_material *asset_system_request_material_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_material_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_material *asset_system_request_material_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_material_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_material *out_asset = KALLOC_TYPE(kasset_material, MEMORY_TAG_ASSET);

	kasset_material_vfs_context *context = KALLOC_TYPE(kasset_material_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = state->default_package_name,
		.is_binary = false,
		.vfs_callback = vfs_on_material_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_material_vfs_context)};
	vfs_request_asset(state->vfs, info);

	return out_asset;
}
// sync load from specific package.
kasset_material *asset_system_request_material_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_material *out_asset = KALLOC_TYPE(kasset_material, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = false,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_material_deserialize(data.text, out_asset);
	vfs_asset_data_cleanup(&data);
	if (!result) {
		KERROR("Failed to deserialize material asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_material, MEMORY_TAG_ASSET);
		return 0;
	}

	out_asset->name = info.asset_name;

	return out_asset;
}

void asset_system_release_material (struct asset_system_state *state, kasset_material *asset) {
	if (state && asset) {
		// Asset type-specific data cleanup
		if (asset->custom_sampler_count && asset->custom_samplers) {
			KFREE_TYPE_CARRAY(asset->custom_samplers, kmaterial_sampler_config, asset->custom_sampler_count);
		}

		KFREE_TYPE(asset, kasset_material, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// AUDIO ASSETS
// ////////////////////////////////////

typedef struct kasset_audio_vfs_context {
	void *listener;
	PFN_kasset_audio_loaded_callback callback;
	kasset_audio *asset;
} kasset_audio_vfs_context;

static void vfs_on_audio_asset_loaded_callback (struct vfs_state *vfs, vfs_asset_data asset_data) {
	kasset_audio_vfs_context *context = asset_data.context;
	b8 result = kasset_audio_deserialize(asset_data.size, asset_data.bytes, context->asset);
	if (!result) {
		KERROR("Failed to deserialize audio asset. See logs for details.");
	}

	context->asset->name = asset_data.asset_name;

	if (context->callback) {
		context->callback(context->listener, context->asset);
	}
}

// async load from game package.
kasset_audio *asset_system_request_audio (struct asset_system_state *state, const char *name, void *listener, PFN_kasset_audio_loaded_callback callback) {
	return asset_system_request_audio_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_audio *asset_system_terrain_request_audio_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_audio_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_audio *asset_system_request_audio_from_package (struct asset_system_state *state, const char *package_name, const char *name, void *listener, PFN_kasset_audio_loaded_callback callback) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_audio *out_asset = KALLOC_TYPE(kasset_audio, MEMORY_TAG_ASSET);

	kasset_audio_vfs_context *context = KALLOC_TYPE(kasset_audio_vfs_context, MEMORY_TAG_ASSET);
	context->asset = out_asset;
	context->callback = callback;
	context->listener = listener;

	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = state->default_package_name,
		.is_binary = true,
		.vfs_callback = vfs_on_audio_asset_loaded_callback,
		.context = context,
		.context_size = sizeof(kasset_audio_vfs_context)};
	vfs_request_asset(state->vfs, info);

	return out_asset;
}
// sync load from specific package.
kasset_audio *asset_system_request_audio_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_audio *out_asset = KALLOC_TYPE(kasset_audio, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = true,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_audio_deserialize(data.size, data.bytes, out_asset);
	if (!result) {
		KERROR("Failed to deserialize audio asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_audio, MEMORY_TAG_ASSET);
		return 0;
	}
	vfs_asset_data_cleanup(&data);

	out_asset->name = info.asset_name;

	return out_asset;
}

void asset_system_release_audio (struct asset_system_state *state, kasset_audio *asset) {
	if (state && asset) {
		// Asset type-specific data cleanup
		if (asset->pcm_data_size && asset->pcm_data) {
			kfree(asset->pcm_data, asset->pcm_data_size, MEMORY_TAG_ASSET);
		}

		KFREE_TYPE(asset, kasset_audio, MEMORY_TAG_ASSET);
	}
}

// ////////////////////////////////////
// SHADER ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_shader *asset_system_terrain_request_shader_sync (struct asset_system_state *state, const char *name) {
	return asset_system_request_shader_from_package_sync(state, state->default_package_name_str, name);
}

// sync load from specific package.
kasset_shader *asset_system_request_shader_from_package_sync (struct asset_system_state *state, const char *package_name, const char *name) {
	if (!state || !name || !string_length(name)) {
		KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
		return 0;
	}

	kasset_shader *out_asset = KALLOC_TYPE(kasset_shader, MEMORY_TAG_ASSET);
	vfs_request_info info = {
		.asset_name = kname_create(name),
		.package_name = kname_create(package_name),
		.is_binary = false,
	};
	vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

	b8 result = kasset_shader_deserialize(data.text, out_asset);
	vfs_asset_data_cleanup(&data);
	if (!result) {
		KERROR("Failed to deserialize shader asset. See logs for details.");
		KFREE_TYPE(out_asset, kasset_shader, MEMORY_TAG_ASSET);
		return 0;
	}

	out_asset->name = info.asset_name;

	return out_asset;
}

void asset_system_release_shader (struct asset_system_state *state, kasset_shader *asset) {
	if (state && asset) {
		// Asset type-specific data cleanup

		// Vertex pipelines
		for (u8 pi = 0; pi < asset->pipeline_count; ++pi) {
			kasset_shader_pipeline *p = &asset->pipelines[pi];
			// Stages
			if (p->stages && p->stage_count) {
				for (u32 i = 0; i < p->stage_count; ++i) {
					kasset_shader_stage *stage = &p->stages[i];
					if (stage->source_asset_name) {
						string_free(stage->source_asset_name);
					}
					if (stage->package_name) {
						string_free(stage->package_name);
					}
				}
				kfree(p->stages, sizeof(kasset_shader_stage) * p->stage_count, MEMORY_TAG_ARRAY);
				p->stages = 0;
				p->stage_count = 0;
			}

			// Attributes
			if (p->attributes && p->attribute_count) {
				for (u32 i = 0; i < p->attribute_count; ++i) {
					kasset_shader_attribute *attrib = &p->attributes[i];
					if (attrib->name) {
						string_free(attrib->name);
					}
				}
				kfree(p->attributes, sizeof(kasset_shader_attribute) * p->attribute_count, MEMORY_TAG_ARRAY);
				p->attributes = 0;
				p->attribute_count = 0;
			}
		}
		KFREE_TYPE_CARRAY(asset->pipelines, kasset_shader_pipeline, asset->pipeline_count);
		asset->pipelines = KNULL;

		// binding sets
		if (asset->binding_sets && asset->binding_set_count) {
			for (u32 i = 0; i < asset->binding_set_count; ++i) {
				shader_binding_set_config *binding_set = &asset->binding_sets[i];

				KFREE_TYPE_CARRAY(binding_set->bindings, shader_binding_config, binding_set->binding_count);
			}
			KFREE_TYPE_CARRAY(asset->binding_sets, shader_binding_set_config, asset->binding_set_count);
			asset->binding_sets = 0;
			asset->binding_set_count = 0;
		}

		// attachments
		if (asset->colour_attachment_count && asset->colour_attachments) {
			for (u8 c = 0; c < asset->colour_attachment_count; c++) {
				if (asset->colour_attachments[c].name) {
					string_free(asset->colour_attachments[c].name);
				}
			}
			KFREE_TYPE_CARRAY(asset->colour_attachments, kasset_shader_attachment, asset->colour_attachment_count);
			asset->colour_attachments = KNULL;
		}

		if (asset->depth_attachment.name) {
			string_free(asset->depth_attachment.name);
		}
		if (asset->stencil_attachment.name) {
			string_free(asset->stencil_attachment.name);
		}

		KFREE_TYPE(asset, kasset_shader, MEMORY_TAG_ASSET);
	}
}

#if KOHI_HOT_RELOAD
static asset_watch *get_watch (asset_system_state *state, u32 watch_id) {
	const bt_node *node = u64_bst_find(state->lookup_tree, watch_id);
	if (!node) {
		KWARN("Asset System: The provided watch_id (%d) isn't registered in the system. Nothing to be done.", watch_id);
		return false; // Allow other listeners to handle the event, but boot early.
	}

	u32 index = node->value.u32;
	return &state->watches[index];
}

static b8 vfs_file_written (u16 code, void *sender, void *listener_inst, event_context context) {
	if (code == EVENT_CODE_VFS_FILE_WRITTEN_TO_DISK) {
		asset_system_state *state = (asset_system_state *)listener_inst;
		vfs_asset_data *asset_data = (vfs_asset_data *)sender;

		KTRACE("Asset System: Notification occurred that asset '%s' has been written to on disk. Performing hot reload.", asset_data->path);

		u32 watch_id = context.data.u32[0];
		asset_watch *watch = get_watch(state, watch_id);

		void *out_asset = 0;

		// LEFTOFF: handle the asset by type when hot-reloading
		switch (watch->type) {
		case KASSET_TYPE_BINARY: {
			kasset_binary *typed_asset = KALLOC_TYPE(kasset_binary, MEMORY_TAG_ASSET);

			kasset_binary_vfs_context *context = KALLOC_TYPE(kasset_binary_vfs_context, MEMORY_TAG_ASSET);
			context->asset = typed_asset;

			asset_data->context = context;
			vfs_on_binary_asset_loaded_callback(state->vfs, *asset_data);

			out_asset = typed_asset;
		} break;
		case KASSET_TYPE_TEXT: {
			kasset_text *typed_asset = KALLOC_TYPE(kasset_text, MEMORY_TAG_ASSET);
			typed_asset->content = string_duplicate(asset_data->text);
			out_asset = typed_asset;
		} break;

			// NOTE: There isn't much value in hot-reloading the shader config, which is what this asset type is.
			/* case KASSET_TYPE_SHADER: {
				kasset_shader* typed_asset = KALLOC_TYPE(kasset_shader, MEMORY_TAG_ASSET);

				b8 result = kasset_shader_deserialize(asset_data->text, typed_asset);
				if (!result) {
					KERROR("Failed to deserialize shader asset. See logs for details.");
					KFREE_TYPE(typed_asset, kasset_shader, MEMORY_TAG_ASSET);
				} else {
					typed_asset->name = watch->asset_name;
				}
				out_asset = typed_asset;

			} break; */

			// TODO: hot-reload these types
			/* case KASSET_TYPE_IMAGE: */
			/* case KASSET_TYPE_MATERIAL: */
			/* case KASSET_TYPE_KSON: */

			// NOTE: The below types probalby should not support hot-reloading.
		/* case KASSET_TYPE_STATIC_MESH: */
		/* case KASSET_TYPE_HEIGHTMAP_TERRAIN: */
		/* case KASSET_TYPE_SCENE: */
		/* case KASSET_TYPE_BITMAP_FONT: */
		/* case KASSET_TYPE_SYSTEM_FONT: */
		/* case KASSET_TYPE_VOXEL_TERRAIN: */
		/* case KASSET_TYPE_SKELETAL_MESH: */
		/* case KASSET_TYPE_AUDIO: */
		case KASSET_TYPE_UNKNOWN:
		default:
			KWARN("%s: Asset type '%s' not supported for hot reload.", __FUNCTION__, kasset_type_to_string(watch->type));
			break;
		}

		// Fire off a message that the asset was hot-reloaded. It is up to the appropriate system
		// to handle it from this point on. Note that the asset will need to be released by the
		// watcher every time this happens.
		if (out_asset) {
			event_context evt_context = {
				.data.u32[0] = watch_id};
			event_fire(EVENT_CODE_ASSET_HOT_RELOADED, out_asset, evt_context);
		} else {
			KWARN("%s: out_asset not set - notification event will not be fired.", __FUNCTION__);
		}
	}

	return false; // Allow other listeners to handle the event.
}

static b8 vfs_file_deleted (u16 code, void *sender, void *listener_inst, event_context context) {
	if (code == EVENT_CODE_VFS_FILE_DELETED_FROM_DISK) {
		asset_system_state *state = (asset_system_state *)listener_inst;

		u32 watch_id = context.data.u32[0];
		KTRACE("Asset System: Notification occurred that an asset has been deleted from disk. Watch will be removed.");

		_asset_system_stop_watch(state, watch_id);
	}

	return false; // Allow other listeners to handle the event.
}
#endif
