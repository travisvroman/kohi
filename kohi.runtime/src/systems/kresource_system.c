#include "kresource_system.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"

typedef struct kresource_system_state {
    kresource_handler handlers[KRESOURCE_TYPE_COUNT];
} kresource_system_state;

b8 kresource_system_initialize(u64* memory_requirement, struct kresource_system_state* state, const kresource_system_config* config) {
    KASSERT_MSG(memory_requirement, "Valid pointer to memory_requirement is required.");

    *memory_requirement = sizeof(kresource_system_state);

    if (!state) {
        return true;
    }

    // TODO: configure state, etc.

    return true;
}

void kresource_system_shutdown(struct kresource_system_state* state) {
    if (state) {
        // TODO: release resources, etc.
    }
}

b8 kresource_system_request(struct kresource_system_state* state, kname name, kresource_type type, kresource_request_info info, kresource* out_resource) {
    KASSERT_MSG(state && out_resource, "Valid pointers to state and out_resource are required.");

    out_resource->name = name;
    out_resource->type = type;
    out_resource->state = KRESOURCE_STATE_UNINITIALIZED;
    out_resource->generation = INVALID_ID;
    out_resource->tag_count = 0;
    out_resource->tags = 0;

    kresource_handler* h = &state->handlers[type];
    if (!h->request) {
        KERROR("There is no handler setup for the asset type.");
        return false;
    }

    b8 result = h->request(h, out_resource, info);
    if (result) {
        // TODO: Increment reference count.
    }
    return result;
}

void kresource_system_release(struct kresource_system_state* state, kresource* resource) {
    KASSERT_MSG(state && resource, "kresource_system_release requires valid pointers to state and resource.");

    // TODO: Decrement reference count. If this reaches 0, release resources/unload, etc.

    kresource_handler* h = &state->handlers[resource->type];
    if (!h->release) {
        KERROR("There is no handler setup for the asset type.");
    } else {
        h->release(h, resource);
    }
}

b8 kresource_system_handler_register(struct kresource_system_state* state, kresource_type type, kresource_handler handler) {
    if (!state) {
        KERROR("kresource_system_handler_register requires valid pointer to state.");
        return false;
    }

    kresource_handler* h = &state->handlers[type];
    if (h->request || h->release) {
        KERROR("A handler already exists for this type.");
        return false;
    }

    h->request = handler.request;
    h->release = handler.release;

    return true;
}
