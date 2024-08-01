/**
 * @file vfs.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Contains the Virtual File System (VFS), which sits atop the packaging layer and OS file I/O layer.
 * @version 1.0
 * @date 2024-07-31
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */
#pragma once

#include "defines.h"

struct kpackage;
struct kasset;
struct kasset_name;

typedef struct vfs_state {
    // darray
    struct kpackage* packages;
} vfs_state;

typedef struct vfs_config {
    const char** text_user_types;
} vfs_config;

typedef enum vfs_asset_flag_bits {
    VFS_ASSET_FLAG_NONE = 0,
    VFS_ASSET_FLAG_BINARY_BIT = 0x01
} vfs_asset_flag_bits;

typedef u32 vfs_asset_flags;

/**
 * @brief Represents data and properties from an asset loaded from the VFS.
 */
typedef struct vfs_asset_data {
    /** @brief The size of the asset in bytes. */
    u64 size;
    union {
        /** The asset data as a string, if a text asset. Zero-terminated. */
        const char* text;
        /** The binary asset data, if a binary asset. */
        void* bytes;
    };
    /** @brief Various flags for the given asset. */
    vfs_asset_flags flags;

    /** The result of the asset load operation. */
    b8 success;

    /** The size of the context in bytes. */
    u32 context_size;

    /** The context passed in from the original request. */
    void* context;
} vfs_asset_data;

typedef void (*PFN_on_asset_loaded_callback)(const char* name, vfs_asset_data asset_data);

/**
 * @brief Initializes the Virtual File System (VFS). Call twice; once to get memory requirement
 * (passing out_state = 0) and a second time passing allocated block of memory to out_state.
 *
 * @param memory_requirement A pointer to hold the memory requirement. Required.
 * @param out_state If gathering the memory requirement, pass 0. Otherwise pass the allocated block of memory to use as the system state.
 * @param config A pointer to the config. Required unless only getting memory requirement.
 * @return True on success; otherwise false.
 */
KAPI b8 vfs_initialize(u64* memory_requirement, vfs_state* out_state, const vfs_config* config);

/**
 * @brief Shuts down the VFS.
 *
 * @param state A pointer to the system state.
 */
KAPI void vfs_shutdown(vfs_state* state);

/**
 * @brief Requests an asset from the VFS, issuing the callback when complete. This call is asynchronous.
 *
 * @param state A pointer to the system state. Required.
 * @param name The fully-qualified name of the asset (i.e. "<PackageName>.<AssetType>.<AssetName>". Ex: "Testbed.Texture.Rock01"). Required.
 * @param is_binary Indicates if the asset is binary. Otherwise is loaded as text.
 * @param context_size The size of the context in bytes.
 * @param context A pointer to the context to be used for this call. This is passed through to the result callback. NOTE: A copy of this is taken immediately, so lifetime of this isn't important.
 * @param callback The callback to be made once the asset load is complete. Required.
 */
KAPI void vfs_request_asset(vfs_state* state, const struct kasset_name* name, b8 is_binary, u32 context_size, const void* context, PFN_on_asset_loaded_callback callback);

/**
 * @brief Attempts to write the provided data to the VFS (or package) for the given asset.
 *
 * @param state A pointer to the system state. Required.
 * @param asset A pointer to the asset to be written.
 * @param is_binary Indicates if the asset data is binary (ot text).
 * @param size The size of the data to be written.
 * @param data The block of data to be written.
 * @returns True on success; otherwise false.
 */
KAPI b8 vfs_asset_write(vfs_state* state, const struct kasset* asset, b8 is_binary, u64 size, void* data);
