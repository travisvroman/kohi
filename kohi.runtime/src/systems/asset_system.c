#include "asset_system.h"
#include "core/engine.h"
// Known handler types
#include "assets/handlers/asset_handler_audio.h"
#include "assets/handlers/asset_handler_binary.h"
#include "assets/handlers/asset_handler_heightmap_terrain.h"
#include "assets/handlers/asset_handler_kson.h"
#include "assets/handlers/asset_handler_material.h"
#include "assets/handlers/asset_handler_scene.h"
#include "assets/handlers/asset_handler_shader.h"
#include "assets/handlers/asset_handler_text.h"
#include "platform/vfs.h"
#include "serializers/kasset_bitmap_font_serializer.h"
#include "serializers/kasset_image_serializer.h"
#include "serializers/kasset_static_mesh_serializer.h"
#include "serializers/kasset_system_font_serializer.h"

#include <assets/asset_handler_types.h>
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
    kname application_package_name;
    const char* application_package_name_str;

    // Max number of assets that can be loaded at any given time.
    u32 max_asset_count;
    // An array of lookups which contain reference and release data.
    asset_lookup* lookups;
    // A BST to use for lookups of assets by name.
    bt_node* lookup_tree;

    // An array of handlers for various asset types.
    asset_handler handlers[KASSET_TYPE_MAX];
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

    // application_package_name
    if (!kson_object_property_value_get_string(&tree.root, "application_package_name", &out_config->application_package_name_str)) {
        KERROR("application_package_name is a required field and was not provided.");
        return false;
    }
    out_config->application_package_name = kname_create(out_config->application_package_name_str);

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

    state->application_package_name = config->application_package_name;
    state->application_package_name_str = string_duplicate(config->application_package_name_str);

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

    // Setup handlers for known types.
    asset_handler_heightmap_terrain_create(&state->handlers[KASSET_TYPE_HEIGHTMAP_TERRAIN], state->vfs);
    asset_handler_material_create(&state->handlers[KASSET_TYPE_MATERIAL], state->vfs);
    asset_handler_text_create(&state->handlers[KASSET_TYPE_TEXT], state->vfs);
    asset_handler_kson_create(&state->handlers[KASSET_TYPE_KSON], state->vfs);
    asset_handler_binary_create(&state->handlers[KASSET_TYPE_BINARY], state->vfs);
    asset_handler_scene_create(&state->handlers[KASSET_TYPE_SCENE], state->vfs);
    asset_handler_shader_create(&state->handlers[KASSET_TYPE_SHADER], state->vfs);
    asset_handler_audio_create(&state->handlers[KASSET_TYPE_AUDIO], state->vfs);

    // Register for hot-reload/deleted events.
    vfs_hot_reload_callbacks_register(state->vfs, state, asset_hot_reloaded_callback, state, asset_deleted_callback);

    return true;
}

void asset_system_shutdown(struct asset_system_state* state) {
    if (state) {
        if (state->lookups) {
            // Unload all currently-held lookups.
            for (u32 i = 0; i < state->max_asset_count; ++i) {
                asset_lookup* lookup = &state->lookups[i];
                if (lookup->asset) {
                    // Force release the asset.
                    asset_system_release_internal(state, lookup->asset->name, lookup->asset->package_name, true);
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
    return asset_system_request_binary_from_package(state, state->application_package_name_str, name, listener, callback);
}

// sync load from game package.
kasset_binary* asset_system_request_binary_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_binary_from_package_sync(state, state->application_package_name_str, name);
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
        .package_name = state->application_package_name,
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

    KFREE_TYPE(context, kasset_image_vfs_context, MEMORY_TAG_ASSET);
}

// async load from game package.
kasset_image* asset_system_request_image(struct asset_system_state* state, const char* name, b8 flip_y, void* listener, PFN_kasset_image_loaded_callback callback) {
    return asset_system_request_image_from_package(state, state->application_package_name_str, name, flip_y, listener, callback);
}
// sync load from game package.
kasset_image* asset_system_request_image_sync(struct asset_system_state* state, const char* name, b8 flip_y) {
    return asset_system_request_image_from_package_sync(state, state->application_package_name_str, name, flip_y);
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
        .package_name = state->application_package_name,
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
    return asset_system_request_bitmap_font_from_package_sync(state, state->application_package_name_str, name);
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
    return asset_system_request_system_font_from_package_sync(state, state->application_package_name_str, name);
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
        .is_binary = true,
        .watch_for_hot_reload = false,
    };
    vfs_asset_data data = vfs_request_asset_sync(state->vfs, info);

    b8 result = kasset_system_font_deserialize(data.text, out_asset);
    if (!result) {
        KERROR("Failed to deserialize system font asset. See logs for details.");
        KFREE_TYPE(out_asset, kasset_system_font, MEMORY_TAG_ASSET);
        return 0;
    }

    return out_asset;
}

void asset_system_release_system_font(struct asset_system_state* state, kasset_system_font* asset) {
    if (state && asset) {
        if (asset->faces && asset->face_count) {
            KFREE_TYPE_CARRAY(asset->faces, kasset_system_font_face, asset->face_count);
        }

        if (asset->font_binary && asset->font_binary_size) {
            kfree(asset->font_binary, asset->font_binary_size, MEMORY_TAG_RESOURCE);
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

    KFREE_TYPE(context, kasset_static_mesh_vfs_context, MEMORY_TAG_ASSET);
}

// async load from game package.
kasset_static_mesh* asset_static_mesh_request_static_mesh(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_static_mesh_loaded_callback callback) {
    return asset_system_request_static_mesh_from_package(state, state->application_package_name_str, name, listener, callback);
}
// sync load from game package.
kasset_static_mesh* asset_static_mesh_request_static_mesh_sync(struct asset_system_state* state, const char* name) {
    return asset_system_request_static_mesh_from_package_sync(state, state->application_package_name_str, name);
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
        .package_name = state->application_package_name,
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
