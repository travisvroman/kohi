#pragma once

#include "defines.h"
#include "strings/kname.h"

// NOTE: extension requires prepended "."
KAPI b8 kasset_audio_import(kname output_asset_name, kname output_package_name, u64 data_size, const void* data, const char* extension);
