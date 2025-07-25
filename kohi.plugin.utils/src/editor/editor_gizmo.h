#pragma once

#include <defines.h>
#include <math/geometry.h>
#include <math/math_types.h>
#include <resources/resource_types.h>
#include <systems/kcamera_system.h>

#include "identifiers/khandle.h"

#if KOHI_DEBUG
#    include <resources/debug/debug_line3d.h>
#endif

struct ray;
struct frame_data;

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

    kgeometry geo;

    u32 extents_count;
    extents_3d* mode_extents;

    u8 current_axis_index;
    plane_3d interaction_plane;
    plane_3d interaction_plane_back;

    vec3 interaction_start_pos;
    vec3 last_interaction_pos;
} editor_gizmo_mode_data;

typedef enum editor_gizmo_orientation {
    /** @brief The gizmo's transform operations are relative to global transform. */
    EDITOR_GIZMO_ORIENTATION_GLOBAL = 0,
    /** @brief The gizmo's transform operations are relative to local transform. */
    EDITOR_GIZMO_ORIENTATION_LOCAL = 1,
    /** @brief The gizmo's transform operations are relative to the current view. */
    // EDITOR_GIZMO_ORIENTATION_VIEW = 2,
    EDITOR_GIZMO_ORIENTATION_MAX = EDITOR_GIZMO_ORIENTATION_LOCAL
} editor_gizmo_orientation;

typedef struct editor_gizmo {
    /** @brief The transform of the gizmo. */
    khandle xform_handle;
    /** @brief A handle to the currently selected object's transform. Invalid handle if nothing is selected. */
    khandle selected_xform_handle;
    /** @brief A handle to the parent of the currently selected object's transform, if one exists. Otherwise invalid handle. */
    khandle selected_xform_parent_handle;
    /** @brief The current mode of the gizmo. */
    editor_gizmo_mode mode;

    /** @brief Used to keep the gizmo a consistent size on the screen despite camera distance. */
    f32 scale_scalar;

    /** @brief Indicates the editor transform operaton orientation. */
    editor_gizmo_orientation orientation;

    /** @brief The data for each mode of the gizmo. */
    editor_gizmo_mode_data mode_data[EDITOR_GIZMO_MODE_MAX + 1];

    editor_gizmo_interaction_type interaction;

    b8 is_dirty;

#if KOHI_DEBUG
    debug_line3d plane_normal_line;
#endif
} editor_gizmo;

KAPI b8 editor_gizmo_create(editor_gizmo* out_gizmo);
KAPI void editor_gizmo_destroy(editor_gizmo* gizmo);

KAPI b8 editor_gizmo_initialize(editor_gizmo* gizmo);
KAPI b8 editor_gizmo_load(editor_gizmo* gizmo);
KAPI b8 editor_gizmo_unload(editor_gizmo* gizmo);

KAPI void editor_gizmo_refresh(editor_gizmo* gizmo);
KAPI editor_gizmo_orientation editor_gizmo_orientation_get(editor_gizmo* gizmo);
KAPI void editor_gizmo_orientation_set(editor_gizmo* gizmo, editor_gizmo_orientation orientation);
KAPI void editor_gizmo_selected_transform_set(editor_gizmo* gizmo, khandle xform_handle, khandle parent_xform_handle);

KAPI void editor_gizmo_update(editor_gizmo* gizmo);
KAPI void editor_gizmo_render_frame_prepare(editor_gizmo* gizmo, const struct frame_data* p_frame_data);

KAPI void editor_gizmo_mode_set(editor_gizmo* gizmo, editor_gizmo_mode mode);

KAPI void editor_gizmo_interaction_begin(editor_gizmo* gizmo, kcamera camera, struct ray* r, editor_gizmo_interaction_type interaction_type);
KAPI void editor_gizmo_interaction_end(editor_gizmo* gizmo);
KAPI void editor_gizmo_handle_interaction(editor_gizmo* gizmo, kcamera camera, struct ray* r, editor_gizmo_interaction_type interaction_type);

KAPI mat4 editor_gizmo_model_get(editor_gizmo* gizmo);
