#pragma once

#include "defines.h"

struct kasset;
struct kasset_importer;

KAPI b8 kasset_importer_skeletalmesh_gltf_import(const struct kasset_importer* self, u64 data_size, const void* data, void* params, struct kasset* out_asset);
