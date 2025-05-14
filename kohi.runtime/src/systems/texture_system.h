/**
 * @file texture_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the texture system, which handles the acquisition
 * and releasing of textures. It also reference monitors textures, and can
 * auto-release them when they no longer have any references, if configured to
 * do so.
 * @version 2.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2025
 *
 */

#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"

struct texture_system_state;

/** @brief The texture system configuration */
typedef struct texture_system_config {
    /** @brief The maximum number of textures that can be loaded at once. */
    u16 max_texture_count;
} texture_system_config;

/** @brief The default texture name. */
#define DEFAULT_TEXTURE_NAME "Texture.Default"

/** @brief The default base colour texture name. */
#define DEFAULT_BASE_COLOUR_TEXTURE_NAME "Texture.DefaultBase"

/** @brief The default specular texture name. */
#define DEFAULT_SPECULAR_TEXTURE_NAME "Texture.DefaultSpecular"

/** @brief The default normal texture name. */
#define DEFAULT_NORMAL_TEXTURE_NAME "Texture.DefaultNormal"

/** @brief The default combined (metallic, roughness, AO) texture name. */
#define DEFAULT_MRA_TEXTURE_NAME "Texture.DefaultMRA"

/** @brief The default cube texture name. */
#define DEFAULT_CUBE_TEXTURE_NAME "Texture.DefaultCube"

/** @brief The default water normal texture name. */
#define DEFAULT_WATER_NORMAL_TEXTURE_NAME "Texture.DefaultWaterNormal"

/** @brief The default water derivative (dudv) texture name. */
#define DEFAULT_WATER_DUDV_TEXTURE_NAME "Texture.DefaultWaterDUDV"

/**
 * @brief Initializes the texture system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (texture_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 texture_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the texture system.

 * @param state The state block of memory for this system.
 */
void texture_system_shutdown(void* state);

typedef void (*PFN_texture_loaded_callback)(ktexture texture, void* listener);

/**
 * @brief Attempts to acquire a texture using the given image asset name. Asynchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * The image_asset_name is also used as the texture name. Loads from the application package.
 * Texture will be auto-released when reference count reaches 0.
 * Texture will be of KTEXTURE_TYPE_2D.
 *
 * This function is considered to be asynchronous, and invokes the given callback when the texture is loaded.
 *
 * @param image_asset_name The name of the image asset to load and use for the texture.
 * @param listener A structure containing data to be passed along to the callback when the image is loaded. Optional.
 * @param callback The callback to be made once the texture is loaded. Optional.
 * @return The texture to be loaded. The default texture if not found. INVALID_ID_U16 if error. May/may not be loaded yet on return.
 */
KAPI ktexture texture_acquire(const char* image_asset_name, void* listener, PFN_texture_loaded_callback callback);

/**
 * @brief Attempts to acquire a texture using the given image asset name. Synchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * The image_asset_name is also used as the texture name. Loads from the application package.
 * Texture will be auto-released when reference count reaches 0.
 * Texture will be of KTEXTURE_TYPE_2D.
 *
 * This function is considered to be synchronous and is guaranteed to be loaded on return.
 *
 * @param image_asset_name The name of the image asset to load and use for the texture.
 * @return The loaded texture. Can be the default texture if not found. INVALID_ID_U16 if error.
 */
KAPI ktexture texture_acquire_sync(const char* image_asset_name);

/**
 * @brief Releases resources for the given texture.
 *
 * @param t A pointer to the texture to be released.
 */
KAPI void texture_release(ktexture texture);

/**
 * @brief Attempts to acquire a texture using the given image asset name, loaded from the provided package. Asynchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * The image_asset_name is also used as the texture name. Loads from the application package.
 * Texture will be auto-released when reference count reaches 0.
 * Texture will be of KTEXTURE_TYPE_2D.
 *
 * This function is considered to be asynchronous, and invokes the given callback when the texture is loaded.
 *
 * @param image_asset_name The name of the image asset to load and use for the texture.
 * @param package_name The name of the package to load the texture image asset from.
 * @param listener A structure containing data to be passed along to the callback when the image is loaded. Optional.
 * @param callback The callback to be made once the texture is loaded. Optional.
 * @return The texture to be loaded. The default texture if not found. INVALID_ID_U16 if error. May/may not be loaded yet on return.
 */
KAPI ktexture texture_acquire_from_package(const char* image_asset_name, const char* package_name, void* listener, PFN_texture_loaded_callback callback);

/**
 * @brief Attempts to acquire a texture using the given image asset name, loaded from the provided package. Synchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * The image_asset_name is also used as the texture name. Loads from the application package.
 * Texture will be auto-released when reference count reaches 0.
 * Texture will be of KTEXTURE_TYPE_2D.
 *
 * This function is considered to be synchronous and is guaranteed to be loaded on return.
 *
 * @param image_asset_name The name of the image asset to load and use for the texture.
 * @param package_name The name of the package to load the texture image asset from.
 * @return The loaded texture. Can be the default texture if not found. INVALID_ID_U16 if error.
 */
KAPI ktexture texture_acquire_from_package_sync(const char* image_asset_name, const char* package_name);

/**
 * @brief Attempts to acquire a cubemap texture with the given image asset name. Asynchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default cubemap texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * Requires textures with image_asset_name_prefix as the base, one for each side of a cube.
 * - name_f Front
 * - name_b Back
 * - name_u Up
 * - name_d Down
 * - name_r Right
 * - name_l Left
 *
 * For example, "skybox_f.png", "skybox_b.png", etc. where image_asset_name_prefix is "skybox".
 *
 * This function is considered to be asynchronous, and invokes the given callback when the texture is loaded.
 *
 * @param image_asset_name_prefix The prefix of the name of the image assets to load and use for the texture.
 * @param listener The object listening for the callback to be made once the resource is loaded. Optional.
 * @param callback The callback to be made once the resource is loaded. Optional.
 * @return The texture to be loaded. The default texture if not found. INVALID_ID_U16 if error. May/may not be loaded yet on return.
 */
KAPI ktexture texture_cubemap_acquire(const char* image_asset_name_prefix, void* listener, PFN_texture_loaded_callback callback);

/**
 * @brief Attempts to acquire a cubemap texture with the given image asset name. Synchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default cubemap texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * Requires textures with image_asset_name_prefix as the base, one for each side of a cube.
 * - name_f Front
 * - name_b Back
 * - name_u Up
 * - name_d Down
 * - name_r Right
 * - name_l Left
 *
 * For example, "skybox_f.png", "skybox_b.png", etc. where image_asset_name_prefix is "skybox".
 *
 * This function is considered to be synchronous and is guaranteed to be loaded on return.
 *
 * @param image_asset_name_prefix The prefix of the name of the image assets to load and use for the texture.
 * @return The loaded texture. Can be the default texture if not found. INVALID_ID_U16 if error.
 */
KAPI ktexture texture_cubemap_acquire_sync(const char* image_asset_name_prefix);

/**
 * @brief Attempts to acquire a cubemap texture with the given image asset name from the provided package. Asynchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default cubemap texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * Requires textures with image_asset_name_prefix as the base, one for each side of a cube.
 * - name_f Front
 * - name_b Back
 * - name_u Up
 * - name_d Down
 * - name_r Right
 * - name_l Left
 *
 * For example, "skybox_f.png", "skybox_b.png", etc. where image_asset_name_prefix is "skybox".
 *
 * This function is considered to be asynchronous, and invokes the given callback when the texture is loaded.
 *
 * @param image_asset_name_prefix The prefix of the name of the image assets to load and use for the texture.
 * @param package_name The name of the package from which to load the image assets from.
 * @param listener The object listening for the callback to be made once the resource is loaded. Optional.
 * @param callback The callback to be made once the resource is loaded. Optional.
 * @return The texture to be loaded. The default texture if not found. INVALID_ID_U16 if error. May/may not be loaded yet on return.
 */
KAPI ktexture texture_cubemap_acquire_from_package(const char* image_asset_name_prefix, const char* package_name, void* listener, PFN_texture_loaded_callback callback);

/**
 * @brief Attempts to acquire a cubemap texture with the given image asset name from the provided package. Synchronous.
 *
 * If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default cubemap texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * Requires textures with image_asset_name_prefix as the base, one for each side of a cube.
 * - name_f Front
 * - name_b Back
 * - name_u Up
 * - name_d Down
 * - name_r Right
 * - name_l Left
 *
 * For example, "skybox_f.png", "skybox_b.png", etc. where image_asset_name_prefix is "skybox".
 *
 * This function is considered to be synchronous and is guaranteed to be loaded on return.
 *
 * @param image_asset_name_prefix The prefix of the name of the image assets to load and use for the texture.
 * @param package_name The name of the package from which to load the image assets from.
 * @return The loaded texture. Can be the default texture if not found. INVALID_ID_U16 if error.
 */
KAPI ktexture texture_cubemap_acquire_from_package_sync(const char* image_asset_name_prefix, const char* package_name);

// Easier idea? synchronous. auto_release=true, default options
KAPI ktexture texture_acquire_from_image(const struct kasset_image* image, const char* name);

KAPI ktexture texture_acquire_from_pixel_data(kpixel_format format, u32 pixel_array_size, void* pixels, u32 width, u32 height, const char* name);

KAPI ktexture texture_cubemap_acquire_from_pixel_data(kpixel_format format, u32 pixel_array_size, void* pixels, u32 width, u32 height, const char* name);

/* KAPI ktexture texture_cubemap_acquire_from_images(const struct kasset_image* images[6]); */

typedef struct ktexture_load_options {
    b8 is_writeable;
    b8 is_depth;
    b8 is_stencil;
    b8 multiframe_buffering;
    // Unload from GPU when reference count reaches 0.
    b8 auto_release;
    kpixel_format format;
    ktexture_type type;
    u32 width;
    u32 height;
    // Set to 0 to calculate mip levels based on size.
    u8 mip_levels;
    union {
        u32 depth;
        u32 layer_count;
    };
    const char* name;
    // The name of the image asset to load for the texture. Optional. Only used for single-layer textures and cubemaps. Ignored for layered textures.
    const char* image_asset_name;
    // The name of the image asset to load for the texture. Optional. Only used for single-layer textures and cubemaps. Ignored for layered textures.
    const char* package_name;
    // Names of layer image assets, only used for array/layered textures. Element count must be layer_count.
    const char** layer_image_asset_names;
    // Names of packages containing layer image assets, only used for array/layered textures. Element count must be layer_count. Use null/0 to load from application package.
    const char** layer_package_names;

    // Block of pixel data, which can be multiple layers as defined by layer_count. The pixel data for all layers should be contiguous. Layout interpreted based on format.
    void* pixel_data;
    // The size of the pixel_data array in bytes (NOT pixel count!)
    u32 pixel_array_size;
} ktexture_load_options;

KAPI ktexture texture_acquire_with_options(ktexture_load_options options, void* listener, PFN_texture_loaded_callback callback);
KAPI ktexture texture_acquire_with_options_sync(ktexture_load_options options);

/**
 * @brief Resizes the given texture. May only be done on writeable textures.
 * Potentially regenerates internal data, if configured to do so.
 *
 * @param t The texture to be resized.
 * @param width The new width in pixels.
 * @param height The new height in pixels.
 * @param regenerate_internal_data Indicates if the internal data should be regenerated.
 * @return True on success; otherwise false.
 */
KAPI b8 texture_resize(ktexture t, u32 width, u32 height, b8 regenerate_internal_data);

/**
 * @brief Writes the given data to the provided texture. May only be used on
 * writeable textures.
 *
 * @param t The texture to be written to.
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param data A pointer to the data to be written.
 * @return True on success; otherwise false.
 */
KAPI b8 texture_write_data(ktexture t, u32 offset, u32 size, void* data);
