#include "asset_system.h"
#include "core/engine.h"

#include "platform/vfs.h"
#include "serializers/kasset_audio_serializer.h"
#include "serializers/kasset_bitmap_font_serializer.h"
#include "serializers/kasset_heightmap_terrain_serializer.h"
#include "serializers/kasset_image_serializer.h"
#include "serializers/kasset_material_serializer.h"
#include "serializers/kasset_scene_serializer.h"
#include "serializers/kasset_shader_serializer.h"
#include "serializers/kasset_static_mesh_serializer.h"
#include "serializers/kasset_system_font_serializer.h"

#include <assets/kasset_types.h>
#include <assets/kasset_utils.h>
#include <containers/darray.h>
#include <containers/u64_bst.h>
#include <core/event.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/identifier.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <strings/kname.h>
#include <strings/kstring.h>

typedef struct asset_lookup {
    // The asset itself, owned by this lookup.
    kasset* asset;
    // The current number of references to the asset.
    i32 reference_count;
    // Indicates if the asset will be released when the reference_count reaches 0.
    b8 auto_release;

    u32 file_watch_id;

    void* hot_reload_context;
} asset_lookup;

typedef struct asset_system_state {
    vfs_state* vfs;

    // The name of the default package to use (i.e, the game's package name)
    kname default_package_name;
    const char* default_package_name_str;

    // Max number of assets that can be loaded at any given time.
    u32 max_asset_count;
    // An array of lookups which contain reference and release data.
    asset_lookup* lookups;
    // A BST to use for lookups of assets by name.
    bt_node* lookup_tree;
} asset_system_state;

b8 asset_system_deserialize_config(const char* config_str, asset_system_config* out_config) {
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
    if (!kson_object_property_value_get_int(&tree.root, "max_asset_count", (i64*)&out_config->max_asset_count)) {
        KERROR("max_asset_count is a required field and was not provided.");
        return false;
    }

    return true;
}

b8 asset_system_initialize(u64* memory_requirement, struct asset_system_state* state, const asset_system_config* config) {
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
    state->lookups = kallocate(sizeof(asset_lookup) * state->max_asset_count, MEMORY_TAG_ENGINE);

    // Asset lookup tree.
    {
        // NOTE: BST node created when first asset is requested.
        state->lookup_tree = 0;

        // Invalidate all lookups.
        for (u32 i = 0; i < state->max_asset_count; ++i) {
            state->lookups[i].asset = 0;
        }
    }

    state->vfs = engine_systems_get()->vfs_system_state;

    return true;
}

void asset_system_shutdown(struct asset_system_state* state) {
    if (state) {
        if (state->lookups) {
            // Unload all currently-held lookups.
            for (u32 i = 0; i < state->max_asset_count; ++i) {
                asset_lookup* lookup = &state->lookups[i];
                if (lookup->asset) {
                    // FIXME: Asset cache not currently being written to on the new asset load logic, need to do this so it can be undone here.
                    // Force release the asset.
                    /* asset_system_release_internal(state, lookup->asset->name, lookup->asset->package_name, true); */
                }
            }
            kfree(state->lookups, sizeof(asset_lookup) * state->max_asset_count, MEMORY_TAG_ARRAY);
        }

        // Destroy the BST.
        u64_bst_cleanup(state->lookup_tree);

        kzero_memory(state, sizeof(asset_system_state));
    }
}

// ////////////////////////////////////
// BINARY ASSETS
// ////////////////////////////////////

typedef struct kasset_binary_vfs_context {
    void* listener;
    PFN_kasset_binary_loaded_callback callback;
    kasset_binary* asset;
} kasset_binary_vfs_context;

static void vfs_on_binary_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data) {
    kasset_binary_vfs_context* context = asset_data.context;
    kasset_binary* out_asset = context->asset;
    out_asset->size = asset_data.size;
    void* content = kallocate(out_asset->size, MEMORY_TAG_ASSET);
    kcopy_memory(content, asset_data.bytes, out_asset->size);
    out_asset->content = content;

    KFREE_TYPE(context, kasset_binary_vfs_context, MEMORY_TAG_ASSET);
}

// async load from game package.
kasset_binary* asset_system_request_binary(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_binary_loaded_callback callback) {
    return asset_system_request_binary_from_package(state, state->default_package_name_str, name, listener, callback);
}

// sync load from game package.
kasset_binary* asset_system_request_binary_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_binary_from_package_sync(state, state->default_package_name_str, name);
}

// async load from specific package.
kasset_binary* asset_system_request_binary_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_binary_loaded_callback callback) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_binary* out_asset = KALLOC_TYPE(kasset_binary, MEMORY_TAG_ASSET);

    kasset_binary_vfs_context* context = KALLOC_TYPE(kasset_binary_vfs_context, MEMORY_TAG_ASSET);
    context->asset = out_asset;
    context->callback = callback;
    context->listener = listener;

    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = state->default_package_name,
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
        .vfs_callback = vfs_on_binary_asset_loaded_callback,
        .context = context,
        .context_size = sizeof(kasset_binary_vfs_context)};
    vfs_request_asset(state->vfs, info);

    return out_asset;
}
// sync load from specific package.
kasset_binary* asset_system_request_binary_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_binary* out_asset = KALLOC_TYPE(kasset_binary, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    out_asset->size = data.size;
    void* content = kallocate(out_asset->size, MEMORY_TAG_ASSET);
    kcopy_memory(content, data.bytes, out_asset->size);
    out_asset->content = content;

    return out_asset;
}

void asset_system_release_binary(struct asset_system_state* state, kasset_binary* asset) {
    if (state && asset) {
        if (asset->content && asset->size) {
            kfree((void*)asset->content, asset->size, MEMORY_TAG_ASSET);
        }
        KFREE_TYPE(asset, kasset_binary, MEMORY_TAG_ASSET);
    }
}

// ////////////////////////////////////
// TEXT ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_text* asset_system_request_text_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_text_from_package_sync(state, state->default_package_name_str, name);
}
// sync load from specific package.
kasset_text* asset_system_request_text_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_text* out_asset = KALLOC_TYPE(kasset_text, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    out_asset->content = string_duplicate(data.text);

    return out_asset;
}

void asset_system_release_text(struct asset_system_state* state, kasset_text* asset) {
    if (state && asset) {
        if (asset->content) {
            string_free(asset->content);
        }
        KFREE_TYPE(asset, kasset_text, MEMORY_TAG_ASSET);
    }
}

// ////////////////////////////////////
// IMAGE ASSETS
// ////////////////////////////////////

typedef struct kasset_image_vfs_context {
    void* listener;
    PFN_kasset_image_loaded_callback callback;
    kasset_image* asset;
} kasset_image_vfs_context;

static void vfs_on_image_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data) {
    kasset_image_vfs_context* context = asset_data.context;
    kasset_image* out_asset = context->asset;
    b8 result = kasset_image_deserialize(asset_data.size, asset_data.bytes, out_asset);
    if (!result) {
        KERROR("Failed to deserialize image asset. See logs for details.");
    }

    if (context->callback) {
        context->callback(context->listener, out_asset);
    }
}

// async load from game package.
kasset_image* asset_system_request_image(struct asset_system_state* state, const char* name, b8 flip_y, void* listener, PFN_kasset_image_loaded_callback callback) {
    return asset_system_request_image_from_package(state, state->default_package_name_str, name, flip_y, listener, callback);
}
// sync load from game package.
kasset_image* asset_system_request_image_sync(struct asset_system_state* state, const char* name, b8 flip_y) {
    return asset_system_request_image_from_package_sync(state, state->default_package_name_str, name, flip_y);
}
// async load from specific package.
kasset_image* asset_system_request_image_from_package(struct asset_system_state* state, const char* package_name, const char* name, b8 flip_y, void* listener, PFN_kasset_image_loaded_callback callback) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_image* out_asset = KALLOC_TYPE(kasset_image, MEMORY_TAG_ASSET);

    kasset_image_vfs_context* context = KALLOC_TYPE(kasset_image_vfs_context, MEMORY_TAG_ASSET);
    context->asset = out_asset;
    context->callback = callback;
    context->listener = listener;

    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
        .vfs_callback = vfs_on_image_asset_loaded_callback,
        .context = context,
        .context_size = sizeof(kasset_image_vfs_context)};
    vfs_request_asset(state->vfs, info);

    return out_asset;
}
// sync load from specific package.
kasset_image* asset_system_request_image_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name, b8 flip_y) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_image* out_asset = KALLOC_TYPE(kasset_image, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_image_deserialize(data.size, data.bytes, out_asset);
    if (!result) {
        KERROR("Failed to deserialize image asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_image, MEMORY_TAG_ASSET);
        return 0;
    }

    return out_asset;
}

void asset_system_release_image(struct asset_system_state* state, kasset_image* asset) {
    if (state && asset) {
        if (asset->pixel_array_size && asset->pixels) {
            kfree((void*)asset->pixels, asset->pixel_array_size, MEMORY_TAG_ASSET);
        }
        KFREE_TYPE(asset, kasset_image, MEMORY_TAG_ASSET);
    }
}

// ////////////////////////////////////
// BITMAP FONT ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_bitmap_font* asset_system_request_bitmap_font_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_bitmap_font_from_package_sync(state, state->default_package_name_str, name);
}

// sync load from specific package.
kasset_bitmap_font* asset_system_request_bitmap_font_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_bitmap_font* out_asset = KALLOC_TYPE(kasset_bitmap_font, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_bitmap_font_deserialize(data.size, data.bytes, out_asset);
    if (!result) {
        KERROR("Failed to deserialize bitmap font asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_bitmap_font, MEMORY_TAG_ASSET);
        return 0;
    }

    return out_asset;
}

void asset_system_release_bitmap_font(struct asset_system_state* state, kasset_bitmap_font* asset) {
    if (state && asset) {
        array_kasset_bitmap_font_kerning_destroy(&asset->kernings);
        array_kasset_bitmap_font_glyph_destroy(&asset->glyphs);
        array_kasset_bitmap_font_page_destroy(&asset->pages);

        kzero_memory(asset, sizeof(kasset_bitmap_font));
    }
}

// ////////////////////////////////////
// SYSTEM FONT ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_system_font* asset_system_request_system_font_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_system_font_from_package_sync(state, state->default_package_name_str, name);
}

// sync load from specific package.
kasset_system_font* asset_system_request_system_font_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_system_font* out_asset = KALLOC_TYPE(kasset_system_font, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_system_font_deserialize(data.text, out_asset);
    if (!result) {
        KERROR("Failed to deserialize system font asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_system_font, MEMORY_TAG_ASSET);
        return 0;
    }

    // Load the font binary file.
    kasset_binary* ttf_binary_asset = asset_system_request_binary_from_package_sync(
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

void asset_system_release_system_font(struct asset_system_state* state, kasset_system_font* asset) {
    if (state && asset) {
        if (asset->faces && asset->face_count) {
            KFREE_TYPE_CARRAY(asset->faces, kasset_system_font_face, asset->face_count);
        }

        if (asset->font_binary && asset->font_binary_size) {
            kfree(asset->font_binary, asset->font_binary_size, MEMORY_TAG_ASSET);
        }

        kzero_memory(asset, sizeof(kasset_system_font));
    }
}

// ////////////////////////////////////
// STATIC MESH ASSETS
// ////////////////////////////////////

typedef struct kasset_static_mesh_vfs_context {
    void* listener;
    PFN_kasset_static_mesh_loaded_callback callback;
    kasset_static_mesh* asset;
} kasset_static_mesh_vfs_context;

static void vfs_on_static_mesh_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data) {
    kasset_static_mesh_vfs_context* context = asset_data.context;
    kasset_static_mesh* out_asset = context->asset;
    b8 result = kasset_static_mesh_deserialize(asset_data.size, asset_data.bytes, out_asset);
    if (!result) {
        KERROR("Failed to deserialize static_mesh asset. See logs for details.");
    }

    if (context->callback) {
        context->callback(context->listener, out_asset);
    }
}

// async load from game package.
kasset_static_mesh* asset_system_request_static_mesh(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_static_mesh_loaded_callback callback) {
    return asset_system_request_static_mesh_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_static_mesh* asset_system_request_static_mesh_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_static_mesh_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_static_mesh* asset_system_request_static_mesh_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_static_mesh_loaded_callback callback) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_static_mesh* out_asset = KALLOC_TYPE(kasset_static_mesh, MEMORY_TAG_ASSET);

    kasset_static_mesh_vfs_context* context = KALLOC_TYPE(kasset_static_mesh_vfs_context, MEMORY_TAG_ASSET);
    context->asset = out_asset;
    context->callback = callback;
    context->listener = listener;

    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = state->default_package_name,
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
        .vfs_callback = vfs_on_static_mesh_asset_loaded_callback,
        .context = context,
        .context_size = sizeof(kasset_static_mesh_vfs_context)};
    vfs_request_asset(state->vfs, info);

    return out_asset;
}
// sync load from specific package.
kasset_static_mesh* asset_system_request_static_mesh_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_static_mesh* out_asset = KALLOC_TYPE(kasset_static_mesh, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_static_mesh_deserialize(data.size, data.bytes, out_asset);
    if (!result) {
        KERROR("Failed to deserialize static_mesh asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_static_mesh, MEMORY_TAG_ASSET);
        return 0;
    }

    return out_asset;
}

void asset_system_release_static_mesh(struct asset_system_state* state, kasset_static_mesh* asset) {
    if (state && asset) {
        // Asset type-specific data cleanup
        if (asset->geometries && asset->geometry_count) {
            for (u32 i = 0; i < asset->geometry_count; ++i) {
                kasset_static_mesh_geometry* g = &asset->geometries[i];
                if (g->vertices && g->vertex_count) {
                    kfree(g->vertices, sizeof(g->vertices[0]) * g->vertex_count, MEMORY_TAG_ARRAY);
                }
                if (g->indices && g->index_count) {
                    kfree(g->indices, sizeof(g->indices[0]) * g->index_count, MEMORY_TAG_ARRAY);
                }
            }
            kfree(asset->geometries, sizeof(asset->geometries[0]) * asset->geometry_count, MEMORY_TAG_ARRAY);
        }
        KFREE_TYPE(asset, kasset_static_mesh, MEMORY_TAG_ASSET);
    }
}

// ////////////////////////////////////
// HEIGHTMAP TERRAIN ASSETS
// ////////////////////////////////////

typedef struct kasset_heightmap_terrain_vfs_context {
    void* listener;
    PFN_kasset_heightmap_terrain_loaded_callback callback;
    kasset_heightmap_terrain* asset;
} kasset_heightmap_terrain_vfs_context;

static void vfs_on_heightmap_terrain_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data) {
    kasset_heightmap_terrain_vfs_context* context = asset_data.context;
    b8 result = kasset_heightmap_terrain_deserialize(asset_data.text, context->asset);
    if (!result) {
        KERROR("Failed to deserialize heightmap_terrain asset. See logs for details.");
    }

    KFREE_TYPE(context, kasset_heightmap_terrain_vfs_context, MEMORY_TAG_ASSET);
}

// async load from game package.
kasset_heightmap_terrain* asset_system_request_heightmap_terrain(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_heightmap_terrain_loaded_callback callback) {
    return asset_system_request_heightmap_terrain_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_heightmap_terrain* asset_system_request_heightmap_terrain_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_heightmap_terrain_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_heightmap_terrain* asset_system_request_heightmap_terrain_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_heightmap_terrain_loaded_callback callback) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_heightmap_terrain* out_asset = KALLOC_TYPE(kasset_heightmap_terrain, MEMORY_TAG_ASSET);

    kasset_heightmap_terrain_vfs_context* context = KALLOC_TYPE(kasset_heightmap_terrain_vfs_context, MEMORY_TAG_ASSET);
    context->asset = out_asset;
    context->callback = callback;
    context->listener = listener;

    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = state->default_package_name,
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
        .vfs_callback = vfs_on_heightmap_terrain_asset_loaded_callback,
        .context = context,
        .context_size = sizeof(kasset_heightmap_terrain_vfs_context)};
    vfs_request_asset(state->vfs, info);

    return out_asset;
}
// sync load from specific package.
kasset_heightmap_terrain* asset_system_request_heightmap_terrain_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_heightmap_terrain* out_asset = KALLOC_TYPE(kasset_heightmap_terrain, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
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

void asset_system_release_heightmap_terrain(struct asset_system_state* state, kasset_heightmap_terrain* asset) {
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
// MATERIAL ASSETS
// ////////////////////////////////////

typedef struct kasset_material_vfs_context {
    void* listener;
    PFN_kasset_material_loaded_callback callback;
    kasset_material* asset;
} kasset_material_vfs_context;

static void vfs_on_material_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data) {
    kasset_material_vfs_context* context = asset_data.context;
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
kasset_material* asset_system_request_material(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_material_loaded_callback callback) {
    return asset_system_request_material_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_material* asset_system_terrain_request_material_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_material_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_material* asset_system_request_material_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_material_loaded_callback callback) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_material* out_asset = KALLOC_TYPE(kasset_material, MEMORY_TAG_ASSET);

    kasset_material_vfs_context* context = KALLOC_TYPE(kasset_material_vfs_context, MEMORY_TAG_ASSET);
    context->asset = out_asset;
    context->callback = callback;
    context->listener = listener;

    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = state->default_package_name,
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
        .vfs_callback = vfs_on_material_asset_loaded_callback,
        .context = context,
        .context_size = sizeof(kasset_material_vfs_context)};
    vfs_request_asset(state->vfs, info);

    return out_asset;
}
// sync load from specific package.
kasset_material* asset_system_request_material_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_material* out_asset = KALLOC_TYPE(kasset_material, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_material_deserialize(data.text, out_asset);
    if (!result) {
        KERROR("Failed to deserialize material asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_material, MEMORY_TAG_ASSET);
        return 0;
    }

    out_asset->name = info.asset_name;

    return out_asset;
}

void asset_system_release_material(struct asset_system_state* state, kasset_material* asset) {
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
    void* listener;
    PFN_kasset_audio_loaded_callback callback;
    kasset_audio* asset;
} kasset_audio_vfs_context;

static void vfs_on_audio_asset_loaded_callback(struct vfs_state* vfs, vfs_asset_data asset_data) {
    kasset_audio_vfs_context* context = asset_data.context;
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
kasset_audio* asset_system_request_audio(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_audio_loaded_callback callback) {
    return asset_system_request_audio_from_package(state, state->default_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_audio* asset_system_terrain_request_audio_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_audio_from_package_sync(state, state->default_package_name_str, name);
}
// async load from specific package.
kasset_audio* asset_system_request_audio_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_audio_loaded_callback callback) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_audio* out_asset = KALLOC_TYPE(kasset_audio, MEMORY_TAG_ASSET);

    kasset_audio_vfs_context* context = KALLOC_TYPE(kasset_audio_vfs_context, MEMORY_TAG_ASSET);
    context->asset = out_asset;
    context->callback = callback;
    context->listener = listener;

    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = state->default_package_name,
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
        .vfs_callback = vfs_on_audio_asset_loaded_callback,
        .context = context,
        .context_size = sizeof(kasset_audio_vfs_context)};
    vfs_request_asset(state->vfs, info);

    return out_asset;
}
// sync load from specific package.
kasset_audio* asset_system_request_audio_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_audio* out_asset = KALLOC_TYPE(kasset_audio, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = true,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_audio_deserialize(data.size, data.bytes, out_asset);
    if (!result) {
        KERROR("Failed to deserialize audio asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_audio, MEMORY_TAG_ASSET);
        return 0;
    }

    out_asset->name = info.asset_name;

    return out_asset;
}

void asset_system_release_audio(struct asset_system_state* state, kasset_audio* asset) {
    if (state && asset) {
        // Asset type-specific data cleanup
        if (asset->pcm_data_size && asset->pcm_data) {
            kfree(asset->pcm_data, asset->pcm_data_size, MEMORY_TAG_ASSET);
        }

        KFREE_TYPE(asset, kasset_audio, MEMORY_TAG_ASSET);
    }
}

// ////////////////////////////////////
// SCENE ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_scene* asset_system_terrain_request_scene_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_scene_from_package_sync(state, state->default_package_name_str, name);
}
// sync load from specific package.
kasset_scene* asset_system_request_scene_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_scene* out_asset = KALLOC_TYPE(kasset_scene, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_scene_deserialize(data.text, out_asset);
    if (!result) {
        KERROR("Failed to deserialize scene asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_scene, MEMORY_TAG_ASSET);
        return 0;
    }

    out_asset->name = info.asset_name;

    return out_asset;
}

static void kasset_scene_destroy_node(scene_node_config* node) {

    // Descroy attachments by type
    if (node->skybox_configs) {
        darray_destroy(node->skybox_configs);
        node->skybox_configs = 0;
    }

    if (node->dir_light_configs) {
        darray_destroy(node->dir_light_configs);
        node->dir_light_configs = 0;
    }
    if (node->point_light_configs) {
        darray_destroy(node->point_light_configs);
        node->point_light_configs = 0;
    }
    if (node->static_mesh_configs) {
        darray_destroy(node->static_mesh_configs);
        node->static_mesh_configs = 0;
    }
    if (node->heightmap_terrain_configs) {
        darray_destroy(node->heightmap_terrain_configs);
        node->heightmap_terrain_configs = 0;
    }
    if (node->water_plane_configs) {
        darray_destroy(node->water_plane_configs);
        node->water_plane_configs = 0;
    }

    // Destroy child nodes.
    for (u32 i = 0; i < node->child_count; ++i) {
        kasset_scene_destroy_node(&node->children[i]);
    }
    kfree(node->children, sizeof(scene_node_config) * node->child_count, MEMORY_TAG_ARRAY);
    node->child_count = 0;
    node->children = 0;
}

void asset_system_release_scene(struct asset_system_state* state, kasset_scene* asset) {
    if (state && asset) {
        // Asset type-specific data cleanup
        if (asset->description) {
            string_free(asset->description);
        }
        if (asset->node_count && asset->nodes) {
            for (u32 i = 0; i < asset->node_count; ++i) {
                kasset_scene_destroy_node(&asset->nodes[i]);
            }
            kfree(asset->nodes, sizeof(scene_node_config) * asset->node_count, MEMORY_TAG_ARRAY);
        }

        KFREE_TYPE(asset, kasset_scene, MEMORY_TAG_ASSET);
    }
}

// ////////////////////////////////////
// SHADER ASSETS
// ////////////////////////////////////

// sync load from game package.
kasset_shader* asset_system_terrain_request_shader_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_shader_from_package_sync(state, state->default_package_name_str, name);
}

// sync load from specific package.
kasset_shader* asset_system_request_shader_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name) {
    if (!state || !name || !string_length(name)) {
        KERROR("%s requires valid pointers to state and name.", __FUNCTION__);
        return 0;
    }

    kasset_shader* out_asset = KALLOC_TYPE(kasset_shader, MEMORY_TAG_ASSET);
    vfs_request_info info = {
        .asset_name = kname_create(name),
        .package_name = kname_create(package_name),
        .get_source = false,
        .is_binary = false,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_shader_deserialize(data.text, out_asset);
    if (!result) {
        KERROR("Failed to deserialize shader asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_shader, MEMORY_TAG_ASSET);
        return 0;
    }

    out_asset->name = info.asset_name;

    return out_asset;
}

void asset_system_release_shader(struct asset_system_state* state, kasset_shader* asset) {
    if (state && asset) {
        // Asset type-specific data cleanup
        // Stages
        if (asset->stages && asset->stage_count) {
            for (u32 i = 0; i < asset->stage_count; ++i) {
                kasset_shader_stage* stage = &asset->stages[i];
                if (stage->source_asset_name) {
                    string_free(stage->source_asset_name);
                }
                if (stage->package_name) {
                    string_free(stage->package_name);
                }
            }
            kfree(asset->stages, sizeof(kasset_shader_stage) * asset->stage_count, MEMORY_TAG_ARRAY);
            asset->stages = 0;
            asset->stage_count = 0;
        }

        // Attributes
        if (asset->attributes && asset->attribute_count) {
            for (u32 i = 0; i < asset->attribute_count; ++i) {
                kasset_shader_attribute* attrib = &asset->attributes[i];
                if (attrib->name) {
                    string_free(attrib->name);
                }
            }
            kfree(asset->attributes, sizeof(kasset_shader_attribute) * asset->attribute_count, MEMORY_TAG_ARRAY);
            asset->attributes = 0;
            asset->attribute_count = 0;
        }

        // Uniforms
        if (asset->uniforms && asset->uniform_count) {
            for (u32 i = 0; i < asset->uniform_count; ++i) {
                kasset_shader_uniform* attrib = &asset->uniforms[i];
                if (attrib->name) {
                    string_free(attrib->name);
                }
            }
            kfree(asset->uniforms, sizeof(kasset_shader_uniform) * asset->uniform_count, MEMORY_TAG_ARRAY);
            asset->uniforms = 0;
            asset->uniform_count = 0;
        }

        KFREE_TYPE(asset, kasset_shader, MEMORY_TAG_ASSET);
    }
}
