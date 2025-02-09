#pragma once

#include <containers/kpool.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <math/math_types.h>

#include "scene/kscene_attachment_types.h"

/**
 * Lifecycle of an attachment (assuming it uses all "stages")
 *  - create
 *  - deserialize (typically from configuration, called automatically by attachment registry, but can be called manually as well)
 *  - initialize
 *  - load
 *  - serialize (can be done any time after attachment is loaded)
 *  - unload
 *  - destroy
 */

/**
 * @brief Represents a handler for a given attachment type.
 */
typedef struct kscene_attachment_handler {
    kname type_name;

    // Handler internal state.
    void* internal_state;

    // The max number of attachments handled by this handler.
    u32 pool_element_max;

    // The size of each element in the pool.
    u32 pool_element_size;

    // An pool of attachments owned by this handler.
    kpool attachments;

    b8 (*create)(struct kscene_attachment_handler* handler, const kscene_attachment_config* config, khandle* out_attachment);
    void (*destroy)(struct kscene_attachment_handler* handler, khandle* attachment);

    b8 (*deserialize)(struct kscene_attachment_handler* handler, khandle attachment, const char* source_string);
    const char* (*serialize)(struct kscene_attachment_handler* handler, khandle attachment);

    b8 (*initialize)(struct kscene_attachment_handler* handler, khandle attachment);
    b8 (*load)(struct kscene_attachment_handler* handler, khandle attachment);
    void (*unload)(struct kscene_attachment_handler* handler, khandle attachment);
    b8 (*update)(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data);

    b8 (*render_frame_prepare)(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data);
    b8 (*generate_render_data)(struct kscene_attachment_handler* handler, khandle attachment, mat4 node_model, const struct frame_data* p_frame_data, u32* render_data_count, struct geometry_render_data** out_render_datas);

    b8 (*debug_initialize)(struct kscene_attachment_handler* handler, khandle attachment);
    b8 (*debug_load)(struct kscene_attachment_handler* handler, khandle attachment);
    void (*debug_unload)(struct kscene_attachment_handler* handler, khandle attachment);
    b8 (*debug_update)(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data);

    b8 (*debug_render_frame_prepare)(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data);
    b8 (*debug_generate_render_data)(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data, struct geometry_render_data* out_render_data);

} kscene_attachment_handler;

typedef struct kscene_attachment_type_registry_state {
    // BST for quick lookups of type handlers by type name.
    struct bt_node* lookup_bst;

    // darray of attachment type handlers.
    kscene_attachment_handler* handlers;
} kscene_attachment_type_registry_state;

KAPI b8 kscene_attachment_type_registry_initialize(struct kscene_attachment_type_registry_state* state);
KAPI void kscene_attachment_type_registry_shutdown(struct kscene_attachment_type_registry_state* state);

KAPI b8 kscene_attachment_type_register_type_handler(struct kscene_attachment_type_registry_state* state, kscene_attachment_handler handler);

// Creates the attachment. Also automatically calls deserialize if setup.
KAPI b8 kscene_attachment_create(struct kscene_attachment_type_registry_state* state, kscene_attachment_config* config, kscene_attachment* out_attachment);
KAPI void kscene_attachment_destroy(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);

KAPI b8 kscene_attachment_deserialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const char* source_string);
KAPI const char* kscene_attachment_serialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);

KAPI b8 kscene_attachment_initialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);
KAPI b8 kscene_attachment_load(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);
KAPI void kscene_attachment_unload(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);
KAPI b8 kscene_attachment_update(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data);

KAPI b8 kscene_attachment_render_frame_prepare(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data);
KAPI b8 kscene_attachment_generate_render_data(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, mat4 node_model, const struct frame_data* p_frame_data, struct geometry_render_data* out_render_data);

KAPI b8 kscene_attachment_debug_initialize(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);
KAPI b8 kscene_attachment_debug_load(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);
KAPI void kscene_attachment_debug_unload(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment);
KAPI b8 kscene_attachment_debug_update(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data);

KAPI b8 kscene_attachment_debug_render_frame_prepare(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data);
KAPI b8 kscene_attachment_debug_generate_render_data(struct kscene_attachment_type_registry_state* state, kscene_attachment* attachment, const struct frame_data* p_frame_data, struct geometry_render_data* out_render_data);
