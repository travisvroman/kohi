#pragma once

#include "assets/kasset_types.h"

KAPI const char* kasset_scene_serialize(const kasset_scene* asset);

KAPI b8 kasset_scene_deserialize(const char* file_text, kasset_scene* out_asset);
