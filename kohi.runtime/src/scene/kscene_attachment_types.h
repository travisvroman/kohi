#pragma once

#include <identifiers/khandle.h>
#include <strings/kname.h>

#define KSCENE_ATTACHMENT_TYPE_NAME_STATIC_MESH "static_mesh"

struct frame_data;
struct geometry_render_data;
struct bt_node;

/**
 * @brief Represents the base configuration structure for a kscene attachment.
 */
typedef struct kscene_attachment_config {
    // The name of the attachment type. (i.e. kname_create("static_mesh"))
    kname type_name;
    // Name of the attachment
    kname name;
    // String representation of the config for the underlying type.
    const char* config;
} kscene_attachment_config;

typedef enum kscene_attachment_state {
    KSCENE_ATTACHMENT_STATE_UNINITIALIZED,
    KSCENE_ATTACHMENT_STATE_INITIALIZED,
    KSCENE_ATTACHMENT_STATE_LOADING,
    KSCENE_ATTACHMENT_STATE_LOADED,
    KSCENE_ATTACHMENT_STATE_UNLOADING,
    KSCENE_ATTACHMENT_STATE_UNLOADED
} kscene_attachment_state;

typedef struct kscene_attachment {
    // The name of the attachment type. (i.e. kname_create("static_mesh"))
    kname type_name;
    // Name of the attachment
    kname name;
    // Handle to the internal attachment type-specific data.
    khandle internal_attachment;

} kscene_attachment;
