#pragma once

#include "defines.h"
#include "strings/kname.h"

KAPI b8 kasset_bitmap_font_fnt_import(kname output_asset_name, kname output_package_name, u64 data_size, const void* data, void* params);
