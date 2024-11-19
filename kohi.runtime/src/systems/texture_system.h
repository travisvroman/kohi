/**
 * @file texture_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the texture system, which handles the acquisition
 * and releasing of textures. It also reference monitors textures, and can
 * auto-release them when they no longer have any references, if configured to
 * do so.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "kresources/kresource_types.h"

struct texture_system_state;

/** @brief The texture system configuration */
typedef struct texture_system_config {
    /** @brief The maximum number of textures that can be loaded at once. */
    u32 max_texture_count;
} texture_system_config;

/** @brief The default texture name. */
#define DEFAULT_TEXTURE_NAME "default"

/** @brief The default diffuse texture name. */
#define DEFAULT_DIFFUSE_TEXTURE_NAME "default_DIFF"

/** @brief The default specular texture name. */
#define DEFAULT_SPECULAR_TEXTURE_NAME "default_SPEC"

/** @brief The default normal texture name. */
#define DEFAULT_NORMAL_TEXTURE_NAME "default_NORM"

/** @brief The default combined (metallic, roughness, AO) texture name. */
#define DEFAULT_COMBINED_TEXTURE_NAME "default_COMBINED"

/** @brief The default cube texture name. */
#define DEFAULT_CUBE_TEXTURE_NAME "default_cube"

/** @brief The default terrain texture name. */
#define DEFAULT_TERRAIN_TEXTURE_NAME "default_TERRAIN"

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
 *
 * @param state The state block of memory for this system.
 */
void texture_system_shutdown(void* state);

KAPI kresource_texture* texture_system_request(kname name, kname package_name, void* listener, PFN_resource_loaded_user_callback callback);

/**
 * @brief Attempts to acquire a cubemap texture with the given name. If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * Requires textures with name as the base, one for each side of a cube, in the following order:
 * - name_f Front
 * - name_b Back
 * - name_u Up
 * - name_d Down
 * - name_r Right
 * - name_l Left
 *
 * For example, "skybox_f.png", "skybox_b.png", etc. where name is "skybox".
 *
 * @param name The name of the texture to find. Used as a base string for actual texture names.
 * @param auto_release Indicates if the texture should auto-release when its reference count is 0.
 * Only takes effect the first time the texture is acquired.
 * @param listener The object listening for the callback to be made once the resource is loaded. Optional.
 * @param callback The callback to be made once the resource is loaded. Optional.
 * @return A pointer to the loaded texture. Can be a pointer to the default texture if not found.
 */
KAPI kresource_texture* texture_system_request_cube(kname name, b8 auto_release, b8 multiframe_buffering, void* listener, PFN_resource_loaded_user_callback callback);

/**
 * Requests a writeable cubemap texture.
 *
 * @param name The name of the texture.
 * @param dimension The size of each side of the cubemap. Used for both width and height.
 * @param auto_release Indicates if the resource will be released automatically once its reference count reaches 0.
 * @returns A pointer to the texture.
 */
KAPI kresource_texture* texture_system_request_cube_writeable(kname name, u32 dimension, b8 auto_release, b8 multiframe_buffering);

/**
 * Requests a depth cubemap texture.
 *
 * @param name The name of the texture.
 * @param dimension The size of each side of the cubemap. Used for both width and height.
 * @param auto_release Indicates if the resource will be released automatically once its reference count reaches 0.
 * @returns A pointer to the texture.
 */
KAPI kresource_texture* texture_system_request_cube_depth(kname name, u32 dimension, b8 auto_release, b8 multiframe_buffering);

/**
 * @brief Requests a writeable texture with the given name. This does not point to
 * nor attempt to load an image asset file.
 *
 * @param name The name of the texture to acquire.
 * @param width The texture width in pixels.
 * @param height The texture height in pixels.
 * @param format The texture format.
 * @param has_transparency Indicates if the texture will have transparency.
 * @return A pointer to the texture resource on success; otherwise 0/null.
 */
KAPI kresource_texture* texture_system_request_writeable(kname name, u32 width, u32 height, kresource_texture_format format, b8 has_transparency, b8 multiframe_buffering);

/**
 * @brief Attempts to acquire a writeable array texture with the given name. This does not point to
 * nor attempt to load a texture file. Does also increment the reference counter.
 *
 * @param name The name of the texture to acquire.
 * @param width The texture width in pixels.
 * @param height The texture height in pixels.
 * @param format The texture format.
 * @param has_transparency Indicates if the texture will have transparency.
 * @param type The texture type.
 * @param array_size The number of "layers" in the texture.
 * @return A pointer to the texture resource on success; otherwise 0/null.
 */
KAPI kresource_texture* texture_system_request_writeable_arrayed(kname name, u32 width, u32 height, kresource_texture_format format, b8 has_transparency, b8 multiframe_buffering, kresource_texture_type type, u16 array_size);

/**
 * @brief Requests a depth texture with the given name.
 *
 * @param name The name of the texture to acquire.
 * @param width The texture width in pixels.
 * @param height The texture height in pixels.
 * @return A pointer to the texture resource on success; otherwise 0/null.
 */
KAPI kresource_texture* texture_system_request_depth(kname name, u32 width, u32 height, b8 multiframe_buffering);

/**
 * @brief Attempts to acquire a depth array texture with the given name.
 *
 * @param name The name of the texture to acquire.
 * @param width The texture width in pixels.
 * @param height The texture height in pixels.
 * @param array_size The number of "layers" in the texture.
 * @return A pointer to the texture resource on success; otherwise 0/null.
 */
KAPI kresource_texture* texture_system_request_depth_arrayed(kname name, u32 width, u32 height, u16 array_size, b8 multiframe_buffering);

/**
 * @brief Attempts to acquire an array texture with the given name. This uses the provided array
 * of texture names to load data from each in its own layer. All textures must be be of the same size.
 * Size is determined by the first file in the list.
 *
 * @param name The name of the texture to acquire. Not tied to a filename, only used for lookups.
 * @param package_name The name of the package from which to acquire the textures.
 * @param layer_count The number of layers in the array texture (Must be at least 1)
 * @param layer_asset_names The names of the image assets to load, one per layer. All image assets must be the same dimension. Size of array must match layer_count.
 * @param auto_release Indicates if the texture will have its resources automatically released when the last reference is released.
 * @param listener A pointer to something that requires the callback to be made once the resource is loaded. Optional.
 * @param callback A callback to be made once the resource is loaded. Optional.
 * @return A pointer to the generated texture.
 */
KAPI kresource_texture* texture_system_acquire_textures_as_arrayed(kname name, kname package_name, u32 layer_count, kname* layer_asset_names, b8 auto_release, b8 multiframe_buffering, void* listener, PFN_resource_loaded_user_callback callback);

KAPI void texture_system_release_resource(kresource_texture* t);

/**
 * @brief Resizes the given texture. May only be done on writeable textures.
 * Potentially regenerates internal data, if configured to do so.
 *
 * @param t A pointer to the texture to be resized.
 * @param width The new width in pixels.
 * @param height The new height in pixels.
 * @param regenerate_internal_data Indicates if the internal data should be regenerated.
 * @return True on success; otherwise false.
 */
KAPI b8 texture_system_resize(kresource_texture* t, u32 width, u32 height, b8 regenerate_internal_data);

/**
 * @brief Writes the given data to the provided texture. May only be used on
 * writeable textures.
 *
 * @param t A pointer to the texture to be written to.
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param data A pointer to the data to be written.
 * @return True on success; otherwise false.
 */
KAPI b8 texture_system_write_data(kresource_texture* t, u32 offset, u32 size, void* data);

/**
 * @brief Gets a pointer to the default texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to the default diffuse texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_diffuse_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to the default specular texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_specular_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to the default normal texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_normal_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to the default combined (metallic, roughness, AO) texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_combined_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to the default cube texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_cube_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to the default terrain texture.
 * @param state A pointer to the texture system state.
 * @returns A pointer to the texture.
 */
KAPI const kresource_texture* texture_system_get_default_kresource_terrain_texture(struct texture_system_state* state);

/**
 * @brief Gets a pointer to either the internal data of the supplied texture if loaded,
 * or one to the internal of a default texture of the appropriate type. If a default texture
 * is used, out_generation will be set to INVALID_ID. If an invalid texture is passed, 0/null
 * will be returned.
 *
 * @param t A pointer to the texture whose internal data will be fetched.
 * @param out_generation A pointer to hold the generation of the texture.
 * @returns A handle to texture internal data if successful, otherwise invalid handle.
 */
KAPI khandle texture_system_resource_get_internal_or_default(const kresource_texture* t, u32* out_generation);
