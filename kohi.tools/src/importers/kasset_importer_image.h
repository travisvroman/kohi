#pragma once

#include "core_render_types.h"
#include "defines.h"

KAPI b8 kasset_image_import(const char* output_directory, const char* output_filename, u64 data_size, const void* data, b8 flip_y, kpixel_format output_format);

KAPI b8 kasset_image_import_from_path(const char* output_directory, const char* output_filename, const char* path, b8 flip_y, kpixel_format output_format);
