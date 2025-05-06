#include "texture_system.h"

#include "assets/kasset_types.h"
#include "containers/u64_bst.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "kresources/kresource_utils.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "resources/resource_types.h"
#include "runtime_defines.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"
#include "utils/render_type_utils.h"

typedef struct texture_system_state {
    texture_system_config config;

    // All registered textures. id=INVALID_ID means slot is "free"
    ktexture* textures;
    u16* texture_reference_counts;
    b8* auto_releases;

    // For quick lookups by name.
    bt_node* texture_name_lookup;

    ktexture* default_kresource_texture;
    ktexture* default_kresource_base_colour_texture;
    ktexture* default_kresource_specular_texture;
    ktexture* default_kresource_normal_texture;
    ktexture* default_kresource_mra_texture;
    ktexture* default_kresource_cube_texture;
    ktexture* default_kresource_water_normal_texture;
    ktexture* default_kresource_water_dudv_texture;

    // A convenience pointer to the renderer system state.
    struct renderer_system_state* renderer;

    struct asset_system_state* kasset_system;
} texture_system_state;

// Also used as result_data from job.
typedef struct texture_load_params {
    char* resource_name;
    kresource_texture* out_texture;
    kresource_texture temp_texture;
    resource image_resource;
} texture_load_params;

typedef struct texture_load_layered_params {
    char* name;
    u32 layer_count;
    char** layer_names;
    kresource_texture* out_texture;
} texture_load_layered_params;

typedef enum texture_load_job_code {
    TEXTURE_LOAD_JOB_CODE_FIRST_QUERY_FAILED,
    TEXTURE_LOAD_JOB_CODE_RESOURCE_LOAD_FAILED,
    TEXTURE_LOAD_JOB_CODE_RESOURCE_DIMENSION_MISMATCH,
} texture_load_job_code;

typedef struct texture_load_layered_result {
    char* name;
    u32 layer_count;
    kresource_texture* out_texture;
    u64 data_block_size;
    u8* data_block;
    kresource_texture temp_texture;
    texture_load_job_code result_code;
} texture_load_layered_result;

typedef struct texture_asset_load_listener_context {
    PFN_texture_loaded_callback user_callback;
    void* user_listener;
    ktexture* texture;
    // NOTE: size of array is texture->layer_count
    kasset_image** assets;
    kname name;
    const char** image_asset_names;
    const char** package_names;
    ktexture_load_options options;
} texture_asset_load_listener_context;

// FIXME: remove this and its dependencies.
static texture_system_state* state_ptr = 0;

// TODO: remove old functions.
static b8 create_default_textures(texture_system_state* state);
static void release_default_textures(texture_system_state* state);
static void increment_generation(kresource_texture* t);
static void invalidate_texture(kresource_texture* t);
static b8 is_default_texture(texture_system_state* state, kresource_texture* t);

static kresource_texture* default_texture_by_name(texture_system_state* state, kname name);
static kresource_texture* request_writeable_arrayed(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, texture_type type, u16 array_size, b8 is_depth, b8 is_stencil, b8 multiframe_buffering);

// NOTE: new functions
static void texture_kasset_image_loaded(void* listener, kasset_image* asset);
static ktexture* texture_get_if_exists(kname name);
static ktexture* texture_get_new(kname name);
static b8 texture_resources_acquire(ktexture* t, kname name);
static void texture_cleanup(ktexture* t, b8 clear_references);
static b8 get_image_asset_names_from_options(const ktexture_load_options* options, u16* out_count, const char*** image_asset_names, const char*** package_names);
static void combine_asset_pixel_data(kasset_image** assets, u32 count, u32 expected_width, u32 expected_height, b8 release_assets, u32* out_size, void** out_pixels);
static b8 texture_apply_asset_data(ktexture* t, kname name, const ktexture_load_options* options, kasset_image** assets);

b8 texture_system_initialize(u64* memory_requirement, void* state, void* config) {
    texture_system_config* typed_config = (texture_system_config*)config;
    if (typed_config->max_texture_count == 0) {
        KFATAL("texture_system_initialize - config.max_texture_count must be > 0.");
        return false;
    }

    *memory_requirement = sizeof(texture_system_state);

    if (!state) {
        return true;
    }

    KDEBUG("Initializing texture system...");

    state_ptr = state;
    state_ptr->config = *typed_config;

    // Setup texture cache.
    state_ptr->textures = KALLOC_TYPE_CARRAY(ktexture, typed_config->max_texture_count);
    state_ptr->texture_reference_counts = KALLOC_TYPE_CARRAY(u16, typed_config->max_texture_count);
    state_ptr->auto_releases = KALLOC_TYPE_CARRAY(b8, typed_config->max_texture_count);

    // Auto-release and texture reference counts default to false/0.
    for (u32 i = 0; i < typed_config->max_texture_count; ++i) {
        state_ptr->textures[i].id = INVALID_ID;
    }

    // Keep a pointer to the renderer system state.
    state_ptr->renderer = engine_systems_get()->renderer_system;
    state_ptr->kasset_system = engine_systems_get()->asset_state;

    // Create default textures for use in the system.
    create_default_textures(state_ptr);

    KDEBUG("Texture system initialization complete.");

    return true;
}

void texture_system_shutdown(void* state) {
    if (state_ptr) {
        release_default_textures(state_ptr);

        state_ptr->renderer = 0;
        state_ptr = 0;
    }
}

// LEFTOFF: new

ktexture* texture_acquire(const char* image_asset_name, void* listener, PFN_texture_loaded_callback callback) {
    return texture_acquire_async_internal(image_asset_name, 0, listener, callback);
}

// auto_release=true, default options
ktexture* texture_acquire_sync(const char* image_asset_name) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .auto_release = true,
        .name = image_asset_name,
        .image_asset_name = image_asset_name,
        .package_name = 0};
    return texture_acquire_with_options_sync(options);
}

// auto_release=true, default options
ktexture* texture_acquire_from_package(const char* image_asset_name, const char* package_name, void* listener, PFN_texture_loaded_callback callback) {
    return texture_acquire_async_internal(image_asset_name, package_name, listener, callback);
}

ktexture* texture_acquire_from_package_sync(const char* image_asset_name, const char* package_name) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .auto_release = true,
        .name = image_asset_name,
        .image_asset_name = image_asset_name,
        .package_name = package_name};
    return texture_acquire_with_options_sync(options);
}

ktexture* texture_cubemap_acquire(const char* image_asset_name_prefix) {
}
// auto_release=true, default options
ktexture* texture_cubemap_acquire_from_package(const char* image_asset_name_prefix, const char* package_name) {
}

// Easier idea? synchronous. auto_release=true, default options
ktexture* texture_acquire_from_image(const struct kasset_image* image) {
}

ktexture* texture_cubemap_acquire_from_images(const struct kasset_image* images[6]) {
}

ktexture* texture_acquire_with_options(ktexture_load_options options, void* listener, PFN_texture_loaded_callback callback) {
    if ((!options.name || !string_length(options.name)) && (!options.image_asset_name || !string_length(options.image_asset_name))) {
        KERROR("%s - Either name or image_asset_name is required.", __FUNCTION__);
        return 0;
    }

    b8 success = false;
    kname name = kname_create(options.name ? options.name : options.image_asset_name);
    ktexture* t = texture_get_if_exists(name);

    // If an entry with the name exists, return it.
    if (t) {
        // Increment reference count.
        state_ptr->texture_reference_counts[t->id]++;

        // Immediately make the user callback, and boot.
        if (callback) {
            callback(t, listener);
        }
        return t;
    }

    // Pick a free slot in the texture cache.
    t = texture_get_new(name);
    if (!t) {
        goto texture_acquire_with_options_async_cleanup;
    }

    // Set some default properties.
    t->format = options.format;
    t->type = options.type;
    t->width = options.width;
    t->height = options.height;
    t->mip_levels = options.mip_levels;

    // Parse flags from options booleans.
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_DEPTH, options.is_depth);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_STENCIL, options.is_stencil);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_IS_WRITEABLE, options.is_writeable);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_RENDERER_BUFFERING, options.multiframe_buffering);

    state_ptr->auto_releases[t->id] = options.auto_release;

    const char** image_asset_names = 0;
    const char** package_names = 0;
    if (!get_image_asset_names_from_options(&options, &t->array_size, &image_asset_names, &package_names)) {
        goto texture_acquire_with_options_async_cleanup;
    }

    // If there are assets, need to break off into "async callback" logic, and handle resource acquisition and GPU upload later.
    if (image_asset_names) {
        // Setup a listener context for the async callback to use.
        texture_asset_load_listener_context* context = KALLOC_TYPE(texture_asset_load_listener_context, MEMORY_TAG_TEXTURE);
        context->texture = t;
        context->image_asset_names = image_asset_names;
        context->package_names = package_names;
        context->user_listener = listener;
        context->user_callback = callback;
        context->name = name;
        context->options = options;
        context->assets = KALLOC_TYPE_CARRAY(kasset_image*, t->array_size);

        // Fetch assets.
        for (u16 i = 0; i < t->array_size; ++i) {
            if (package_names[i]) {
                context->assets[i] = asset_system_request_image_from_package(state_ptr->kasset_system, image_asset_names[i], package_names[i], false, context, texture_kasset_image_loaded);
            } else {
                context->assets[i] = asset_system_request_image(state_ptr->kasset_system, image_asset_names[i], false, context, texture_kasset_image_loaded);
            }
            if (!context->assets[i]) {
                // NOTE: Continue to load other images instead of booting here.
                KERROR("%s - Asset named '%s' does not exist, thus a texture cannot be loaded from it.", __FUNCTION__, options.image_asset_name);
            }
        }

        // NOTE: Return pointer to texture here because all of the resource acquisition and pixel
        // data upload must be done after all assets are loaded.
        return t;
    }

    // TODO: handle pixel data
}

ktexture* texture_acquire_with_options_sync(ktexture_load_options options) {
    if ((!options.name || !string_length(options.name)) && (!options.image_asset_name || !string_length(options.image_asset_name))) {
        KERROR("%s - Either name or image_asset_name is required.", __FUNCTION__);
        return 0;
    }

    b8 success = false;
    kname name = kname_create(options.name ? options.name : options.image_asset_name);
    ktexture* t = texture_get_if_exists(name);

    // If an entry with the name exists, return it.
    if (t) {
        // Increment reference count.
        state_ptr->texture_reference_counts[t->id]++;
        return t;
    }

    // Pick a free slot in the texture cache.
    t = texture_get_new(name);
    if (!t) {
        goto texture_acquire_with_options_sync_cleanup;
    }

    // Set some default properties.
    t->format = options.format;
    t->type = options.type;
    t->width = options.width;
    t->height = options.height;
    t->mip_levels = options.mip_levels;

    // Parse flags from options booleans.
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_DEPTH, options.is_depth);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_STENCIL, options.is_stencil);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_IS_WRITEABLE, options.is_writeable);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_RENDERER_BUFFERING, options.multiframe_buffering);

    kasset_image** assets = 0;

    // Gather asset/package names, if relevant.
    const char** image_asset_names = 0;
    const char** package_names = 0;
    if (!get_image_asset_names_from_options(&options, &t->array_size, &image_asset_names, &package_names)) {
        goto texture_acquire_with_options_sync_cleanup;
    }

    if (image_asset_names) {
        // Fetch assets.
        assets = KALLOC_TYPE_CARRAY(kasset_image*, t->array_size);
        for (u16 i = 0; i < t->array_size; ++i) {
            if (package_names[i]) {
                assets[i] = asset_system_request_image_from_package_sync(state_ptr->kasset_system, image_asset_names[i], package_names[i], false);
            } else {
                assets[i] = asset_system_request_image_sync(state_ptr->kasset_system, image_asset_names[i], false);
            }
            if (!assets[i]) {
                // NOTE: Continue to load other images instead of booting here.
                KERROR("%s - Asset named '%s' does not exist, thus a texture cannot be loaded from it.", __FUNCTION__, options.image_asset_name);
            }
        }

        // Take the dimensions of the first asset as the size for layered images.
        if (assets[0]) {
            t->width = assets[0]->width;
            t->height = assets[0]->height;
            t->mip_levels = assets[0]->mip_levels;
        } else {
            KWARN("Asset sub 0 not found, using reasonable defaults.");
            // Provide reasonable defaults.
            if (!t->width) {
                t->width = 16;
            }
            if (!t->height) {
                t->height = 16;
            }
        }
    }

    // Calculate mip levels if needed.
    if (!t->mip_levels) {
        t->mip_levels = calculate_mip_levels_from_dimension(t->width, t->height);
    }

    state_ptr->auto_releases[t->id] = options.auto_release;

    if (!texture_resources_acquire(t, name)) {
        KERROR("%s - Failed to acquire renderer texture resources for texture '%s'", __FUNCTION__, options.image_asset_name);
        goto texture_acquire_with_options_sync_cleanup;
    }

    // Load pixel/asset pixel data.
    u32 all_pixel_size = 0;
    // TODO: This will be an issue with any other bit depth than 8
    u8* all_pixels = 0;
    b8 free_pixels = false;
    if (options.pixel_array_size && options.pixel_data) {
        all_pixels = options.pixel_data;
        all_pixel_size = options.pixel_array_size;
    } else if (assets) {
        free_pixels = true;
        combine_asset_pixel_data(assets, t->array_size, t->width, t->height, true, &all_pixel_size, (void*)&all_pixels);
    }

    // Determine transparency.
    b8 has_transparency = pixel_data_has_transparency(all_pixels, all_pixel_size, t->format);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_HAS_TRANSPARENCY, has_transparency);

    // Write the image asset data to the texture.
    u32 texture_data_offset = 0; // NOTE: The only time this potentially could be nonzero is when explicitly loading a layer of texture data.
    b8 write_result = renderer_texture_write_data(
        state_ptr->renderer,
        t->renderer_texture_handle,
        texture_data_offset, all_pixel_size, all_pixels);

    if (!write_result) {
        KERROR("%s - Failed to write texture data resource '%s'.", __FUNCTION__, options.image_asset_name);
        goto texture_acquire_with_options_sync_cleanup;
    }

    success = true;
texture_acquire_with_options_sync_cleanup:

    if (all_pixels && free_pixels) {
        kfree(all_pixels, all_pixel_size, MEMORY_TAG_ARRAY);
    }
    if (t) {
        if (assets) {
            KFREE_TYPE_CARRAY(assets, kasset_image, t->array_size);
        }
        string_cleanup_array(image_asset_names, t->array_size);
        string_cleanup_array(package_names, t->array_size);
    }

    if (!success) {
        texture_cleanup(t, true);
        t = 0;
    }

    return t;
}

void texture_release(ktexture* texture) {
    if (texture && texture->id != INVALID_ID) {
        if (state_ptr->texture_reference_counts[texture->id] > 0) {
            state_ptr->texture_reference_counts[texture->id]--;

            if (state_ptr->texture_reference_counts[texture->id] == 0 && state_ptr->auto_releases[texture->id] == true) {
                texture_cleanup(texture, true);
            }
        } else {
            KWARN("Texture id %u has no references and cannot be released.", texture->id);
        }
    }
}

// TODO: old - remove
kresource_texture* texture_system_request(kname name, kname package_name, void* listener, PFN_resource_loaded_user_callback callback) {
    texture_system_state* state = engine_systems_get()->texture_system;

    // Check that name is not the name of a default texture. If it is, immediately
    // make the callback with the appropriate default texture and return it
    kresource_texture* t = default_texture_by_name(state, name);
    if (t) {
        if (callback) {
            callback((kresource*)t, listener);
        }
        return t;
    }
    // If not default, request the resource from the resource system.
    kresource_texture_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.base.listener_inst = listener;
    request.base.user_callback = callback;

    request.base.assets = array_kresource_asset_info_create(1);
    request.base.assets.data[0].type = KASSET_TYPE_IMAGE;
    request.base.assets.data[0].package_name = package_name;
    request.base.assets.data[0].asset_name = name;

    request.array_size = 1;
    request.texture_type = TEXTURE_TYPE_2D;
    request.flags = 0;
    request.flip_y = true;

    t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to properly request resource for texture '%s'.", kname_string_get(name));
    }

    return t;
}

kresource_texture* texture_system_request_cube(kname name, b8 auto_release, b8 multiframe_buffering, void* listener, PFN_resource_loaded_user_callback callback) {
    texture_system_state* state = engine_systems_get()->texture_system;

    // If requesting the default cube texture name, just return it.
    if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    }

    if (name == INVALID_KNAME) {
        KWARN("texture_system_request_cube - name supplied is invalid. Returning default cubemap instead.");
        return state->default_kresource_cube_texture;
    }

    // If not default, request the resource from the resource system.
    kresource_texture_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.base.listener_inst = listener;
    request.base.user_callback = callback;

    request.base.assets = array_kresource_asset_info_create(6);

    // +X,-X,+Y,-Y,+Z,-Z in _cubemap_ space, which is LH y-down
    // Build out image side asset names. Order is important here.
    // name_r Right
    // name_l Left
    // name_u Up
    // name_d Down
    // name_f Front
    // name_b Back
    const char* sides = "rludfb";
    const char* base_name = kname_string_get(name);
    for (u8 i = 0; i < 6; ++i) {
        const char* buf = string_format("%s_%c", base_name, sides[i]);
        kname side_name = kname_create(buf);
        string_free(buf);

        request.base.assets.data[i].type = KASSET_TYPE_IMAGE;
        request.base.assets.data[i].package_name = INVALID_KNAME; // TODO: automatic package name?
        request.base.assets.data[i].asset_name = side_name;
    }

    request.array_size = 6;
    request.texture_type = TEXTURE_TYPE_CUBE;
    request.flags = multiframe_buffering ? TEXTURE_FLAG_RENDERER_BUFFERING : 0;
    request.flip_y = false;

    kresource_texture* t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to properly request resource for cube texture '%s'.", kname_string_get(name));
    }

    return t;
}

kresource_texture* texture_system_request_cube_writeable(kname name, u32 dimension, b8 auto_release, b8 multiframe_buffering) {
    texture_system_state* state = engine_systems_get()->texture_system;
    // If requesting the default cube texture name, just return it.
    if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    }
    if (name == INVALID_KNAME) {
        KWARN("texture_system_request_cube - name supplied is invalid. Returning default cubemap instead.");
        return state->default_kresource_cube_texture;
    }
    return request_writeable_arrayed(name, dimension, dimension, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_CUBE, 6, false, false, multiframe_buffering);
}

kresource_texture* texture_system_request_cube_depth(kname name, u32 dimension, b8 auto_release, b8 include_stencil, b8 multiframe_buffering) {
    texture_system_state* state = engine_systems_get()->texture_system;
    // If requesting the default cube texture name, just return it.
    if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    }
    if (name == INVALID_KNAME) {
        KWARN("texture_system_request_cube - name supplied is invalid. Returning default cubemap instead.");
        return state->default_kresource_cube_texture;
    }
    return request_writeable_arrayed(name, dimension, dimension, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_CUBE, 6, true, include_stencil, multiframe_buffering);
}

kresource_texture* texture_system_request_writeable(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, b8 multiframe_buffering) {
    return request_writeable_arrayed(name, width, height, format, has_transparency, TEXTURE_TYPE_2D, 1, false, false, multiframe_buffering);
}

kresource_texture* texture_system_request_writeable_arrayed(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, b8 multiframe_buffering, texture_type type, u16 array_size) {
    return request_writeable_arrayed(name, width, height, format, has_transparency, type, array_size, false, false, multiframe_buffering);
}

kresource_texture* texture_system_request_depth(kname name, u32 width, u32 height, b8 include_stencil, b8 multiframe_buffering) {
    return request_writeable_arrayed(name, width, height, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_2D, 1, true, include_stencil, multiframe_buffering);
}

kresource_texture* texture_system_request_depth_arrayed(kname name, u32 width, u32 height, u16 array_size, b8 include_stencil, b8 multiframe_buffering) {
    return request_writeable_arrayed(name, width, height, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_2D_ARRAY, array_size, true, include_stencil, multiframe_buffering);
}

kresource_texture* texture_system_acquire_textures_as_arrayed(kname name, kname package_name, u32 layer_count, kname* layer_asset_names, b8 auto_release, b8 multiframe_buffering, void* listener, PFN_resource_loaded_user_callback callback) {
    if (layer_count < 1) {
        KERROR("Must contain at least one layer.");
        return 0;
    }

    texture_system_state* state = engine_systems_get()->texture_system;

    // Check that name is not the name of a default texture. If it is, immediately
    // make the callback with the appropriate default texture and return it
    kresource_texture* t = default_texture_by_name(state, name);
    if (t) {
        if (callback) {
            callback((kresource*)t, listener);
        }
        return t;
    }
    // If not default, request the resource from the resource system.
    kresource_texture_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.base.listener_inst = listener;
    request.base.user_callback = callback;

    request.base.assets = array_kresource_asset_info_create(layer_count);
    for (u32 i = 0; i < layer_count; ++i) {
        kresource_asset_info* asset = &request.base.assets.data[i];
        asset->type = KASSET_TYPE_IMAGE;
        asset->package_name = package_name;
        asset->asset_name = layer_asset_names[i];
    }

    request.array_size = layer_count;
    request.texture_type = TEXTURE_TYPE_2D_ARRAY;
    request.flags = 0;
    request.flip_y = true;

    t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to properly request resource for arrayed texture '%s'.", kname_string_get(name));
    }

    return t;
}

void texture_system_release_resource(kresource_texture* t) {
    struct kresource_system_state* resource_system = engine_systems_get()->kresource_state;
    texture_system_state* state = engine_systems_get()->texture_system;

    // Do nothing if this is a default texture.
    if (is_default_texture(state, t)) {
        return;
    }

    kresource_system_release(resource_system, t->base.name);
}

b8 texture_system_resize(kresource_texture* t, u32 width, u32 height, b8 regenerate_internal_data) {
    if (t) {
        if (!(t->flags & TEXTURE_FLAG_IS_WRITEABLE)) {
            KWARN("texture_system_resize should not be called on textures that are not writeable.");
            return false;
        }
        t->width = width;
        t->height = height;
        // FIXME: remove this requirement, and potentially the regenerate_internal_data flag as well.
        // Only allow this for writeable textures that are not wrapped.
        // Wrapped textures can call texture_system_set_internal then call
        // this function to get the above parameter updates and a generation
        // update.
        if (!(t->flags & TEXTURE_FLAG_IS_WRAPPED) && regenerate_internal_data) {
            // Regenerate internals for the new size.
            b8 result = renderer_texture_resize(state_ptr->renderer, t->renderer_texture_handle, width, height);
            increment_generation(t);
            return result;
        }
        return true;
    }
    return false;
}

b8 texture_system_write_data(kresource_texture* t, u32 offset, u32 size, void* data) {
    if (t) {
        return renderer_texture_write_data(state_ptr->renderer, t->renderer_texture_handle, offset, size, data);
    }
    return false;
}

#define RETURN_TEXT_PTR_OR_NULL(texture, func_name)                                              \
    if (state_ptr) {                                                                             \
        return &texture;                                                                         \
    }                                                                                            \
    KERROR("%s called before texture system initialization! Null pointer returned.", func_name); \
    return 0;

static b8 is_default_texture(texture_system_state* state, kresource_texture* t) {
    if (!state_ptr) {
        return false;
    }
    return (t == state->default_kresource_texture) ||
           (t == state->default_kresource_base_colour_texture) ||
           (t == state->default_kresource_specular_texture) ||
           (t == state->default_kresource_normal_texture) ||
           (t == state->default_kresource_mra_texture) ||
           (t == state->default_kresource_cube_texture) ||
           (t == state->default_kresource_water_normal_texture) ||
           (t == state->default_kresource_water_dudv_texture);
}

kresource_texture* create_default_kresource_texture(texture_system_state* state, kname name, texture_type type, u32 tex_dimension, u8 layer_count, u8 channel_count, u32 pixel_array_size, u8* pixels) {
    kresource_texture_request_info request = {0};
    kzero_memory(&request, sizeof(kresource_texture_request_info));
    request.texture_type = type;
    request.array_size = layer_count;
    request.flags = TEXTURE_FLAG_IS_WRITEABLE;
    request.pixel_data = array_kresource_texture_pixel_data_create(1);
    kresource_texture_pixel_data* px = &request.pixel_data.data[0];
    px->pixel_array_size = pixel_array_size;
    px->pixels = pixels;
    px->width = tex_dimension;
    px->height = tex_dimension;
    px->channel_count = channel_count;
    px->format = TEXTURE_FORMAT_RGBA8;
    px->mip_levels = 1;
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.flip_y = false; // Doesn't really matter since there's no asset being loaded.
    kresource_texture* t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to request resources for default texture");
    }
    return t;
}

static b8 create_default_textures(texture_system_state* state) {
    // NOTE: Create default texture, a 256x256 blue/white checkerboard pattern.
    // This is done in code to eliminate asset dependencies.
    KTRACE("Creating default texture...");
    const u32 tex_dimension = 16;
    const u32 channels = 4;
    const u32 pixel_count = tex_dimension * tex_dimension;
    u8 pixels[16 * 16 * 4];
    kset_memory(pixels, 255, sizeof(u8) * pixel_count * channels);

    // Each pixel.
    for (u64 row = 0; row < tex_dimension; ++row) {
        for (u64 col = 0; col < tex_dimension; ++col) {
            u64 index = (row * tex_dimension) + col;
            u64 index_bpp = index * channels;
            if (row % 2) {
                if (col % 2) {
                    pixels[index_bpp + 0] = 0;
                    pixels[index_bpp + 1] = 0;
                }
            } else {
                if (!(col % 2)) {
                    pixels[index_bpp + 0] = 0;
                    pixels[index_bpp + 1] = 0;
                }
            }
        }
    }

    // Default texture
    {
        KTRACE("Creating default resource texture...");

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_texture = create_default_kresource_texture(state, kname_create(DEFAULT_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, pixels);
        if (!state->default_kresource_texture) {
            KERROR("Failed to request resources for default texture");
            return false;
        }
    }

    // Base colour texture.
    {
        KTRACE("Creating default base colour texture...");

        u8 diff_pixels[16 * 16 * 4];
        // Default diffuse map is all white.
        kset_memory(diff_pixels, 255, sizeof(u8) * 16 * 16 * 4);
        // Request new resource texture.

        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_base_colour_texture = create_default_kresource_texture(state, kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, diff_pixels);
        if (!state->default_kresource_base_colour_texture) {
            KERROR("Failed to request resources for default base colour texture");
            return false;
        }
    }

    // Specular texture.
    {
        KTRACE("Creating default specular texture...");
        u8 spec_pixels[16 * 16 * 4];
        // Default spec map is black (no specular)
        kset_memory(spec_pixels, 0, sizeof(u8) * 16 * 16 * 4);

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_specular_texture = create_default_kresource_texture(state, kname_create(DEFAULT_SPECULAR_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, spec_pixels);
        if (!state->default_kresource_specular_texture) {
            KERROR("Failed to request resources for default specular texture");
            return false;
        }
    }

    // Normal texture.
    u8 normal_pixels[16 * 16 * 4]; // w * h * channels
    {
        KTRACE("Creating default normal texture...");
        kset_memory(normal_pixels, 255, sizeof(u8) * 16 * 16 * 4);

        // Each pixel.
        for (u64 row = 0; row < 16; ++row) {
            for (u64 col = 0; col < 16; ++col) {
                u64 index = (row * 16) + col;
                u64 index_bpp = index * channels;
                // Set blue, z-axis by default and alpha.
                normal_pixels[index_bpp + 2] = 128;
                normal_pixels[index_bpp + 3] = 128;
            }
        }

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_normal_texture = create_default_kresource_texture(state, kname_create(DEFAULT_NORMAL_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, normal_pixels);
        if (!state->default_kresource_normal_texture) {
            KERROR("Failed to request resources for default normal texture");
            return false;
        }
    }

    // MRA texture
    u8 mra_pixels[16 * 16 * 4]; // w * h * channels
    {
        KTRACE("Creating default MRA (metallic, roughness, AO) texture...");
        kset_memory(mra_pixels, 255, sizeof(u8) * 16 * 16 * 4);

        // Each pixel.
        for (u64 row = 0; row < 16; ++row) {
            for (u64 col = 0; col < 16; ++col) {
                u64 index = (row * 16) + col;
                u64 index_bpp = index * channels;
                mra_pixels[index_bpp + 0] = 0;   // Default for metallic is black.
                mra_pixels[index_bpp + 1] = 128; // Default for roughness is medium grey
                mra_pixels[index_bpp + 2] = 255; // Default for AO is white.
            }
        }

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_mra_texture = create_default_kresource_texture(state, kname_create(DEFAULT_MRA_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, mra_pixels);
        if (!state->default_kresource_mra_texture) {
            KERROR("Failed to request resources for default MRA texture");
            return false;
        }
    }

    // Cube texture.
    {
        KTRACE("Creating default cube texture...");

        // New resource type
        const u32 tex_dimension = 16;
        const u32 channels = 4;
        const u32 layers = 6; // one per side.
        const u32 pixel_count = tex_dimension * tex_dimension;
        u8 cube_side_pixels[16 * 16 * 4];
        kset_memory(cube_side_pixels, 255, sizeof(u8) * pixel_count * channels);

        // Each pixel.
        for (u64 row = 0; row < tex_dimension; ++row) {
            for (u64 col = 0; col < tex_dimension; ++col) {
                u64 index = (row * tex_dimension) + col;
                u64 index_bpp = index * channels;
                if (row % 2) {
                    if (col % 2) {
                        cube_side_pixels[index_bpp + 1] = 0;
                        cube_side_pixels[index_bpp + 2] = 0;
                    }
                } else {
                    if (!(col % 2)) {
                        cube_side_pixels[index_bpp + 1] = 0;
                        cube_side_pixels[index_bpp + 2] = 0;
                    }
                }
            }
        }

        // Copy the image side data (same on all sides) to the relevant portion of the pixel array.
        u64 layer_size = sizeof(u8) * tex_dimension * tex_dimension * channels;
        u64 image_size = layer_size * layers;
        u8* pixels = kallocate(image_size, MEMORY_TAG_ARRAY);
        for (u8 i = 0; i < layers; ++i) {
            kcopy_memory(pixels + layer_size * i, cube_side_pixels, layer_size);
        }

        // Request new resource texture.?
        u32 pixel_array_size = image_size;
        state->default_kresource_cube_texture = create_default_kresource_texture(state, kname_create(DEFAULT_CUBE_TEXTURE_NAME), TEXTURE_TYPE_CUBE, tex_dimension, 6, channels, pixel_array_size, pixels);
        if (!state->default_kresource_cube_texture) {
            KERROR("Failed to request resources for default cube texture");
            return false;
        }
        kfree(pixels, image_size, MEMORY_TAG_ARRAY);
    }

    // FIXME: Should there even _be_ a default layered texture, or should they just
    // be created on the fly as needed?
    //
    // Default layered textures. 4 materials, 3 maps per, for 12 layers.
    // TODO: This should be a default layered texture of n layers, if anything.
    //
    /* {
        u32 layer_size = sizeof(u8) * 16 * 16 * 4;
        u32 terrain_material_count = 4;
        u32 terrain_per_material_map_count = 3;
        u32 layer_count = terrain_per_material_map_count * terrain_material_count;
        u8* terrain_pixels = kallocate(layer_size * layer_count, MEMORY_TAG_ARRAY);
        u32 material_size = layer_size * terrain_per_material_map_count;
        for (u32 i = 0; i < terrain_material_count; ++i) {
            // Albedo NOTE: purposefully using checkerboard here instead of default diffuse white;
            kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 0), pixels, layer_size);
            // Normal
            kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 1), normal_pixels, layer_size);
            // Combined
            kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 2), mra_pixels, layer_size);
        }

        // Request new resource texture.
        state->default_kresource_terrain_texture = create_default_kresource_texture(state, kname_create(DEFAULT_TERRAIN_TEXTURE_NAME), texture_TYPE_2D_ARRAY, tex_dimension, layer_count, channels, layer_size * layer_count, terrain_pixels);
        if (!state->default_kresource_terrain_texture) {
            KERROR("Failed to request resources for default terrain texture");
            return false;
        }
        kfree(terrain_pixels, layer_size * layer_count, MEMORY_TAG_ARRAY);
    } */

    // Default water normal texture is part of the runtime package - request it.
    state->default_kresource_water_normal_texture = texture_system_request(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME), kname_create(PACKAGE_NAME_RUNTIME), 0, 0);

    // Default water dudv texture is part of the runtime package - request it.
    state->default_kresource_water_dudv_texture = texture_system_request(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME), kname_create(PACKAGE_NAME_RUNTIME), 0, 0);

    return true;
}

static void release_default_textures(texture_system_state* state) {
    if (state) {
        kresource_system_release(state->kresource_system, state->default_kresource_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_base_colour_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_specular_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_normal_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_mra_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_cube_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_water_normal_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_water_dudv_texture->base.name);
    }
}

static void increment_generation(kresource_texture* t) {
    if (t) {
        t->base.generation++;
        // Ensure we don't land on invalid before rolling over.
        if (t->base.generation == INVALID_ID_U8) {
            t->base.generation = 0;
        }
    }
}

static void invalidate_texture(kresource_texture* t) {
    if (t) {
        kzero_memory(t, sizeof(kresource_texture));
        t->base.generation = INVALID_ID_U8;
        t->renderer_texture_handle = khandle_invalid();
    }
}

static kresource_texture* default_texture_by_name(texture_system_state* state, kname name) {
    if (name == state->default_kresource_texture->base.name) {
        return state->default_kresource_texture;
    } else if (name == state->default_kresource_base_colour_texture->base.name) {
        return state->default_kresource_base_colour_texture;
    } else if (name == state->default_kresource_normal_texture->base.name) {
        return state->default_kresource_normal_texture;
    } else if (name == state->default_kresource_specular_texture->base.name) {
        return state->default_kresource_specular_texture;
    } else if (name == state->default_kresource_mra_texture->base.name) {
        return state->default_kresource_mra_texture;
    } else if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    } else if (state->default_kresource_water_normal_texture && name == state->default_kresource_water_normal_texture->base.name) {
        return state->default_kresource_water_normal_texture;
    } else if (state->default_kresource_water_dudv_texture && name == state->default_kresource_water_dudv_texture->base.name) {
        return state->default_kresource_water_dudv_texture;
    }

    return 0;
}

static kresource_texture* request_writeable_arrayed(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, texture_type type, u16 array_size, b8 is_depth, b8 is_stencil, b8 multiframe_buffering) {

    struct kresource_system_state* kresource_system = engine_systems_get()->kresource_state;
    kresource_texture_request_info request = {0};
    kzero_memory(&request, sizeof(kresource_texture_request_info));
    request.texture_type = type;
    request.array_size = array_size;
    request.flags = TEXTURE_FLAG_IS_WRITEABLE;
    request.flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    request.flags |= is_depth ? TEXTURE_FLAG_DEPTH : 0;
    request.flags |= is_stencil ? TEXTURE_FLAG_STENCIL : 0;
    request.flags |= multiframe_buffering ? TEXTURE_FLAG_RENDERER_BUFFERING : 0;
    request.width = width;
    request.height = height;
    request.format = format;
    request.mip_levels = 1; // TODO: configurable?
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.flip_y = false; // Doesn't really matter anyway for this type.
    kresource_texture* t = (kresource_texture*)kresource_system_request(kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to request resources for arrayed writeable texture");
        return 0;
    }

    return t;
}

// NOTE: new
//

static void texture_kasset_image_loaded(void* listener, kasset_image* asset) {
    texture_asset_load_listener_context* context = (texture_asset_load_listener_context*)listener;

    b8 success = false;
    ktexture* t = context->texture;

    // TODO: Check the number of loaded assets vs the number required. Only proceed when these match.

    // FIXME: Handle these defaults in a more reasonable way - such as finding _any_ of the assets with a nonzero dimension.
    // Take the dimensions of the first asset as the size for layered images.
    if (context->assets[0]) {
        t->width = context->assets[0]->width;
        t->height = context->assets[0]->height;
        // TODO: Should this use the calculated?
        t->mip_levels = context->assets[0]->mip_levels;
    } else {
        KWARN("Asset sub 0 not found, using reasonable defaults.");
        // Provide reasonable defaults.
        if (!t->width) {
            t->width = 16;
        }
        if (!t->height) {
            t->height = 16;
        }
        if (!t->mip_levels) {
            t->mip_levels = 1;
        }
    }

    // Handle the GPU upload of pixel data.
    if (texture_apply_asset_data(t, context->name, &context->options, context->assets)) {
        success = true;
    }

    if (context->assets) {
        KFREE_TYPE_CARRAY(context->assets, kasset_image*, t->array_size);
    }
    string_cleanup_array(context->image_asset_names, t->array_size);
    string_cleanup_array(context->package_names, t->array_size);

    if (!success) {
        if (t && t->id != INVALID_ID) {
            texture_cleanup(t, true);
        }

        t = 0;
    }

    if (t && context->user_callback) {
        context->user_callback(t, context->user_listener);
    }

    KFREE_TYPE(context, texture_asset_load_listener_context, MEMORY_TAG_RESOURCE);
}

static ktexture* texture_get_if_exists(kname name) {
    ktexture* t = 0;

    // Check first if an entry with the name exists. If it does, return it.
    const bt_node* node = u64_bst_find(state_ptr->texture_name_lookup, name);
    if (node) {
        // Already exists, just return it.
        u32 index = node->value.u32;
        t = &state_ptr->textures[index];
        if (t->id != index) {
            KERROR("%s - lookup for name '%s' exists, but texture is invalid. This likely means a release wasn't done properly.", __FUNCTION__, kname_string_get(name));
            return 0;
        }
        KTRACE("%s - Texture '%s' already exists - returning.", __FUNCTION__, kname_string_get(name));

        return t;
    }

    return 0;
}

static ktexture* texture_get_new(kname name) {
    ktexture* t = 0;
    for (u32 i = 0; i < state_ptr->config.max_texture_count; ++i) {
        if (state_ptr->textures[i].id == INVALID_ID) {
            // Found one, use it.
            t = &state_ptr->textures[i];
            t->id = i;

            // Insert into the lookup tree.
            bt_node_value val = {.u32 = t->id};
            bt_node* new_node = u64_bst_insert(state_ptr->texture_name_lookup, name, val);
            if (!state_ptr->texture_name_lookup) {
                state_ptr->texture_name_lookup = new_node;
            }

            // Start reference count at 1.
            state_ptr->texture_reference_counts[t->id] = 1;

            // Invalidate the renderer handle.
            t->renderer_texture_handle = khandle_invalid();

            return t;
        }
    }

    KERROR("%s - Failed to find free slot in texture cache. Cache is full. Increase max_texture_count.");
    return 0;
}

static b8 texture_resources_acquire(ktexture* t, kname name) {
    return renderer_texture_resources_acquire(
        state_ptr->renderer,
        name,
        t->type,
        t->width,
        t->height,
        channel_count_from_pixel_format(t->format),
        t->mip_levels,
        t->array_size,
        t->flags,
        &t->renderer_texture_handle);
}

static void texture_cleanup(ktexture* t, b8 clear_references) {
    if (t && t->id != INVALID_ID) {
        renderer_texture_resources_release(state_ptr->renderer, &t->renderer_texture_handle);

        if (clear_references) {
            state_ptr->texture_reference_counts[t->id] = 0;
            state_ptr->auto_releases[t->id] = false;
        }

        kzero_memory(t, sizeof(ktexture));
        t->id = INVALID_ID;
        t->renderer_texture_handle = khandle_invalid();
    }
}

static b8 get_image_asset_names_from_options(const ktexture_load_options* options, u16* out_count, const char*** image_asset_names, const char*** package_names) {
    u16 count = 0;

    // No pixel data provided, check if asset name(s) are.
    if (options->layer_image_asset_names) {
        // Multiple assets.
        if (options->type == KTEXTURE_TYPE_2D_ARRAY) {
            count = options->layer_count;

            *image_asset_names = KALLOC_TYPE_CARRAY(const char*, count);
            *package_names = KALLOC_TYPE_CARRAY(const char*, count);
            for (u8 i = 0; i < count; ++i) {
                (*image_asset_names)[i] = string_duplicate(options->layer_image_asset_names[i]);
                (*package_names)[i] = string_duplicate(options->layer_package_names[i]);
            }
        } else if (options->type == KTEXTURE_TYPE_CUBE_ARRAY) {
            // FIXME: Support for loading an array of cubemaps.
            KERROR("%s - KTEXTURE_TYPE_CUBE_ARRAY not yet supported.", __FUNCTION__);
            return false;
        } else {
            KFATAL("%s - 'layer_image_asset_names' should only be used with 'KTEXTURE_TYPE_2D_ARRAY'.", __FUNCTION__);
            return false;
        }
    } else if (options->image_asset_name) {
        // Single asset.
        if (options->type == KTEXTURE_TYPE_CUBE) {
            /* NOTE: Treat the image_asset_name as a prefix and load the required 6 images.
             * - name_f Front
             * - name_b Back
             * - name_u Up
             * - name_d Down
             * - name_r Right
             * - name_l Left
             * For example, "skybox_f.png", "skybox_b.png", etc. where name is "skybox".
             */
            count = 6;

            // Build asset names.
            *image_asset_names = KALLOC_TYPE_CARRAY(const char*, count);
            *package_names = KALLOC_TYPE_CARRAY(const char*, count);
            const char* cube_sides = "fbudrl";
            for (u8 i = 0; i < 6; ++i) {
                (*image_asset_names)[i] = string_format("%s_%c", options->image_asset_name, cube_sides[i]);
                (*package_names)[i] = options->package_name ? string_duplicate(options->package_name) : 0;
            }
        } else if (options->type == KTEXTURE_TYPE_2D) {
            // Load a single asset for the texture.
            count = 1;

            *image_asset_names = KALLOC_TYPE_CARRAY(const char*, count);
            *package_names = KALLOC_TYPE_CARRAY(const char*, count);

            (*image_asset_names)[0] = string_duplicate(options->image_asset_name);
            (*package_names)[0] = options->package_name ? string_duplicate(options->package_name) : 0;
        } else {
            // Need to use layer_image_asset_names instead.
            if (options->type == KTEXTURE_TYPE_2D_ARRAY || options->type == KTEXTURE_TYPE_CUBE_ARRAY) {
                KERROR("%s - 'image_asset_name' is only to be used with KTEXTURE_TYPE_2D and KTEXTURE_TYPE_CUBE. Use layer_image_asset_names instead.");
                return false;
            }
        }
    }

    *out_count = count;
    return true;
}

static void combine_asset_pixel_data(kasset_image** assets, u32 count, u32 expected_width, u32 expected_height, b8 release_assets, u32* out_size, void** out_pixels) {
    // If assets are loaded, handle the pixel data.
    // Combine pixels from all images into a single array of pixels in order to upload it all at once.
    u32 layer_size = 0;
    if (assets[0]) {
        layer_size = assets[0]->pixel_array_size;
    } else {
        layer_size = expected_width * expected_height * 4;
    }
    *out_size = layer_size * count;
    *out_pixels = kallocate(*out_size, MEMORY_TAG_TEXTURE);
    // Look for asset size mismatches. For layered textures, all assets must be the same size.
    // The initial size is always based on the first texture. Start at the second texture, if one exits.
    u32 offset = 0;
    for (u16 i = 1; i < count; ++i) {
        b8 mismatch = false;
        kasset_image* asset = assets[i];
        if (!asset) {
            KERROR("No asset at index %u. This layer may not appear correctly.", i);
            goto acquire_with_options_sync_asset_continue;
        }

        if (asset->width != expected_width) {
            KERROR("Width mismatch at index %u. Expected: %u, Actual: %u", i, expected_width, asset->width);
            goto acquire_with_options_sync_asset_continue;
        }
        if (asset->height != expected_height) {
            KERROR("Height mismatch at index %u. Expected: %u, Actual: %u", i, expected_height, asset->height);
            goto acquire_with_options_sync_asset_continue;
        }
        if (asset->pixel_array_size != layer_size) {
            KERROR("Layer pixel data size mismatch at index %u. Expected: %u, Actual: %u", i, layer_size, asset->pixel_array_size);
            goto acquire_with_options_sync_asset_continue;
        }

        // Copy into the all-pixels array.
        // TODO: check image format to see if channel count is different, etc. and account for this here.
        // If the format is different, it must be converted to the same format as the texture uses (i.e. the first asset)
        kcopy_memory((*out_pixels) + offset, asset->pixels, asset->pixel_array_size);

    acquire_with_options_sync_asset_continue:
        if (release_assets) {
            // Release the asset here since it is no longer needed at this point.
            asset_system_release_image(state_ptr->kasset_system, asset);
            assets[i] = 0;
        }

        offset += layer_size;
    }
}

static b8 texture_apply_asset_data(ktexture* t, kname name, const ktexture_load_options* options, kasset_image** assets) {
    b8 success = false;

    // Calculate mip levels if needed.
    if (!t->mip_levels) {
        t->mip_levels = calculate_mip_levels_from_dimension(t->width, t->height);
    }

    if (!texture_resources_acquire(t, name)) {
        KERROR("%s - Failed to acquire renderer texture resources for texture '%s'", __FUNCTION__, kname_string_get(name));
        goto texture_apply_asset_data_cleanup;
    }

    // Load pixel/asset pixel data.
    u32 all_pixel_size = 0;
    u8* all_pixels = 0;
    b8 free_pixels = false;
    if (options->pixel_array_size && options->pixel_data) {
        all_pixels = options->pixel_data;
        all_pixel_size = options->pixel_array_size;
    } else if (assets) {
        free_pixels = true;
        combine_asset_pixel_data(assets, t->array_size, t->width, t->height, true, &all_pixel_size, (void*)&all_pixels);
    }

    // Upload the pixel data to the GPU
    b8 has_transparency = pixel_data_has_transparency(all_pixels, all_pixel_size, t->format);
    t->flags = FLAG_SET(t->flags, KTEXTURE_FLAG_HAS_TRANSPARENCY, has_transparency);

    // Write the image asset data to the texture.
    u32 texture_data_offset = 0; // NOTE: The only time this potentially could be nonzero is when explicitly loading a layer of texture data.
    b8 write_result = renderer_texture_write_data(
        state_ptr->renderer,
        t->renderer_texture_handle,
        texture_data_offset, all_pixel_size, all_pixels);

    if (!write_result) {
        KERROR("%s - Failed to write texture data resource '%s'.", __FUNCTION__, kname_string_get(name));
        goto texture_apply_asset_data_cleanup;
    }

    success = true;
texture_apply_asset_data_cleanup:

    if (all_pixels && free_pixels) {
        kfree(all_pixels, all_pixel_size, MEMORY_TAG_ARRAY);
    }

    if (t) {
        if (assets) {
            KFREE_TYPE_CARRAY(assets, kasset_image, t->array_size);
        }
    }

    if (!success) {
        texture_cleanup(t, true);
        t = 0;
    }

    return success;
}
