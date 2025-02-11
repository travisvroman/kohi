#pragma once

#include <identifiers/khandle.h>
#include <strings/kname.h>

#define KSCENE_ATTACHMENT_TYPE_NAME_STATIC_MESH "static_mesh"

struct frame_data;
struct geometry_render_data;
struct bt_node;

typedef enum kscene_attachment_state {
    KSCENE_ATTACHMENT_STATE_UNINITIALIZED,
    KSCENE_ATTACHMENT_STATE_INITIALIZED,
    KSCENE_ATTACHMENT_STATE_LOADING,
    KSCENE_ATTACHMENT_STATE_LOADED,
    KSCENE_ATTACHMENT_STATE_UNLOADING,
    KSCENE_ATTACHMENT_STATE_UNLOADED
} kscene_attachment_state;
