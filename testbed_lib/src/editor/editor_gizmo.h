#pragma once

#include <defines.h>
#include <math/math_types.h>
#include <resources/resource_types.h>

struct ray;

typedef enum editor_gizmo_mode {
    EDITOR_GIZMO_MODE_NONE = 0,
    EDITOR_GIZMO_MODE_MOVE = 1,
    EDITOR_GIZMO_MODE_ROTATE = 2,
    EDITOR_GIZMO_MODE_SCALE = 3,
    EDITOR_GIZMO_MODE_MAX = EDITOR_GIZMO_MODE_SCALE,
} editor_gizmo_mode;

typedef enum editor_gizmo_interaction_type {
    EDITOR_GIZMO_INTERACTION_TYPE_NONE,
    EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER,
    EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DOWN,
    EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG,
    EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_UP,
    EDITOR_GIZMO_INTERACTION_TYPE_CANCEL
} editor_gizmo_interaction_type;

typedef struct editor_gizmo_mode_data {
    u32 vertex_count;
    colour_vertex_3d* vertices;

    u32 index_count;
    u32* indices;

    geometry geo;

    u32 extents_count;
    extents_3d* mode_extents;

    u8 current_axis_index;
    plane_3d interaction_plane;
    vec3 interaction_start_pos;
    vec3 last_interaction_pos;
} editor_gizmo_mode_data;

typedef struct editor_gizmo {
    /** @brief The transform of the gizmo. */
    transform xform;
    /** @brief The current mode of the gizmo. */
    editor_gizmo_mode mode;

    /** @brief Used to keep the gizmo a consistent size on the screen despite camera distance. */
    f32 scale_scalar;

    /** @brief The data for each mode of the gizmo. */
    editor_gizmo_mode_data mode_data[EDITOR_GIZMO_MODE_MAX + 1];

    editor_gizmo_interaction_type interaction;
} editor_gizmo;

KAPI b8 editor_gizmo_create(editor_gizmo* out_gizmo);
KAPI void editor_gizmo_destroy(editor_gizmo* gizmo);

KAPI b8 editor_gizmo_initialize(editor_gizmo* gizmo);
KAPI b8 editor_gizmo_load(editor_gizmo* gizmo);
KAPI b8 editor_gizmo_unload(editor_gizmo* gizmo);

KAPI void editor_gizmo_update(editor_gizmo* gizmo);

KAPI void editor_gizmo_mode_set(editor_gizmo* gizmo, editor_gizmo_mode mode);

KAPI void editor_gizmo_interaction_begin(editor_gizmo* gizmo, struct ray* r, editor_gizmo_interaction_type interaction_type);
KAPI void editor_gizmo_interaction_end(editor_gizmo* gizmo);
KAPI void editor_gizmo_handle_interaction(editor_gizmo* gizmo, struct ray* r, editor_gizmo_interaction_type interaction_type);
