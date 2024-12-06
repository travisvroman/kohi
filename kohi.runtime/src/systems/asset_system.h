/**
 * @file asset_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains the implementation of the asset system, which
 * is responsible for managing the lifecycle of assets.
 *
 * @details
 * @version 1.0
 * @date 2024-07-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include <assets/kasset_types.h>
#include <strings/kname.h>

typedef struct asset_system_config {
    // The maximum number of assets which may be loaded at once.
    u32 max_asset_count;
} asset_system_config;

struct asset_system_state;

typedef struct asset_request_info {
    /** @param type The asset type. */
    kasset_type type;
    /** @param package_name The name of the package. */
    kname package_name;
    /** @param asset_name The name of the asset. */
    kname asset_name;
    // If true, request is synchronous and does not return until asset is read and processed.
    b8 synchronous;
    /** @param auto_release Indicates if the asset should be released automatically when its internal reference counter reaches 0. Only has an effect the first time the asset is requested. */
    b8 auto_release;
    /** @param listener_inst A pointer to the listener instance that is awaiting the asset. Technically optional as perhaps nothing is interested in the result, but hwhy? */
    void* listener_inst;
    /** @param callback A pointer to the function to be called when the load is complete (or failed). Technically optional as perhaps nothing is interested in the result, but hwhy? */
    PFN_kasset_on_result callback;
    /** @param import_params_size Size of the import params, if used; otherwise 0. */
    u32 import_params_size;
    /** @param import_params A pointer to the inport params, if used; otherwise 0; */
    void* import_params;
    /** @param hot_reload_callback A callback to be made if the asset is hot-reloaded. Pass 0 if not used. Hot-reloaded assets are not auto-released. */
    PFN_kasset_on_hot_reload hot_reload_callback;
    /** @param hot_reload_listener A pointer to the listener data for an asset hot-reload. */
    void* hot_reload_context;
} asset_request_info;

/**
 * @brief Deserializes configuration for the asset system from the provided string.
 *
 * @param config_str The string to deserialize.
 * @param out_config A pointer to hold the deserialized config.
 * @return True on success; otherwise false.
 */
KAPI b8 asset_system_deserialize_config(const char* config_str, asset_system_config* out_config);

/**
 * @brief Initializes the asset system. Call twice; once to get the memory requirement (pass 0 to state and config) and a second
 * time passing along the state and config once allocated.
 *
 * @param memory_requirement A pointer to hold the numeric amount of bytes needed for the state. Required.
 * @param state A pointer to the state. Pass 0 when getting memory requirement, otherwise pass the block of allocated memory.
 * @param config A constant pointer to the configuration of the system. Ignored when getting memory requirement.
 * @return True on success; otherwise false.
 */
KAPI b8 asset_system_initialize(u64* memory_requirement, struct asset_system_state* state, const asset_system_config* config);

/**
 * @brief Shuts the system down.
 *
 * @param state A pointer to the state. Required.
 */
KAPI void asset_system_shutdown(struct asset_system_state* state);

/**
 * @brief Requests an asset by type, name and package name. This operation is asynchronus, and will provide its result via a
 * callback (if provided) at a later time. Internally, a reference count for each asset is maintained each time the asset
 * is requested. If the asset's first request had auto-release set to true, it will be released automatically when this
 * count reaches 0.
 *
 * @param A pointer to the asset system state. Required.
 * @param info The information about the asset request.
 */
KAPI void asset_system_request(struct asset_system_state* state, asset_request_info info);

/**
 * @brief Releases an asset via the fully-qualified name.
 *
 * @param A pointer to the asset system state. Required.
 * @param asset_name The name of the asset to be released.
 * @param package_name The name of the package containing the asset.
 */
KAPI void asset_system_release(struct asset_system_state* state, kname asset_name, kname package_name);

/**
 * @brief A callback function to be made from an asset handler when an asset is fully loaded and ready to go.
 *
 * @param A pointer to the asset system state. Required.
 * @param result The result of the load operation.
 * @param A pointer to the asset used in the operation.
 */
KAPI void asset_system_on_handler_result(struct asset_system_state* state, asset_request_result result, kasset* asset, void* listener_instance, PFN_kasset_on_result callback);

/**
 * @brief Indicates if the provided asset type is a binary asset.
 *
 * @param type The asset type.
 * @return True if binary; otherwise treated as text.
 */
KAPI b8 asset_type_is_binary(kasset_type type);
