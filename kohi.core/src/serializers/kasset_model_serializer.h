#pragma once

#include "assets/kasset_types.h"

/**
 * @brief Attempts to serialize the asset into a binary blob.
 * NOTE: allocates memory that should be freed by the caller.
 *
 * @param asset A constant pointer to the asset to be serialized. Required.
 * @param out_size A pointer to hold the size of the serialized block of memory. Required.
 * @param exporter_type Specifies the type of the exporter for debugging purposes.
 * @param exporter_version Specifies the version of the exporter for debugging purposes.
 * @returns A block of memory containing the serialized asset on success; 0 on failure.
 */
KAPI void *kasset_model_serialize (const kasset_model *asset, u32 exporter_type, u8 exporter_version, u64 *out_size);

/**
 * @brief Attempts to deserialize the given block of memory into an model asset.
 *
 * @param size The size of the serialized block in bytes. Required.
 * @param block A constant pointer to the block of memory to deserialize. Required.
 * @param out_asset A pointer to the asset to deserialize to. Required.
 * @returns True on success; otherwise false.
 */
KAPI b8 kasset_model_deserialize (u64 size, const void *block, kasset_model *out_asset);
