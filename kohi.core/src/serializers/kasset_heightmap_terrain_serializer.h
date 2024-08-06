#pragma once

#include "assets/kasset_types.h"

KAPI const char* kasset_heightmap_serialize(const kasset* asset);

KAPI b8 kasset_heightmap_deserialize(const char* file_text, kasset* out_asset);
