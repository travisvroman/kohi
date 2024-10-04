#include "kresource_system.h"
#include "containers/u64_bst.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/handlers/kresource_handler_material.h"
#include "kresources/handlers/kresource_handler_texture.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kname.h"

struct asset_system_state;

typedef struct resource_lookup {
    // The resource itself, owned by this lookup.
    // FIXME: need a pointer to kresource here, or void* to hold whatever
    // as writing actual resource types here clobbers memory afterward.
    kresource* r;
    // The current number of references to the resource.
    i32 reference_count;
    // Indicates if the resource will be released when the reference_count reaches 0.
    b8 auto_release;
} resource_lookup;

typedef struct kresource_system_state {
    struct asset_system_state* asset_system;
    kresource_handler handlers[KRESOURCE_TYPE_COUNT];

    // Max number of resources that can be loaded at any given time.
    u32 max_resource_count;
    // An array of lookups which contain reference and release data.
    resource_lookup* lookups;
    // A BST to use for lookups of resources by kname.
    bt_node* lookup_tree;

} kresource_system_state;

static void kresource_system_release_internal(struct kresource_system_state* state, kresource* resource, b8 force_release);

b8 kresource_system_initialize(u64* memory_requirement, struct kresource_system_state* state, const kresource_system_config* config) {
    KASSERT_MSG(memory_requirement, "Valid pointer to memory_requirement is required.");

    *memory_requirement = sizeof(kresource_system_state);

    if (!state) {
        return true;
    }

    state->max_resource_count = config->max_resource_count;
    state->lookups = kallocate(sizeof(resource_lookup) * state->max_resource_count, MEMORY_TAG_ARRAY);
    state->lookup_tree = 0;

    state->asset_system = engine_systems_get()->asset_state;

    // Register known handler types
    // Texture handler
    {
        kresource_handler handler = {0};
        handler.allocate = kresource_handler_texture_allocate;
        handler.release = kresource_handler_texture_release;
        handler.request = kresource_handler_texture_request;
        if (!kresource_system_handler_register(state, KRESOURCE_TYPE_TEXTURE, handler)) {
            KERROR("Failed to register texture resource handler");
            return false;
        }
    }

    // Material handler.
    {
        kresource_handler handler = {0};
        handler.allocate = kresource_handler_material_allocate;
        handler.release = kresource_handler_material_release;
        handler.request = kresource_handler_material_request;
        if (!kresource_system_handler_register(state, KRESOURCE_TYPE_MATERIAL, handler)) {
            KERROR("Failed to register material resource handler");
            return false;
        }
    }

    KINFO("Resource system (new) initialized.");
    return true;
}

void kresource_system_shutdown(struct kresource_system_state* state) {
    if (state) {
        // release resources, etc.
        for (u32 i = 0; i < state->max_resource_count; ++i) {
            if (state->lookups[i].r) {
                kresource_system_release_internal(state, state->lookups[i].r, true);
            }
        }

        // Destroy the bst
        u64_bst_cleanup(state->lookup_tree);

        kfree(state->lookups, sizeof(resource_lookup) * state->max_resource_count, MEMORY_TAG_ARRAY);

        kzero_memory(state, sizeof(kresource_system_state));
    }
}

kresource* kresource_system_request(struct kresource_system_state* state, kname name, const struct kresource_request_info* info) {
    KASSERT_MSG(state && info, "Valid pointers to state and info are required.");

    // Attempt to find the resource by kname.
    u32 lookup_index = INVALID_ID;
    const bt_node* node = u64_bst_find(state->lookup_tree, name);
    if (node) {
        lookup_index = node->value.u32;
    }

    if (lookup_index != INVALID_ID) {
        resource_lookup* lookup = &state->lookups[lookup_index];
        lookup->reference_count++;
        // Immediately issue the callback if setup.
        if (info->user_callback) {
            info->user_callback(lookup->r, info->listener_inst);
        }

        // Return a pointer to the resource.
        return lookup->r;
    } else {
        // Resource doesn't exist. Create a new one and its lookup.
        // Look for an empty slot.
        for (u32 i = 0; i < state->max_resource_count; ++i) {
            resource_lookup* lookup = &state->lookups[i];
            if (!lookup->r) {
                // Grab a handler for the resource type, if there is one.
                kresource_handler* h = &state->handlers[info->type];
                if (!h->allocate) {
                    KERROR("There is no handler setup for the asset type. Null/0 will be returned.");
                    return 0;
                }

                // Have the handler allocate memory for the resource.
                lookup->r = h->allocate();
                if (!lookup->r) {
                    KERROR("Resource handler failed to allocate resource. Null/0 will be returned.");
                    return 0;
                }

                // Add an entry to the bst for this node.
                bt_node_value v;
                v.u32 = i;
                bt_node* new_node = u64_bst_insert(state->lookup_tree, name, v);
                // Save as root if this is the first resource. Otherwise it'll be part of the tree automatically.
                if (!state->lookup_tree) {
                    state->lookup_tree = new_node;
                }

                // Setup the resource.
                lookup->r->name = name;
                lookup->r->type = info->type;
                lookup->r->state = KRESOURCE_STATE_UNINITIALIZED;
                lookup->r->generation = INVALID_ID;
                lookup->r->tag_count = 0;
                lookup->r->tags = 0;
                lookup->reference_count = 0;

                // Make the actual request.
                b8 result = h->request(h, lookup->r, info);
                if (result) {
                    // Increment reference count.
                    lookup->reference_count++;

                    // Return a pointer to the resource, even if it's not yet ready.
                    return lookup->r;
                }

                // This means the handler failed.
                KERROR("Resource handler failed to fulfill request. See logs for details. Null/0 will be returned.");
                return 0;
            }
        }

        KFATAL("Max configured resource count of %u has been exceeded and all slots are full. Increase this count in configuration.", state->max_resource_count);
        return 0;
    }
}

void kresource_system_release(struct kresource_system_state* state, kresource* resource) {
    KASSERT_MSG(state && resource, "kresource_system_release requires valid pointers to state and resource.");

    kresource_system_release_internal(state, resource, false);
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

    h->asset_system = state->asset_system;
    h->allocate = handler.allocate;
    h->request = handler.request;
    h->release = handler.release;

    return true;
}

static void kresource_system_release_internal(struct kresource_system_state* state, kresource* resource, b8 force_release) {
    KASSERT_MSG(state && resource, "kresource_system_release requires valid pointers to state and resource.");

    u32 lookup_index = INVALID_ID;
    const bt_node* node = u64_bst_find(state->lookup_tree, resource->name);
    if (node) {
        lookup_index = node->value.u32;
    }
    if (lookup_index != INVALID_ID) {
        // Valid entry found, decrement the reference count.
        resource_lookup* lookup = &state->lookups[lookup_index];
        b8 do_release = false;
        if (force_release) {
            lookup->reference_count = 0;
            do_release = true;
        } else {
            lookup->reference_count--;
            do_release = lookup->auto_release && lookup->reference_count < 1;
        }
        if (do_release) {
            // Auto release set and criteria met, so call resource handler's 'release' function.
            kresource_handler* handler = &state->handlers[lookup->r->type];
            if (!handler->release) {
                KTRACE("No release setup on handler for resource type %d, name='%s'", lookup->r->type, kname_string_get(lookup->r->name));
            } else {
                // Release the resource-specific data.
                handler->release(handler, lookup->r);
            }

            lookup->r = 0;

            // Ensure the lookup is invalidated.
            lookup->reference_count = 0;
            lookup->auto_release = false;
        }
    } else {
        // Entry not found, nothing to do.
        KWARN("kresource_system_release: Attempted to release resource '%s', which does not exist or is not already loaded. Nothing to do.", kname_string_get(resource->name));
    }
}
