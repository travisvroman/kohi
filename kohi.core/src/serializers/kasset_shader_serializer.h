#pragma once

#include "assets/kasset_types.h"

KAPI const char* kasset_shader_serialize(const kasset_shader* asset);

KAPI b8 kasset_shader_deserialize(const char* file_text, kasset_shader* out_asset);
