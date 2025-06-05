#pragma once

#include "defines.h"

// NOTE: extension requires prepended "."
KAPI b8 kasset_audio_import(const char* output_directory, const char* output_filename, u64 data_size, const void* data, const char* extension);
