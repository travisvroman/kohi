#pragma once

#include "assets/kasset_types.h"

KAPI void* kasset_binary_image_serialize(const kasset_image* asset, u64* out_size);

KAPI b8 kasset_binary_image_deserialize(u64 size, void* block, kasset_image* out_image);
