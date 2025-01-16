#pragma once

#include "assets/kasset_types.h"

KAPI const char* kasset_material_serialize(const kasset* asset);

KAPI b8 kasset_material_deserialize(const char* file_text, kasset* out_asset);
