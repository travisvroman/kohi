

#pragma once

#include "defines.h"
#include "renderer/renderer_types.inl"

typedef struct shader_system_config {
    u16 max_shader_count;
} shader_system_config;

b8 shader_system_initialize(u64* memory_requirement, void* memory, shader_system_config config);
void shader_system_shutdown(void* state);

KAPI b8 shader_system_create(const shader_config* config);

KAPI b8 shader_system_use(const char* shader_name);
