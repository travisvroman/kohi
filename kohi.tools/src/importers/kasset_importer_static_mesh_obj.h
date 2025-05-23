#pragma once

#include "defines.h"
#include "strings/kname.h"

KAPI b8 kasset_static_mesh_obj_import(kname output_asset_name, kname output_package_name, const char* data, u32* out_material_file_count, const char*** out_material_file_names);
