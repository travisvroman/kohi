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

typedef enum asset_request_result {
    /** The asset load was a success, including any GPU operations (if required). */
    ASSET_REQUEST_RESULT_SUCCESS,
    /** The specified package name was invalid or not found. */
    ASSET_REQUEST_RESULT_INVALID_PACKAGE,
    /** The specified asset type was invalid or not found. */
    ASSET_REQUEST_RESULT_INVALID_ASSET_TYPE,
    /** The specified asset name was invalid or not found. */
    ASSET_REQUEST_RESULT_INVALID_NAME,
    /** The asset was found, but failed to load during the parsing stage. */
    ASSET_REQUEST_RESULT_PARSE_FAILED,
    /** The asset was found, but failed to load during the GPU upload stage. */
    ASSET_REQUEST_RESULT_GPU_UPLOAD_FAILED,
    /** An internal system failure has occurred. See logs for details. */
    ASSET_REQUEST_RESULT_INTERNAL_FAILURE,
    /** No handler exists for the given asset. See logs for details. */
    ASSET_REQUEST_RESULT_NO_HANDLER,
    /** The total number of result options in this enumeration. Not an actual result value */
    ASSET_REQUEST_RESULT_COUNT
} asset_request_result;

typedef struct asset_system_config {
    // The maximum number of assets which may be loaded at once.
    u32 max_asset_count;
} asset_system_config;

/**
 * @brief A function pointer typedef to be used to provide the asset asset_system
 * with a calback function when asset loading is complete or failed. This process is asynchronus.
 *
 * @param result The result of the asset request.
 * @param asset A constant pointer to the asset that is loaded.
 * @param listener_inst A pointer to the listener, usually passed along with the original request.
 */
typedef void (*PFN_kasset_on_result)(asset_request_result result, const kasset* asset, void* listener_inst);

struct asset_system_state;

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
 * @brief Requests an asset by fully-qualified name. This operation is asynchronus, and will provide its result via a
 * callback (if provided) at a later time. Internally, a reference count for each asset is maintained each time the asset
 * is requested. If the asset's first request had auto-release set to true, it will be released automatically when this
 * count reaches 0.
 *
 * @param A pointer to the asset system state. Required.
 * @param fully_qualified_name The fully-qualified asset name, typically in the format of "<package_name>.<asset_type>.<asset_name>". Ex: "Testbed.Texture.Rock01". Required.
 * @param auto_release Indicates if the asset should be released automatically when its internal reference counter reaches 0. Only has an effect the first time the asset is requested.
 * @param listener_inst A pointer to the listener instance that is awaiting the asset. Technically optional as perhaps nothing is interested in the result, but hwhy?
 * @param callback A pointer to the function to be called when the load is complete (or failed). Technically optional as perhaps nothing is interested in the result, but hwhy?
 */
KAPI void asset_system_request(struct asset_system_state* state, const char* fully_qualified_name, b8 auto_release, void* listener_inst, PFN_kasset_on_result callback);

/**
 * @brief Releases an asset via the fully-qualified name.
 *
 * @param A pointer to the asset system state. Required.
 * @param name The fully-qualified asset name, typically in the format of "<package_name>.<asset_type>.<asset_name>". Ex: "Testbed.Texture.Rock01". Required.
 */
KAPI void asset_system_release(struct asset_system_state* state, const char* name);

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
