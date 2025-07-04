#pragma once

#include "assets/kasset_types.h"

KAPI const char* kasset_material_serialize(const kasset_material* asset);

KAPI b8 kasset_material_deserialize(const char* file_text, kasset_material* out_asset);
