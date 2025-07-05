#include "texture_system.h"

#include "assets/kasset_types.h"
#include "containers/u64_bst.h"
#include "core/engine.h"
#include "core_render_types.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "utils/render_type_utils.h"
#include <runtime_defines.h>

typedef enum texture_state {
    TEXTURE_STATE_UNINITIALIZED,
    TEXTURE_STATE_LOADING,
    TEXTURE_STATE_LOADED
} texture_state;

typedef struct texture_system_state {
    texture_system_config config;

    // All registered textures. format=KPIXEL_FORMAT_UNKNOWN means slot is "free"

    /** @brief The the handle to renderer-specific texture data. */
    khandle* renderer_texture_handles;
    /** @brief The texture type. */
    ktexture_type* types;
    /** @brief The texture width. */
    u32* widths;
    /** @brief The texture height. */
    u32* heights;
    /** @brief The format of the texture data. */
    kpixel_format* formats;
    /** @brief Holds various flags for this texture. */
    ktexture_flag_bits* flags;
    /** @brief For arrayed textures, how many "layers" there are. Otherwise this is 1. */
    u16* array_sizes;
    /** @brief The number of mip maps the internal texture has. Must always be at least 1. */
    u8* mip_level_counts;
    u16* texture_reference_counts;
    b8* auto_releases;
    texture_state* states;

    // For quick lookups by name.
    bt_node* texture_name_lookup;

    ktexture default_kresource_texture;
    ktexture default_kresource_base_colour_texture;
    ktexture default_kresource_specular_texture;
    ktexture default_kresource_normal_texture;
    ktexture default_kresource_mra_texture;
    ktexture default_kresource_cube_texture;
    ktexture default_kresource_water_normal_texture;
    ktexture default_kresource_water_dudv_texture;

    // A convenience pointer to the renderer system state.
    struct renderer_system_state* renderer;

    struct asset_system_state* kasset_system;
} texture_system_state;

typedef struct texture_asset_load_listener_context {
    PFN_texture_loaded_callback user_callback;
    void* user_listener;
    ktexture texture;
    // NOTE: size of array is texture->layer_count
    kasset_image** assets;
    kname name;
    kname* image_asset_names;
    kname* package_names;
    ktexture_load_options options;
    u32 loaded_asset_count;
} texture_asset_load_listener_context;

// FIXME: remove this and its dependencies.
static texture_system_state* state_ptr = 0;

static b8 create_default_textures(texture_system_state* state);
static void release_default_textures(texture_system_state* state);
static void texture_kasset_image_loaded(void* listener, kasset_image* asset);
static ktexture texture_get_if_exists(kname name);
static ktexture texture_get_new(kname name);
static b8 texture_resources_acquire(ktexture t, kname name);
static void texture_cleanup(ktexture t, b8 clear_references);
static b8 get_image_asset_names_from_options(const ktexture_load_options* options, u16* out_count, kname** image_asset_names, kname** package_names);
static void combine_asset_pixel_data(kasset_image** assets, u32 count, u32 expected_width, u32 expected_height, b8 release_assets, u32* out_size, void** out_pixels);
static b8 texture_apply_asset_data(ktexture t, kname name, const ktexture_load_options* options, kasset_image** assets);

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
    state_ptr->renderer_texture_handles = KALLOC_TYPE_CARRAY(khandle, typed_config->max_texture_count);
    state_ptr->types = KALLOC_TYPE_CARRAY(ktexture_type, typed_config->max_texture_count);
    state_ptr->widths = KALLOC_TYPE_CARRAY(u32, typed_config->max_texture_count);
    state_ptr->heights = KALLOC_TYPE_CARRAY(u32, typed_config->max_texture_count);
    state_ptr->formats = KALLOC_TYPE_CARRAY(kpixel_format, typed_config->max_texture_count);
    state_ptr->flags = KALLOC_TYPE_CARRAY(ktexture_flag_bits, typed_config->max_texture_count);
    state_ptr->array_sizes = KALLOC_TYPE_CARRAY(u16, typed_config->max_texture_count);
    state_ptr->mip_level_counts = KALLOC_TYPE_CARRAY(u8, typed_config->max_texture_count);

    state_ptr->texture_reference_counts = KALLOC_TYPE_CARRAY(u16, typed_config->max_texture_count);
    state_ptr->auto_releases = KALLOC_TYPE_CARRAY(b8, typed_config->max_texture_count);
    state_ptr->states = KALLOC_TYPE_CARRAY(texture_state, typed_config->max_texture_count);

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

        texture_system_config* typed_config = &state_ptr->config;

        KFREE_TYPE_CARRAY(state_ptr->renderer_texture_handles, khandle, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->types, ktexture_type, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->widths, u32, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->heights, u32, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->formats, kpixel_format, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->flags, ktexture_flag_bits, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->array_sizes, u16, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->mip_level_counts, u8, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->texture_reference_counts, u16, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->auto_releases, b8, typed_config->max_texture_count);
        KFREE_TYPE_CARRAY(state_ptr->states, texture_state, typed_config->max_texture_count);

        state_ptr->renderer = 0;
        state_ptr = 0;
    }
}

ktexture texture_acquire(kname image_asset_name, void* listener, PFN_texture_loaded_callback callback) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .auto_release = true,
        .name = image_asset_name,
        .image_asset_name = image_asset_name,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = 0};
    return texture_acquire_with_options(options, listener, callback);
}

// auto_release=true, default options
ktexture texture_acquire_sync(kname image_asset_name) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .auto_release = true,
        .name = image_asset_name,
        .image_asset_name = image_asset_name,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = 0};
    return texture_acquire_with_options_sync(options);
}

void texture_release(ktexture texture) {
    if (!state_ptr) {
        return;
    }
    if (texture != INVALID_KTEXTURE && state_ptr->formats[texture] != KPIXEL_FORMAT_UNKNOWN) {
        if (state_ptr->texture_reference_counts[texture] > 0) {
            state_ptr->texture_reference_counts[texture]--;

            if (state_ptr->texture_reference_counts[texture] == 0 && state_ptr->auto_releases[texture] == true) {
                texture_cleanup(texture, true);
            }
        } else {
            KWARN("Texture id %u has no references and cannot be released.", texture);
        }
    }
}

// auto_release=true, default options
ktexture texture_acquire_from_package(kname image_asset_name, kname package_name, void* listener, PFN_texture_loaded_callback callback) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .layer_count = 1,
        .auto_release = true,
        .name = image_asset_name,
        .image_asset_name = image_asset_name,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = package_name};
    return texture_acquire_with_options(options, listener, callback);
}

ktexture texture_acquire_from_package_sync(kname image_asset_name, kname package_name) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .layer_count = 1,
        .auto_release = true,
        .name = image_asset_name,
        .image_asset_name = image_asset_name,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = package_name};
    return texture_acquire_with_options_sync(options);
}

ktexture texture_cubemap_acquire(kname image_asset_name_prefix, void* listener, PFN_texture_loaded_callback callback) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_CUBE,
        .layer_count = 6,
        .auto_release = true,
        .name = image_asset_name_prefix,
        .image_asset_name = image_asset_name_prefix,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = 0};
    return texture_acquire_with_options(options, listener, callback);
}

ktexture texture_cubemap_acquire_sync(kname image_asset_name_prefix) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_CUBE,
        .layer_count = 6,
        .auto_release = true,
        .name = image_asset_name_prefix,
        .image_asset_name = image_asset_name_prefix,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = 0};
    return texture_acquire_with_options_sync(options);
}

ktexture texture_cubemap_acquire_from_package(kname image_asset_name_prefix, kname package_name, void* listener, PFN_texture_loaded_callback callback) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_CUBE,
        .layer_count = 6,
        .auto_release = true,
        .name = image_asset_name_prefix,
        .image_asset_name = image_asset_name_prefix,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = package_name};
    return texture_acquire_with_options(options, listener, callback);
}

ktexture texture_cubemap_acquire_from_package_sync(kname image_asset_name_prefix, kname package_name) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_CUBE,
        .layer_count = 6,
        .auto_release = true,
        .name = image_asset_name_prefix,
        .image_asset_name = image_asset_name_prefix,
        .format = KPIXEL_FORMAT_RGBA8,
        .package_name = package_name};
    return texture_acquire_with_options_sync(options);
}

ktexture texture_acquire_from_image(const struct kasset_image* image, kname name) {
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .layer_count = 1,
        .auto_release = true,
        .name = name,
        .pixel_array_size = image->pixel_array_size,
        .format = KPIXEL_FORMAT_RGBA8,
        .pixel_data = image->pixels};
    return texture_acquire_with_options_sync(options);
}

ktexture texture_acquire_from_pixel_data(kpixel_format format, u32 pixel_array_size, void* pixels, u32 width, u32 height, kname name) {
    if (!width || !height) {
        KERROR("%s requires a nonzero width and height, ya dingus!", __FUNCTION__);
        return INVALID_KTEXTURE;
    }
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_2D,
        .layer_count = 1,
        .auto_release = true,
        .name = name,
        .pixel_array_size = pixel_array_size,
        .format = format,
        .width = width,
        .height = height,
        .pixel_data = pixels};
    return texture_acquire_with_options_sync(options);
}

ktexture texture_cubemap_acquire_from_pixel_data(kpixel_format format, u32 pixel_array_size, void* pixels, u32 width, u32 height, kname name) {
    if (!width || !height) {
        KERROR("%s requires a nonzero width and height, ya dingus!", __FUNCTION__);
        return INVALID_KTEXTURE;
    }
    ktexture_load_options options = {
        .type = KTEXTURE_TYPE_CUBE,
        .layer_count = 6,
        .auto_release = true,
        .name = name,
        .pixel_array_size = pixel_array_size,
        .format = format,
        .width = width,
        .height = height,
        .pixel_data = pixels};
    return texture_acquire_with_options_sync(options);
}

// TODO:
/* ktexture texture_cubemap_acquire_from_images(const struct kasset_image* images[6]) {
} */

ktexture texture_acquire_with_options(ktexture_load_options options, void* listener, PFN_texture_loaded_callback callback) {
    if (options.name == INVALID_KNAME && options.image_asset_name == INVALID_KNAME) {
        KERROR("%s - Either name or image_asset_name is required.", __FUNCTION__);
        return INVALID_KTEXTURE;
    }

    b8 success = false;
    kname name = options.name ? options.name : options.image_asset_name;
    ktexture t = texture_get_if_exists(name);

    // If an entry with the name exists, return it.
    if (t != INVALID_KTEXTURE) {
        // Increment reference count.
        state_ptr->texture_reference_counts[t]++;

        // Immediately make the user callback, and boot.
        if (callback) {
            callback(t, listener);
        }
        return t;
    }

    // Pick a free slot in the texture cache.
    t = texture_get_new(name);
    if (t == INVALID_KTEXTURE) {
        goto texture_acquire_with_options_async_cleanup;
    }

    // Set some default properties.
    state_ptr->formats[t] = options.format;
    state_ptr->types[t] = options.type;
    state_ptr->widths[t] = options.width;
    state_ptr->heights[t] = options.height;
    state_ptr->mip_level_counts[t] = options.mip_levels;
    state_ptr->array_sizes[t] = options.layer_count;
    // Ensure there is always at least one layer.
    if (!state_ptr->array_sizes[t]) {
        state_ptr->array_sizes[t] = 1;
    }

    // Parse flags from options booleans.
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_DEPTH, options.is_depth);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_STENCIL, options.is_stencil);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_IS_WRITEABLE, options.is_writeable);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_RENDERER_BUFFERING, options.multiframe_buffering);

    state_ptr->auto_releases[t] = options.auto_release;

    kname* image_asset_names = 0;
    kname* package_names = 0;
    if (!get_image_asset_names_from_options(&options, &state_ptr->array_sizes[t], &image_asset_names, &package_names)) {
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
        context->assets = KALLOC_TYPE_CARRAY(kasset_image*, state_ptr->array_sizes[t]);

        // Fetch assets.
        u16 array_size = state_ptr->array_sizes[t];
        for (u16 i = 0; i < array_size; ++i) {
            const char* asset_name = kname_string_get(image_asset_names[i]);
            if (package_names[i] != INVALID_KNAME) {
                context->assets[i] = asset_system_request_image_from_package(state_ptr->kasset_system, kname_string_get(package_names[i]), asset_name, context, texture_kasset_image_loaded);
            } else {
                context->assets[i] = asset_system_request_image(state_ptr->kasset_system, asset_name, context, texture_kasset_image_loaded);
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

    if (!texture_resources_acquire(t, name)) {
        KERROR("%s - Failed to acquire renderer texture resources for texture '%s'", __FUNCTION__, kname_string_get(name));
        goto texture_acquire_with_options_async_cleanup;
    }

    // Handle pixel data if provided.
    if (options.pixel_data && options.pixel_array_size) {

        // Upload the pixel data to the GPU
        b8 has_transparency = pixel_data_has_transparency(options.pixel_data, options.pixel_array_size, state_ptr->formats[t]);
        state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_HAS_TRANSPARENCY, has_transparency);

        // Write the image asset data to the texture.
        u32 texture_data_offset = 0; // NOTE: The only time this potentially could be nonzero is when explicitly loading a layer of texture data.
        b8 write_result = renderer_texture_write_data(
            state_ptr->renderer,
            state_ptr->renderer_texture_handles[t],
            texture_data_offset, options.pixel_array_size, options.pixel_data);

        if (!write_result) {
            KERROR("%s - Failed to write texture data resource '%s'.", __FUNCTION__, options.image_asset_name);
            goto texture_acquire_with_options_async_cleanup;
        }
    }

    state_ptr->states[t] = TEXTURE_STATE_LOADED;
    success = true;
texture_acquire_with_options_async_cleanup:

    if (t != INVALID_KTEXTURE) {
        KFREE_TYPE_CARRAY(image_asset_names, kname, state_ptr->array_sizes[t]);
        KFREE_TYPE_CARRAY(package_names, kname, state_ptr->array_sizes[t]);
    }

    if (!success) {
        texture_cleanup(t, true);
        t = INVALID_KTEXTURE;
    }

    return t;
}

ktexture texture_acquire_with_options_sync(ktexture_load_options options) {
    if (options.name == INVALID_KNAME && options.image_asset_name == INVALID_KNAME) {
        KERROR("%s - Either name or image_asset_name is required.", __FUNCTION__);
        return INVALID_KTEXTURE;
    }

    b8 success = false;
    kname name = options.name ? options.name : options.image_asset_name;
    ktexture t = texture_get_if_exists(name);

    // Load pixel/asset pixel data.
    u32 all_pixel_size = 0;
    // TODO: This will be an issue with any other bit depth than 8
    u8* all_pixels = 0;
    u32 all_pixel_count = 0;
    b8 free_pixels = false;

    // If an entry with the name exists, return it.
    if (t != INVALID_KTEXTURE) {
        // Increment reference count.
        state_ptr->texture_reference_counts[t]++;
        return t;
    }

    // Pick a free slot in the texture cache.
    t = texture_get_new(name);
    if (t == INVALID_KTEXTURE) {
        goto texture_acquire_with_options_sync_cleanup;
    }

    // Set some default properties.
    state_ptr->formats[t] = options.format;
    state_ptr->types[t] = options.type;
    state_ptr->widths[t] = options.width;
    state_ptr->heights[t] = options.height;
    state_ptr->mip_level_counts[t] = options.mip_levels;
    state_ptr->array_sizes[t] = options.layer_count;
    // Ensure there is always at least one layer.
    if (!state_ptr->array_sizes[t]) {
        state_ptr->array_sizes[t] = 1;
    }

    // Parse flags from options booleans.
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_DEPTH, options.is_depth);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_STENCIL, options.is_stencil);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_IS_WRITEABLE, options.is_writeable);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_RENDERER_BUFFERING, options.multiframe_buffering);

    kasset_image** assets = 0;

    // Gather asset/package names, if relevant.
    kname* image_asset_names = 0;
    kname* package_names = 0;

    if (!get_image_asset_names_from_options(&options, &state_ptr->array_sizes[t], &image_asset_names, &package_names)) {
        goto texture_acquire_with_options_sync_cleanup;
    }

    if (image_asset_names) {
        // Fetch assets.
        assets = KALLOC_TYPE_CARRAY(kasset_image*, state_ptr->array_sizes[t]);
        for (u16 i = 0; i < state_ptr->array_sizes[t]; ++i) {
            const char* asset_name = kname_string_get(image_asset_names[i]);
            if (package_names[i] != INVALID_KNAME) {
                assets[i] = asset_system_request_image_from_package_sync(state_ptr->kasset_system, kname_string_get(package_names[i]), asset_name);
            } else {
                assets[i] = asset_system_request_image_sync(state_ptr->kasset_system, asset_name);
            }
            if (!assets[i]) {
                // NOTE: Continue to load other images instead of booting here.
                KERROR("%s - Asset named '%s' does not exist, thus a texture cannot be loaded from it.", __FUNCTION__, options.image_asset_name);
            }
        }

        // Take the dimensions of the first asset as the size for layered images.
        if (assets[0]) {
            state_ptr->widths[t] = assets[0]->width;
            state_ptr->heights[t] = assets[0]->height;
            state_ptr->mip_level_counts[t] = assets[0]->mip_levels;
            state_ptr->formats[t] = assets[0]->format;
        } else {
            KWARN("Asset sub 0 not found, using reasonable defaults.");
            // Provide reasonable defaults.
            if (!state_ptr->widths[t]) {
                state_ptr->widths[t] = 16;
            }
            if (!state_ptr->heights[t]) {
                state_ptr->heights[t] = 16;
            }
            state_ptr->formats[t] = KPIXEL_FORMAT_RGBA8;
        }
    }

    // Calculate mip levels if needed.
    if (!state_ptr->mip_level_counts[t]) {
        state_ptr->mip_level_counts[t] = calculate_mip_levels_from_dimension(state_ptr->widths[t], state_ptr->heights[t]);
    }

    state_ptr->auto_releases[t] = options.auto_release;

    if (!texture_resources_acquire(t, name)) {
        KERROR("%s - Failed to acquire renderer texture resources for texture '%s'", __FUNCTION__, options.image_asset_name);
        goto texture_acquire_with_options_sync_cleanup;
    }

    if (options.pixel_array_size && options.pixel_data) {
        all_pixels = options.pixel_data;
        all_pixel_size = options.pixel_array_size;
        all_pixel_count = options.width * options.height * options.layer_count;
    } else if (assets) {
        free_pixels = true;
        all_pixel_count = state_ptr->widths[t] * state_ptr->heights[t] * state_ptr->array_sizes[t];
        combine_asset_pixel_data(assets, state_ptr->array_sizes[t], state_ptr->widths[t], state_ptr->heights[t], true, &all_pixel_size, (void*)&all_pixels);
    }

    // Determine transparency.
    b8 has_transparency = pixel_data_has_transparency(all_pixels, all_pixel_count, state_ptr->formats[t]);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_HAS_TRANSPARENCY, has_transparency);

    // Write the image asset/pixel data to the texture if it exists.
    if (all_pixel_size && all_pixels) {
        u32 texture_data_offset = 0; // NOTE: The only time this potentially could be nonzero is when explicitly loading a layer of texture data.
        b8 write_result = renderer_texture_write_data(
            state_ptr->renderer,
            state_ptr->renderer_texture_handles[t],
            texture_data_offset, all_pixel_size, all_pixels);

        if (!write_result) {
            KERROR("%s - Failed to write texture data resource '%s'.", __FUNCTION__, options.image_asset_name);
            goto texture_acquire_with_options_sync_cleanup;
        }
    }

    state_ptr->states[t] = TEXTURE_STATE_LOADED;
    success = true;
texture_acquire_with_options_sync_cleanup:

    if (all_pixels && free_pixels) {
        kfree(all_pixels, all_pixel_size, MEMORY_TAG_ARRAY);
    }
    if (t) {
        if (assets) {
            KFREE_TYPE_CARRAY(assets, kasset_image*, state_ptr->array_sizes[t]);
        }
        if (image_asset_names) {
            KFREE_TYPE_CARRAY(image_asset_names, kname, state_ptr->array_sizes[t]);
        }
        if (package_names) {
            KFREE_TYPE_CARRAY(package_names, kname, state_ptr->array_sizes[t]);
        }
    }

    if (!success) {
        texture_cleanup(t, true);
        t = INVALID_KTEXTURE;
    }

    return t;
}

b8 texture_resize(ktexture t, u32 width, u32 height, b8 regenerate_internal_data) {
    if (t != INVALID_KTEXTURE) {
        if (!width || !height) {
            KERROR("%s - A nonzero width and height are required!", __FUNCTION__);
            return false;
        }
        if (!(state_ptr->flags[t] & KTEXTURE_FLAG_IS_WRITEABLE)) {
            KWARN("texture_system_resize should not be called on textures that are not writeable.");
            return false;
        }
        state_ptr->widths[t] = width;
        state_ptr->heights[t] = height;
        // FIXME: remove this requirement, and potentially the regenerate_internal_data flag as well.
        // Only allow this for writeable textures that are not wrapped.
        // Wrapped textures can call texture_system_set_internal then call
        // this function to get the above parameter updates and a generation
        // update.
        if (!(state_ptr->flags[t] & KTEXTURE_FLAG_IS_WRAPPED) && regenerate_internal_data) {
            // Regenerate internals for the new size.
            return renderer_texture_resize(state_ptr->renderer, state_ptr->renderer_texture_handles[t], width, height);
        }
        return true;
    }
    return false;
}

b8 texture_write_data(ktexture t, u32 offset, u32 size, void* data) {
    if (t) {
        return renderer_texture_write_data(state_ptr->renderer, state_ptr->renderer_texture_handles[t], offset, size, data);
    }
    return false;
}

static b8 texture_is_default(texture_system_state* state, ktexture t) {
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

u32 texture_width_get(ktexture t) {
    if (t == INVALID_KTEXTURE) {
        return 0;
    }

    return state_ptr->widths[t];
}

u32 texture_height_get(ktexture t) {
    if (t == INVALID_KTEXTURE) {
        return 0;
    }

    return state_ptr->heights[t];
}

b8 texture_dimensions_get(ktexture t, u32* out_width, u32* out_height) {
    if (t == INVALID_KTEXTURE) {
        return false;
    }

    *out_width = state_ptr->widths[t];
    *out_height = state_ptr->heights[t];

    return true;
}

khandle texture_renderer_handle_get(ktexture t) {
    if (t == INVALID_KTEXTURE) {
        return khandle_invalid();
    }

    return state_ptr->renderer_texture_handles[t];
}

ktexture_flag_bits texture_flags_get(ktexture t) {
    if (t == INVALID_KTEXTURE) {
        return 0;
    }

    return state_ptr->flags[t];
}

b8 texture_is_loaded(ktexture t) {
    if (t == INVALID_KTEXTURE) {
        return false;
    }

    return state_ptr->states[t] == TEXTURE_STATE_LOADED;
}

static b8 create_default_textures(texture_system_state* state) {
    // NOTE: Create default texture, a 256x256 blue/white checkerboard pattern.
    // This is done in code to eliminate asset dependencies.
    KTRACE("Creating default texture...");
    const u32 tex_dimension = 16;
    const u32 channels = 4;
    const u32 pixel_count = tex_dimension * tex_dimension;

    // Default texture
    {
        u8 default_pixels[16 * 16 * 4] = {0};
        kset_memory(default_pixels, 255, sizeof(u8) * pixel_count * channels);

        // Each pixel.
        for (u64 row = 0; row < tex_dimension; ++row) {
            for (u64 col = 0; col < tex_dimension; ++col) {
                u64 index = (row * tex_dimension) + col;
                u64 index_bpp = index * channels;
                if (row % 2) {
                    if (col % 2) {
                        default_pixels[index_bpp + 0] = 0;
                        default_pixels[index_bpp + 1] = 0;
                    }
                } else {
                    if (!(col % 2)) {
                        default_pixels[index_bpp + 0] = 0;
                        default_pixels[index_bpp + 1] = 0;
                    }
                }
            }
        }

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_texture = INVALID_KTEXTURE;
        ktexture t = texture_acquire_from_pixel_data(
            KPIXEL_FORMAT_RGBA8,
            pixel_array_size,
            default_pixels,
            16,
            16,
            kname_create(DEFAULT_TEXTURE_NAME));
        state->default_kresource_texture = t;
        if (state->default_kresource_texture == INVALID_KTEXTURE) {
            KERROR("Failed to request resources for default texture");
            return false;
        }
    }

    // Base colour texture.
    {
        KTRACE("Creating default base colour texture...");

        u8 diff_pixels[16 * 16 * 4] = {0};
        // Default diffuse map is all white.
        kset_memory(diff_pixels, 255, sizeof(u8) * 16 * 16 * 4);
        // Request new resource texture.

        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_base_colour_texture = texture_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, diff_pixels, 16, 16, kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME));
        if (state->default_kresource_base_colour_texture == INVALID_KTEXTURE) {
            KERROR("Failed to request resources for default base colour texture");
            return false;
        }
    }

    // Specular texture.
    {
        KTRACE("Creating default specular texture...");
        u8 spec_pixels[16 * 16 * 4] = {0};
        // Default spec map is black (no specular)
        kset_memory(spec_pixels, 0, sizeof(u8) * 16 * 16 * 4);

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_specular_texture = texture_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, spec_pixels, 16, 16, kname_create(DEFAULT_SPECULAR_TEXTURE_NAME));
        if (state->default_kresource_specular_texture == INVALID_KTEXTURE) {
            KERROR("Failed to request resources for default specular texture");
            return false;
        }
    }

    // Normal texture.
    u8 normal_pixels[16 * 16 * 4] = {0}; // w * h * channels
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
        state->default_kresource_normal_texture = texture_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, normal_pixels, 16, 16, kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
        if (state->default_kresource_normal_texture == INVALID_KTEXTURE) {
            KERROR("Failed to request resources for default normal texture");
            return false;
        }
    }

    // MRA texture
    u8 mra_pixels[16 * 16 * 4] = {0}; // w * h * channels
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
        state->default_kresource_mra_texture = texture_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, mra_pixels, 16, 16, kname_create(DEFAULT_MRA_TEXTURE_NAME));
        if (state->default_kresource_mra_texture == INVALID_KTEXTURE) {
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
        u8 cube_side_pixels[16 * 16 * 4] = {0};
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
        state->default_kresource_cube_texture = texture_cubemap_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, pixels, 16, 16, kname_create(DEFAULT_CUBE_TEXTURE_NAME));
        if (state->default_kresource_cube_texture == INVALID_KTEXTURE) {
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
        if (state->default_kresource_terrain_texture == INVALID_KTEXTURE) {
            KERROR("Failed to request resources for default terrain texture");
            return false;
        }
        kfree(terrain_pixels, layer_size * layer_count, MEMORY_TAG_ARRAY);
    } */

    // Default water normal texture is part of the runtime package - request it.
    state->default_kresource_water_normal_texture = texture_acquire_from_package(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME), kname_create(PACKAGE_NAME_RUNTIME), 0, 0);

    // Default water dudv texture is part of the runtime package - request it.
    state->default_kresource_water_dudv_texture = texture_acquire_from_package(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME), kname_create(PACKAGE_NAME_RUNTIME), 0, 0);

    return true;
}

static void release_default_textures(texture_system_state* state) {
    if (state) {
        texture_release(state->default_kresource_texture);
        texture_release(state->default_kresource_base_colour_texture);
        texture_release(state->default_kresource_specular_texture);
        texture_release(state->default_kresource_normal_texture);
        texture_release(state->default_kresource_mra_texture);
        texture_release(state->default_kresource_cube_texture);
        texture_release(state->default_kresource_water_normal_texture);
        texture_release(state->default_kresource_water_dudv_texture);
    }
}

static void texture_kasset_image_loaded(void* listener, kasset_image* asset) {
    texture_asset_load_listener_context* context = (texture_asset_load_listener_context*)listener;

    context->loaded_asset_count++;

    b8 success = false;
    ktexture t = context->texture;

    // Check the number of loaded assets vs the number required. Only proceed when these match.
    if (context->loaded_asset_count == state_ptr->array_sizes[t]) {
        KDEBUG("Required asset loaded count (%u) met for texture id %u, proceeding to upload to GPU.", state_ptr->array_sizes[t], t);

        // FIXME: Handle these defaults in a more reasonable way - such as finding _any_ of the assets with a nonzero dimension.
        // Take the dimensions of the first asset as the size for layered images.
        if (context->assets[0]) {
            state_ptr->widths[t] = context->assets[0]->width;
            state_ptr->heights[t] = context->assets[0]->height;
            // TODO: Should this use the calculated?
            state_ptr->mip_level_counts[t] = context->assets[0]->mip_levels;
        } else {
            KWARN("Asset sub 0 not found, using reasonable defaults.");
            // Provide reasonable defaults.
            if (!state_ptr->widths[t]) {
                state_ptr->widths[t] = 16;
            }
            if (!state_ptr->heights[t]) {
                state_ptr->heights[t] = 16;
            }
            if (!state_ptr->mip_level_counts[t]) {
                state_ptr->mip_level_counts[t] = 1;
            }
        }

        // Handle the GPU upload of pixel data.
        if (texture_apply_asset_data(t, context->name, &context->options, context->assets)) {
            success = true;
        }

        if (context->assets) {
            KFREE_TYPE_CARRAY(context->assets, kasset_image*, state_ptr->array_sizes[t]);
        }
        KFREE_TYPE_CARRAY(context->image_asset_names, kname, state_ptr->array_sizes[t]);
        KFREE_TYPE_CARRAY(context->package_names, kname, state_ptr->array_sizes[t]);

        if (!success) {
            if (t != INVALID_KTEXTURE) {
                texture_cleanup(t, true);
            }

            t = INVALID_KTEXTURE;
        }

        if (t != INVALID_KTEXTURE && context->user_callback) {
            context->user_callback(t, context->user_listener);
        }

        // KFREE_TYPE(context, texture_asset_load_listener_context, MEMORY_TAG_RESOURCE);
    }
}

static ktexture texture_get_if_exists(kname name) {
    ktexture t = INVALID_KTEXTURE;

    // Check first if an entry with the name exists. If it does, return it.
    const bt_node* node = u64_bst_find(state_ptr->texture_name_lookup, name);
    if (node) {
        // Already exists, just return it.
        t = node->value.u16;
        if (state_ptr->formats[t] == KPIXEL_FORMAT_UNKNOWN) {
            KERROR("%s - lookup for name '%s' exists, but texture is invalid. This likely means a release wasn't done properly.", __FUNCTION__, kname_string_get(name));
            return INVALID_KTEXTURE;
        }
        KTRACE("%s - Texture '%s' already exists - returning.", __FUNCTION__, kname_string_get(name));

        return t;
    }

    return INVALID_KTEXTURE;
}

static ktexture texture_get_new(kname name) {
    for (u16 i = 0; i < state_ptr->config.max_texture_count; ++i) {
        if (state_ptr->formats[i] == KPIXEL_FORMAT_UNKNOWN) {
            // Found one, use it.
            state_ptr->states[i] = TEXTURE_STATE_LOADING;

            // Insert into the lookup tree.
            bt_node_value val = {.u16 = i};
            bt_node* new_node = u64_bst_insert(state_ptr->texture_name_lookup, name, val);
            if (!state_ptr->texture_name_lookup) {
                state_ptr->texture_name_lookup = new_node;
            }

            // Start reference count at 1.
            state_ptr->texture_reference_counts[i] = 1;

            // Invalidate the renderer handle.
            state_ptr->renderer_texture_handles[i] = khandle_invalid();

            return i;
        }
    }

    KERROR("%s - Failed to find free slot in texture cache. Cache is full. Increase max_texture_count.");
    return INVALID_KTEXTURE;
}

static b8 texture_resources_acquire(ktexture t, kname name) {
    return renderer_texture_resources_acquire(
        state_ptr->renderer,
        name,
        state_ptr->types[t],
        state_ptr->widths[t],
        state_ptr->heights[t],
        channel_count_from_pixel_format(state_ptr->formats[t]),
        state_ptr->mip_level_counts[t],
        state_ptr->array_sizes[t],
        state_ptr->flags[t],
        &state_ptr->renderer_texture_handles[t]);
}

static void texture_cleanup(ktexture t, b8 clear_references) {
    if (t != INVALID_KTEXTURE) {
        renderer_texture_resources_release(state_ptr->renderer, &state_ptr->renderer_texture_handles[t]);

        if (clear_references) {
            state_ptr->texture_reference_counts[t] = 0;
            state_ptr->auto_releases[t] = false;
        }

        state_ptr->renderer_texture_handles[t] = khandle_invalid();
        state_ptr->types[t] = 0;
        state_ptr->widths[t] = 0;
        state_ptr->heights[t] = 0;
        // This marks the slot as 'free'
        state_ptr->formats[t] = KPIXEL_FORMAT_UNKNOWN;
        state_ptr->mip_level_counts[t] = 0;
        state_ptr->array_sizes[t] = 0;
        state_ptr->flags[t] = 0;
        state_ptr->states[t] = TEXTURE_STATE_UNINITIALIZED;
    }
}

static b8 get_image_asset_names_from_options(const ktexture_load_options* options, u16* out_count, kname** image_asset_names, kname** package_names) {
    u16 count = 0;

    // No pixel data provided, check if asset name(s) are.
    if (options->layer_image_asset_names) {
        // Multiple assets.
        if (options->type == KTEXTURE_TYPE_2D_ARRAY) {
            count = options->layer_count;

            *image_asset_names = KALLOC_TYPE_CARRAY(kname, count);
            *package_names = KALLOC_TYPE_CARRAY(kname, count);
            for (u8 i = 0; i < count; ++i) {
                (*image_asset_names)[i] = options->layer_image_asset_names[i];
                (*package_names)[i] = options->layer_package_names[i];
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
             * - name_r Right
             * - name_l Left
             * - name_d Down
             * - name_u Up
             * - name_f Front
             * - name_b Back
             * For example, "skybox_f.png", "skybox_b.png", etc. where name is "skybox".
             */
            count = 6;

            // Build asset names.
            *image_asset_names = KALLOC_TYPE_CARRAY(kname, count);
            *package_names = KALLOC_TYPE_CARRAY(kname, count);
            /* const char* cube_sides = "fbudrl"; */
            // +X/-X/+Y/-Y/+Z/-Z
            const char* cube_sides = "rldufb";
            for (u8 i = 0; i < 6; ++i) {
                char* name_temp = string_format("%s_%c", kname_string_get(options->image_asset_name), cube_sides[i]);
                (*image_asset_names)[i] = kname_create(name_temp);
                string_free(name_temp);
                (*package_names)[i] = options->package_name ? options->package_name : INVALID_KNAME;
            }
        } else if (options->type == KTEXTURE_TYPE_2D) {
            // Load a single asset for the texture.
            count = 1;

            *image_asset_names = KALLOC_TYPE_CARRAY(kname, count);
            *package_names = KALLOC_TYPE_CARRAY(kname, count);

            (*image_asset_names)[0] = options->image_asset_name;
            (*package_names)[0] = options->package_name ? options->package_name : INVALID_KNAME;
        } else {
            // Need to use layer_image_asset_names instead.
            if (options->type == KTEXTURE_TYPE_2D_ARRAY || options->type == KTEXTURE_TYPE_CUBE_ARRAY) {
                KERROR("%s - 'image_asset_name' is only to be used with KTEXTURE_TYPE_2D and KTEXTURE_TYPE_CUBE. Use layer_image_asset_names instead.");
                return false;
            }
        }
    } else {
        // No pixel data or asset data - boot.
        return true;
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
        kasset_image* asset = assets[i];
        if (!asset) {
            KERROR("No asset at index %u. This layer may not appear correctly.", i);
            continue;
        }

        if (assets[i]->width != expected_width) {
            KERROR("Width mismatch at index %u. Expected: %u, Actual: %u", i, expected_width, assets[i]->width);
            continue;
        }
        if (assets[i]->height != expected_height) {
            KERROR("Height mismatch at index %u. Expected: %u, Actual: %u", i, expected_height, assets[i]->height);
            continue;
        }
        if (asset->pixel_array_size != layer_size) {
            KERROR("Layer pixel data size mismatch at index %u. Expected: %u, Actual: %u", i, layer_size, asset->pixel_array_size);
            continue;
        }
    }

    // Take a copy of the image pixel data.
    for (u16 i = 0; i < count; ++i) {
        kasset_image* asset = assets[i];
        if (!asset) {
            KERROR("No asset at index %u. This layer may not appear correctly.", i);
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

static b8 texture_apply_asset_data(ktexture t, kname name, const ktexture_load_options* options, kasset_image** assets) {
    b8 success = false;

    // Calculate mip levels if needed.
    if (!state_ptr->mip_level_counts[t]) {
        state_ptr->mip_level_counts[t] = calculate_mip_levels_from_dimension(state_ptr->widths[t], state_ptr->heights[t]);
    }

    // Load pixel/asset pixel data.
    u32 all_pixel_size = 0;
    u8* all_pixels = 0;
    u32 all_pixel_count = 0;
    b8 free_pixels = false;

    if (!texture_resources_acquire(t, name)) {
        KERROR("%s - Failed to acquire renderer texture resources for texture '%s'", __FUNCTION__, kname_string_get(name));
        goto texture_apply_asset_data_cleanup;
    }

    if (options->pixel_array_size && options->pixel_data) {
        all_pixels = options->pixel_data;
        all_pixel_size = options->pixel_array_size;
        all_pixel_count = options->width * options->height * options->layer_count;
    } else if (assets) {
        free_pixels = true;
        all_pixel_count = state_ptr->widths[t] * state_ptr->heights[t] * state_ptr->array_sizes[t];
        combine_asset_pixel_data(assets, state_ptr->array_sizes[t], state_ptr->widths[t], state_ptr->heights[t], true, &all_pixel_size, (void*)&all_pixels);
    }

    // Upload the pixel data to the GPU
    b8 has_transparency = pixel_data_has_transparency(all_pixels, all_pixel_count, state_ptr->formats[t]);
    state_ptr->flags[t] = FLAG_SET(state_ptr->flags[t], KTEXTURE_FLAG_HAS_TRANSPARENCY, has_transparency);

    // Write the image asset data to the texture.
    u32 texture_data_offset = 0; // NOTE: The only time this potentially could be nonzero is when explicitly loading a layer of texture data.
    b8 write_result = renderer_texture_write_data(
        state_ptr->renderer,
        state_ptr->renderer_texture_handles[t],
        texture_data_offset, all_pixel_size, all_pixels);

    if (!write_result) {
        KERROR("%s - Failed to write texture data resource '%s'.", __FUNCTION__, kname_string_get(name));
        goto texture_apply_asset_data_cleanup;
    }

    state_ptr->states[t] = TEXTURE_STATE_LOADED;
    success = true;
texture_apply_asset_data_cleanup:

    if (all_pixels && free_pixels) {
        kfree(all_pixels, all_pixel_size, MEMORY_TAG_ARRAY);
    }

    if (!success) {
        texture_cleanup(t, true);
        t = INVALID_KTEXTURE;
    }

    return success;
}
