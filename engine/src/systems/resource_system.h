#pragma once

#include "resources/resource_types.h"

typedef struct resource_system_config {
    u32 max_loader_count;
    // The relative base path for assets.
    char* asset_base_path;
} resource_system_config;

typedef struct resource_loader {
    u32 id;
    resource_type type;
    const char* custom_type;
    const char* type_path;
    b8 (*load)(struct resource_loader* self, const char* name, resource* out_resource);
    void (*unload)(struct resource_loader* self, resource* resource);
} resource_loader;

b8 resource_system_initialize(u64* memory_requirement, void* state, resource_system_config config);
void resource_system_shutdown(void* state);

KAPI b8 resource_system_register_loader(resource_loader loader);

KAPI b8 resource_system_load(const char* name, resource_type type, resource* out_resource);
KAPI b8 resource_system_load_custom(const char* name, const char* custom_type, resource* out_resource);

KAPI void resource_system_unload(resource* resource);

KAPI const char* resource_system_base_path();
