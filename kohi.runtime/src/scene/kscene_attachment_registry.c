#include "kscene_attachment_registry.h"
#include "containers/darray.h"
#include "containers/kpool.h"
#include "containers/u64_bst.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "scene/kscene_attachment_types.h"
#include "strings/kname.h"

static kscene_attachment_handler* get_handler(struct kscene_attachment_type_registry_state* state, kname type_name);

b8 kscene_attachment_type_registry_initialize(struct kscene_attachment_type_registry_state* state) {
    if (!state) {
        return false;
    }

    state->handlers = darray_reserve(kscene_attachment_handler, 10);
    state->lookup_bst = 0;

    KDEBUG("kscene attachment type registry initialized.");

    return true;
}

void kscene_attachment_type_registry_shutdown(struct kscene_attachment_type_registry_state* state) {
    if (state) {
        if (state->handlers) {
            u32 handler_count = darray_length(state->handlers);
            for (u32 i = 0; i < handler_count; ++i) {
                kpool_destroy(&state->handlers[i].attachments);
            }
            darray_destroy(state->handlers);
        }
        if (state->lookup_bst) {
            u64_bst_cleanup(state->lookup_bst);
        }

        kzero_memory(state, sizeof(kscene_attachment_type_registry_state));
    }
}

b8 kscene_attachment_type_register_type_handler(struct kscene_attachment_type_registry_state* state, kscene_attachment_handler handler) {
    if (!state || handler.type_name == INVALID_KNAME) {
        return false;
    }

    if (!handler.pool_element_size) {
        KERROR("Attempted to register a scene attachment type handler whose pool_element_size is 0.");
        return false;
    }

    if (!handler.pool_element_max) {
        KERROR("Attempted to register a scene attachment type handler whose pool_element_max is 0.");
        return false;
    }

    kscene_attachment_handler* p_handler = get_handler(state, handler.type_name);
    if (p_handler) {
        // re-register the type, overwriting the current version.
        KDEBUG("Updating handler for scene attachment type '%s'.", kname_string_get(handler.type_name));
        *p_handler = handler;
    } else {
        KDEBUG("Registering new handler for scene attachment type '%s'.", kname_string_get(handler.type_name));

        // Setup the handler's pool.
        if (!kpool_create(handler.pool_element_size, handler.pool_element_max, &handler.attachments)) {
            KERROR("Failed to create attachment pool for handler type '%s'.", kname_string_get(handler.type_name));
            return false;
        }

        // The new entry will always be pushed into the end of the array.
        u64 index = darray_length(state->handlers);
        darray_push(state->handlers, handler);

        // Set it up in the lookup tree.
        bt_node_value value;
        value.u64 = index;
        bt_node* new_node = u64_bst_insert(state->lookup_bst, handler.type_name, value);
        if (!state->lookup_bst) {
            state->lookup_bst = new_node;
        }

        p_handler = &state->handlers[index];
    }

    if (p_handler) {
        // TODO: setup handler? Also validation?
        return true;
    }

    return false;
}

b8 kscene_attachment_create(struct kscene_attachment_type_registry_state* state, kscene_attachment_config* config, kscene_attachment* out_attachment) {
    if (!state || !out_attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, config->type_name);
    if (handler) {
        if (handler->create) {
            if (!handler->create(handler, config, &out_attachment->internal_attachment)) {
                KERROR("Scene attachment creation failed. See logs for details.");
                return false;
            }

            // Deserialize config if setup to do so.
            if (handler->deserialize(handler, out_attachment->internal_attachment, config->config)) {
                if (!config->config) {
                    KERROR("Attachment handler for type '%s' has a deserializer, but no configuration was provided.", kname_string_get(config->type_name));
                    return false;
                }
            }

            return true;
        }
        // Handler found, but no callback exists.
        KERROR("No create function setup for attachment handler type '%s'.", kname_string_get(config->type_name));
        return false;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(config->type_name));
    return false;
}

void kscene_attachment_destroy(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->destroy) {
            handler->destroy(handler, &attachment->internal_attachment);
        }
        return;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
}

b8 kscene_attachment_deserialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const char* source_string) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state, attachment, and source_string.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->deserialize) {
            return handler->deserialize(handler, attachment->internal_attachment, source_string);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

const char* kscene_attachment_serialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return 0;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->serialize) {
            return handler->serialize(handler, attachment->internal_attachment);
        }
        // Handler found, but no callback exists. Technically a success.
        return 0;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return 0;
}

b8 kscene_attachment_initialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->initialize) {
            return handler->initialize(handler, attachment->internal_attachment);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}
b8 kscene_attachment_load(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->load) {
            return handler->load(handler, attachment->internal_attachment);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

void kscene_attachment_unload(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->unload) {
            handler->unload(handler, attachment->internal_attachment);
        }
        return;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
}

b8 kscene_attachment_update(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data) {
    if (!state || !attachment || !p_frame_data) {
        KERROR("%s requires valid pointers to state, attachment, and p_frame_data.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->update) {
            return handler->update(handler, attachment->internal_attachment, p_frame_data);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

b8 kscene_attachment_render_frame_prepare(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data) {
    if (!state || !attachment || !p_frame_data) {
        KERROR("%s requires valid pointers to state, attachment, and p_frame_data.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->render_frame_prepare) {
            return handler->render_frame_prepare(handler, attachment->internal_attachment, p_frame_data);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

b8 kscene_attachment_generate_render_data(struct kscene_attachment_type_registry_state* state, kname type_name, khandle internal_attachment, mat4 node_model, const struct frame_data* p_frame_data, u32* render_data_count, struct geometry_render_data** out_render_datas) {
    if (!state || !p_frame_data || render_data_count || !out_render_datas) {
        KERROR("%s requires valid pointers to state, attachment, p_frame_data, and out_render_data.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, type_name);
    if (handler) {
        if (handler->generate_render_data) {
            return handler->generate_render_data(handler, internal_attachment, node_model, p_frame_data, render_data_count, out_render_datas);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(type_name));
    return false;
}

b8 kscene_attachment_debug_initialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->debug_initialize) {
            return handler->debug_initialize(handler, attachment->internal_attachment);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

b8 kscene_attachment_debug_load(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->debug_load) {
            return handler->debug_load(handler, attachment->internal_attachment);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

void kscene_attachment_debug_unload(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment) {
    if (!state || !attachment) {
        KERROR("%s requires valid pointers to state and attachment.", __FUNCTION__);
        return;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->debug_unload) {
            handler->debug_unload(handler, attachment->internal_attachment);
        }
        // Handler found, but no callback exists. Technically a success.
        return;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
}

b8 kscene_attachment_debug_update(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data) {
    if (!state || !attachment || !p_frame_data) {
        KERROR("%s requires valid pointers to state, attachment, and p_frame_data.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->debug_update) {
            return handler->debug_update(handler, attachment->internal_attachment, p_frame_data);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

b8 kscene_attachment_debug_render_frame_prepare(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data) {
    if (!state || !attachment || !p_frame_data) {
        KERROR("%s requires valid pointers to state, attachment, and p_frame_data.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->debug_render_frame_prepare) {
            return handler->debug_render_frame_prepare(handler, attachment->internal_attachment, p_frame_data);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

b8 kscene_attachment_debug_generate_render_data(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data, struct geometry_render_data* out_render_data) {
    if (!state || !attachment || !p_frame_data || !out_render_data) {
        KERROR("%s requires valid pointers to state, attachment, p_frame_data, and out_render_data.", __FUNCTION__);
        return false;
    }

    kscene_attachment_handler* handler = get_handler(state, attachment->type_name);
    if (handler) {
        if (handler->debug_generate_render_data) {
            return handler->debug_generate_render_data(handler, attachment->internal_attachment, p_frame_data, out_render_data);
        }
        // Handler found, but no callback exists. Technically a success.
        return true;
    }

    KERROR("%s - no handler exists for type '%s'.", __FUNCTION__, kname_string_get(attachment->type_name));
    return false;
}

static kscene_attachment_handler* get_handler(struct kscene_attachment_type_registry_state* state, kname type_name) {
    const bt_node* node = u64_bst_find(state->lookup_bst, type_name);
    if (node) {
        return &state->handlers[node->value.u64];
    }

    return 0;
}
