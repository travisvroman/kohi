#pragma once

#include "assets/kasset_types.h"

/**
 * @brief Attempts to serialize the asset into a binary blob.
 * NOTE: allocates memory that should be freed by the caller.
 *
 * @param asset A constant pointer to the asset to be serialized. Required.
 * @param out_size A pointer to hold the size of the serialized block of memory. Required.
 * @returns A block of memory containing the serialized asset on success; 0 on failure.
 */
KAPI void* kasset_skinned_mesh_serialize(const kasset_skinned_mesh* asset, u64* out_size);

/**
 * @brief Attempts to deserialize the given block of memory into an skinned_mesh asset.
 *
 * @param size The size of the serialized block in bytes. Required.
 * @param block A constant pointer to the block of memory to deserialize. Required.
 * @param out_asset A pointer to the asset to deserialize to. Required.
 * @returns True on success; otherwise false.
 */
KAPI b8 kasset_skinned_mesh_deserialize(u64 size, const void* block, kasset_skinned_mesh* out_asset);
