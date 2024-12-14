
#pragma once

#include "defines.h"

struct kasset;
struct kasset_importer;

KAPI b8 kasset_importer_bitmap_font_fnt(const struct kasset_importer* self, u64 data_size, const void* data, void* params, struct kasset* out_asset);
