#pragma once

#include "assets/kasset_types.h"

struct kasset;
struct vfs_state;

typedef struct asset_handler {
    kasset_type type;
    const char* type_name;
    b8 is_binary;

    /** @brief Cache a pointer to the VFS state for fast lookup. */
    struct vfs_state* vfs;

    /**
     * @brief Requests an asset from the given handler.
     */
    void (*request_asset)(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback);

    void (*release_asset)(struct asset_handler* self, struct kasset* asset);

    /**
     * @brief Attempts to serialize the asset into a binary blob.
     * NOTE: allocates memory that should be freed by the caller.
     *
     * @param asset A constant pointer to the asset to be serialized. Required.
     * @param out_size A pointer to hold the size of the serialized block of memory. Required.
     * @returns A block of memory containing the serialized asset on success; 0 on failure.
     */
    void* (*binary_serialize)(const kasset* asset, u64* out_size);

    /**
     * @brief Attempts to deserialize the given block of memory into an asset.
     *
     * @param size The size of the serialized block in bytes. Required.
     * @param block A constant pointer to the block of memory to deserialize. Required.
     * @param out_asset A pointer to the asset to deserialize to. Required.
     * @returns True on success; otherwise false.
     */
    b8 (*binary_deserialize)(u64 size, const void* block, kasset* out_asset);

    /**
     * @brief Attempts to serialize the asset into a string of text.
     * NOTE: allocates memory that should be freed by the caller.
     *
     * @param asset A constant pointer to the asset to be serialized. Required.
     * @returns A string of text containing the serialized asset on success; 0 on failure.
     */
    const char* (*text_serialize)(const kasset* asset);

    /**
     * @brief Attempts to deserialize the given string of text into an asset.
     *
     * @param file_text A constant pointer to the string of text to deserialize. Required.
     * @param out_asset A pointer to the asset to deserialize to. Required.
     * @returns True on success; otherwise false.
     */
    b8 (*text_deserialize)(const char* file_text, kasset* out_asset);
} asset_handler;

typedef struct asset_handler_request_context {
    struct asset_handler* handler;
    void* listener_instance;
    PFN_kasset_on_result user_callback;
    struct kasset* asset;
} asset_handler_request_context;
