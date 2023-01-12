#pragma once

#include "defines.h"

b8 kvar_initialize(u64* memory_requirement, void* memory, void* config);
void kvar_shutdown(void* state);

KAPI b8 kvar_create_int(const char* name, i32 value);
KAPI b8 kvar_get_int(const char* name, i32* out_value);
KAPI b8 kvar_set_int(const char* name, i32 value);
