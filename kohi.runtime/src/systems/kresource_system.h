#pragma once

#include "kresources/kresource_types.h"

struct kresource_system_state;

typedef struct kresource_system_config {
    u32 dummy;
} kresource_system_config;

typedef struct kresource_handler {
    struct asset_system_state* asset_system;
    b8 (*request)(struct kresource_handler* self, kresource* resource, const struct kresource_request_info* info);
    void (*release)(struct kresource_handler* self, kresource* resource);
} kresource_handler;

KAPI b8 kresource_system_initialize(u64* memory_requirement, struct kresource_system_state* state, const kresource_system_config* config);
KAPI void kresource_system_shutdown(struct kresource_system_state* state);

KAPI b8 kresource_system_request(struct kresource_system_state* state, kname name, const struct kresource_request_info* info, kresource* out_resource);
KAPI void kresource_system_release(struct kresource_system_state* state, kresource* resource);

KAPI b8 kresource_system_handler_register(struct kresource_system_state* state, kresource_type type, kresource_handler handler);
