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
KAPI void* kasset_bitmap_font_serialize(const kasset* asset, u64* out_size);

KAPI b8 kasset_bitmap_font_deserialize(const void* data, kasset* out_asset);
