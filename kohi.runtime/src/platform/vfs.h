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

#include "assets/kasset_types.h"
#include "defines.h"
#include "strings/kname.h"

struct kpackage;
struct kasset;
struct kasset_metadata;
struct vfs_state;

typedef struct vfs_config {
    const char** text_user_types;
    const char* manifest_file_path;
} vfs_config;

typedef enum vfs_asset_flag_bits {
    VFS_ASSET_FLAG_NONE = 0,
    VFS_ASSET_FLAG_BINARY_BIT = 0x01,
} vfs_asset_flag_bits;

typedef u32 vfs_asset_flags;

typedef enum vfs_request_result {
    /** The request was fulfilled successfully. */
    VFS_REQUEST_RESULT_SUCCESS = 0,
    /** The asset exists on the manifest, but the primary file could not be found on disk. */
    VFS_REQUEST_RESULT_FILE_DOES_NOT_EXIST,
    /** The package does not contain the asset */
    VFS_REQUEST_RESULT_NOT_IN_PACKAGE,
    /** Package does not exist. */
    VFS_REQUEST_RESULT_PACKAGE_DOES_NOT_EXIST,
    /** There was an error reading from the file. */
    VFS_REQUEST_RESULT_READ_ERROR,
    /** There was an error writing to the file. */
    VFS_REQUEST_RESULT_WRITE_ERROR,
    /** An internal failure has occurred in the VFS itself. */
    VFS_REQUEST_RESULT_INTERNAL_FAILURE,
} vfs_request_result;

/**
 * @brief Represents data and properties from an asset loaded from the VFS.
 */
typedef struct vfs_asset_data {
    /** @brief The name of the asset stored as a kname. */
    kname asset_name;
    /** @brief The name of the package containing the asset, stored as a kname. */
    kname package_name;
    /** @brief A copy of the asset path. */
    const char* path;

    /** @brief The size of the asset in bytes. */
    u64 size;
    union {
        /** The asset data as a string, if a text asset. Zero-terminated. */
        const char* text;
        /** The binary asset data, if a binary asset. */
        const void* bytes;
    };
    /** @brief Various flags for the given asset. */
    vfs_asset_flags flags;

    /** The result of the asset load operation. */
    vfs_request_result result;

    /** The size of the context in bytes. */
    u32 context_size;

    /** The context passed in from the original request. */
    void* context;
} vfs_asset_data;

typedef void (*PFN_on_asset_loaded_callback)(struct vfs_state* vfs, vfs_asset_data asset_data);

typedef struct vfs_state {
    // darray
    struct kpackage* packages;
} vfs_state;

/**
 * @brief The request options for getting an asset from the VFS.
 */
typedef struct vfs_request_info {
    /** @brief The name of the package to load the asset from. */
    kname package_name;
    /** @brief The name of the asset to request. */
    kname asset_name;
    /** @brief Indicates if the asset is binary. If not, the asset is loaded as text. */
    b8 is_binary;
    /** @brief The size of the context in bytes. */
    u32 context_size;
    /** @param context A constant pointer to the context to be used for this call. This is passed through to the result callback. NOTE: A copy of this is taken immediately, so lifetime of this isn't important. */
    const void* context;

    PFN_on_asset_loaded_callback vfs_callback;
} vfs_request_info;

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
 * @param info The information detailing specifics about the VFS asset request.
 * @param callback The callback to be made once the asset load is complete. Required.
 */
KAPI void vfs_request_asset(vfs_state* state, vfs_request_info info);

/**
 * @brief Requests an asset from the VFS synchronously. NOTE: This should be used sparingly as it performs device I/O directly.
 * NOTE: Caller should also check out_data context to see if it needs freeing, as this is _not_ handled automatically as it is in the async version.
 *
 * @param state A pointer to the system state. Required.
 * @param info The information detailing specifics about the VFS asset request.
 * @param out_data A pointer to hold the loaded asset data. Required.
 */
KAPI vfs_asset_data vfs_request_asset_sync(vfs_state* state, vfs_request_info info);

/**
 * @brief Attempts to retrieve the path for the given asset, if it exists.
 *
 * @param state A pointer to the system state. Required.
 * @param package_name The package name to request from.
 * @param asset_name The name of the asset to request.
 * @returns The path for the asset, if it exists. Otherwise 0/null.
 */
KAPI const char* vfs_path_for_asset(vfs_state* state, kname package_name, kname asset_name);

/**
 * @brief Attempts to retrieve the source path for the given asset, if one exists.
 *
 * @param state A pointer to the system state. Required.
 * @param package_name The package name to request from.
 * @param asset_name The name of the asset to request.
 * @returns The source path for the asset, if it exists. Otherwise 0/null.
 */
KAPI const char* vfs_source_path_for_asset(vfs_state* state, kname package_name, kname asset_name);

/**
 * @brief Requests an asset directly a disk path via the VFS, issuing the callback when complete. This call is asynchronous.
 *
 * @param state A pointer to the system state. Required.
 * @param path The path to the file to load (can be relative or absolute). Required.
 * @param is_binary Indicates if the asset is binary. Otherwise is loaded as text.
 * @param context_size The size of the context in bytes.
 * @param context A pointer to the context to be used for this call. This is passed through to the result callback. NOTE: A copy of this is taken immediately, so lifetime of this isn't important.
 * @param callback The callback to be made once the asset load is complete. Required.
 */
KAPI void vfs_request_direct_from_disk(vfs_state* state, const char* path, b8 is_binary, u32 context_size, const void* context, PFN_on_asset_loaded_callback callback);

/**
 * @brief Requests an asset directly a disk path via the VFS synchronously. NOTE: This should be used sparingly as it performs device I/O directly.
 * NOTE: Caller should also check out_data context to see if it needs freeing, as this is _not_ handled automatically as it is in the async version.
 *
 * @param state A pointer to the system state. Required.
 * @param path The path to the file to load (can be relative or absolute). Required.
 * @param is_binary Indicates if the asset is binary. Otherwise is loaded as text.
 * @param context_size The size of the context in bytes.
 * @param context A pointer to the context to be used for this call. This is passed through to the result callback. NOTE: A copy of this is taken immediately, so lifetime of this isn't important.
 * @param out_data A pointer to hold the loaded asset data. Required.
 */
KAPI void vfs_request_direct_from_disk_sync(vfs_state* state, const char* path, b8 is_binary, u32 context_size, const void* context, vfs_asset_data* out_data);

/**
 * @brief Attempts to write the provided binary data to the VFS (or package).
 *
 * @param state A pointer to the system state. Required.
 * @param asset_name The name of the asset to be written within the given package.
 * @param package_name The name of the package to write the data into.
 * @param size The size of the data to be written.
 * @param data A constant pointer to the block of data to be written.
 * @returns True on success; otherwise false.
 */
KAPI b8 vfs_asset_write_binary(vfs_state* state, kname asset_name, kname package_name, u64 size, const void* data);

/**
 * @brief Attempts to write the provided text data to the VFS (or package).
 *
 * @param state A pointer to the system state. Required.
 * @param asset_name The name of the asset to be written within the given package.
 * @param package_name The name of the package to write the data into.
 * @param size The size of the data to be written.
 * @param data A constant pointer to the block of data to be written.
 * @returns True on success; otherwise false.
 */
KAPI b8 vfs_asset_write_text(vfs_state* state, kname asset_name, kname package_name, const char* text);

/**
 * @brief Releases resources held by data. NOTE: This does _NOT_ account for any dynamic allocations made within said context!
 *
 * @param data A pointer to the VFS data to be released.
 */
KAPI void vfs_asset_data_cleanup(vfs_asset_data* data);

#if KOHI_HOT_RELOAD
/**
 * @brief Watches a given asset for on-disk updates. Only available when hot-reloading of assets is enabled.
 * 
 * @param state A pointer to the VFS state.
 * @param asset_name The name of the asset to watch.
 * @param package_name The name of the package containing the asset.
 * @param is_binary Indicates if the asset being watched is a binary asset.
 * @return A watch id associated with the file watch. INVALID_ID_U32 if the watch failed for any reason (i.e. invalid asset/package name).
 */
KAPI u32 vfs_asset_watch(vfs_state* state, kname asset_name, kname package_name, b8 is_binary);

/**
 * @brief Unwatches an asset with the given id.
 * 
 * @param state A pointer to the VFS state.
 * @param watch_id The id of the watch to stop.
 * @return True if successful; otherwise false.
 */
KAPI b8 vfs_asset_unwatch(vfs_state* state, u32 watch_id);
#endif
