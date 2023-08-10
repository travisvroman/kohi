
// TODO: Test with a raycast against the gizmo first. Gizmo should have 3 obbs (one per axis) for scale and move, then 3 more OBBs for 2-axis combos (x/y, x/z, y/z),
// and finally one more for all 3 axes in the center. Rotation should use either 3 OBBs (one per axis surrounding each ring) or better, cylinders. Not sure yet how combos
// should work for this.
// If a hit is found (specifically on one of the OBBs), flip to a "manipulating gizmo" state unless the state is "none".
// Next, determine the gizmo mode and act accordingly:
// - For move, detect which axis has been hit (x, y, z or a combo or 2/3), and create an imaginary plane on the axe(s). Its normal should
//   be that axis, making the plane orthogonal to it. (so for x, dragging on the y/z plane would move it along x, etc.). Its position is at the gizmo's origin.
//   As the user drags, cast a new ray against that plane, then get the distance from the original position to the hit, and adjust position accordingly.
// - Combinations of 2 axes should create a normal between the 2 axes, so its plane is orthogonal to that center normal.
// - If all 3 axes are selected for move, create a plane whose normal faces the camera, and translate along that plane.
// - Scale should act the same way, but scale instead of move the object.
// - For rotate, detect axis, then detect diff in mouse position and apply rotation of that diff, along said axis.
// - The gizmo should only be active/visible on a selected object. Object selection is done by the closest intersection on button release.
// - The gizmo should be able to get a pointer to a transform by id. This transform is what is edited.
// - Before editing begins, a copy of the transform should be taken beforehand to allow canceling of the operation.
// - Canceling can be done by pressing the right mouse button while manipulating or by presseing esc.
// - Undo will be handled later by an undo stack.

#include "editor_gizmo.h"

#include <core/logger.h>
#include <defines.h>
#include <math/geometry_3d.h>
#include <math/kmath.h>
#include <math/transform.h>
#include <renderer/camera.h>
#include <renderer/renderer_frontend.h>

#include "core/kmemory.h"
#include "math/math_types.h"
#include "renderer/camera.h"

static void create_gizmo_mode_none(editor_gizmo* gizmo);
static void create_gizmo_mode_move(editor_gizmo* gizmo);
static void create_gizmo_mode_scale(editor_gizmo* gizmo);
static void create_gizmo_mode_rotate(editor_gizmo* gizmo);

const static u8 segments = 32;
const static f32 radius = 1.0f;

b8 editor_gizmo_create(editor_gizmo* out_gizmo) {
    if (!out_gizmo) {
        KERROR("Unable to create gizmo with an invalid out pointer.");
        return false;
    }

    out_gizmo->mode = EDITOR_GIZMO_MODE_NONE;
    out_gizmo->xform = transform_create();
    out_gizmo->selected_xform = 0;
    // Default orientation.
    out_gizmo->orientation = EDITOR_GIZMO_ORIENTATION_LOCAL;
    // out_gizmo->orientation = EDITOR_GIZMO_ORIENTATION_GLOBAL;

    // Initialize default values for all modes.
    for (u32 i = 0; i < EDITOR_GIZMO_MODE_MAX + 1; ++i) {
        out_gizmo->mode_data[i].vertex_count = 0;
        out_gizmo->mode_data[i].vertices = 0;

        out_gizmo->mode_data[i].index_count = 0;
        out_gizmo->mode_data[i].indices = 0;
    }

    return true;
}

void editor_gizmo_destroy(editor_gizmo* gizmo) {
    if (gizmo) {
    }
}

b8 editor_gizmo_initialize(editor_gizmo* gizmo) {
    if (!gizmo) {
        return false;
    }

    gizmo->mode = EDITOR_GIZMO_MODE_NONE;

    create_gizmo_mode_none(gizmo);
    create_gizmo_mode_move(gizmo);
    create_gizmo_mode_scale(gizmo);
    create_gizmo_mode_rotate(gizmo);

    return true;
}

b8 editor_gizmo_load(editor_gizmo* gizmo) {
    if (!gizmo) {
        return false;
    }

    for (u32 i = 0; i < EDITOR_GIZMO_MODE_MAX + 1; ++i) {
        if (!renderer_geometry_create(&gizmo->mode_data[i].geo, sizeof(colour_vertex_3d), gizmo->mode_data[i].vertex_count, gizmo->mode_data[i].vertices, 0, 0, 0)) {
            KERROR("Failed to create gizmo geometry type: '%u'", i);
            return false;
        }
        if (!renderer_geometry_upload(&gizmo->mode_data[i].geo)) {
            KERROR("Failed to upload gizmo geometry type: '%u'", i);
            return false;
        }
        if (gizmo->mode_data[i].geo.generation == INVALID_ID_U16) {
            gizmo->mode_data[i].geo.generation = 0;
        } else {
            gizmo->mode_data[i].geo.generation++;
        }
    }

#ifdef _DEBUG
    debug_line3d_create(vec3_zero(), vec3_one(), 0, &gizmo->plane_normal_line);
    debug_line3d_initialize(&gizmo->plane_normal_line);
    debug_line3d_load(&gizmo->plane_normal_line);
    // magenta
    debug_line3d_colour_set(&gizmo->plane_normal_line, (vec4){1.0f, 0, 1.0f, 1.0f});
#endif
    return true;
}

b8 editor_gizmo_unload(editor_gizmo* gizmo) {
    if (gizmo) {
#ifdef _DEBUG
        debug_line3d_unload(&gizmo->plane_normal_line);
        debug_line3d_destroy(&gizmo->plane_normal_line);
#endif
    }
    return true;
}

void editor_gizmo_refresh(editor_gizmo* gizmo) {
    if (gizmo) {
        if (gizmo->selected_xform) {
            // Set the position.
            transform_position_set(&gizmo->xform, transform_position_get(gizmo->selected_xform));
            // If local, set rotation.
            if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL) {
                transform_rotation_set(&gizmo->xform, transform_rotation_get(gizmo->selected_xform));
            } else {
                transform_rotation_set(&gizmo->xform, quat_identity());
            }
        } else {
            // For now, reset.
            transform_position_set(&gizmo->xform, vec3_zero());
            transform_rotation_set(&gizmo->xform, quat_identity());
        }
    }
}

editor_gizmo_orientation editor_gizmo_orientation_get(editor_gizmo* gizmo) {
    if (gizmo) {
        return gizmo->orientation;
    }

    KWARN("editor_gizmo_orientation_get was given no gizmo, returning default of global.");
    return EDITOR_GIZMO_ORIENTATION_GLOBAL;
}

void editor_gizmo_orientation_set(editor_gizmo* gizmo, editor_gizmo_orientation orientation) {
    if (gizmo) {
        gizmo->orientation = orientation;
#if _DEBUG
        switch (gizmo->orientation) {
            case EDITOR_GIZMO_ORIENTATION_GLOBAL:
                KTRACE("Setting editor gizmo to GLOBAL.");
                break;
            case EDITOR_GIZMO_ORIENTATION_LOCAL:
                KTRACE("Setting editor gizmo to LOCAL.");
                break;
        }
#endif
        editor_gizmo_refresh(gizmo);
    }
}
void editor_gizmo_selected_transform_set(editor_gizmo* gizmo, transform* xform) {
    if (gizmo) {
        gizmo->selected_xform = xform;
        editor_gizmo_refresh(gizmo);
    }
}

void editor_gizmo_update(editor_gizmo* gizmo) {
    if (gizmo) {
    }
}

void editor_gizmo_mode_set(editor_gizmo* gizmo, editor_gizmo_mode mode) {
    if (gizmo) {
        gizmo->mode = mode;
    }
}

static void create_gizmo_mode_none(editor_gizmo* gizmo) {
    editor_gizmo_mode_data* data = &gizmo->mode_data[EDITOR_GIZMO_MODE_NONE];

    data->vertex_count = 6;  // 2 per line, 3 lines
    data->vertices = kallocate(sizeof(colour_vertex_3d) * data->vertex_count, MEMORY_TAG_ARRAY);
    vec4 grey = (vec4){0.5f, 0.5f, 0.5f, 1.0f};

    // x
    data->vertices[0].colour = grey;  // First vert is at origin, no pos needed.
    data->vertices[1].colour = grey;
    data->vertices[1].position.x = 1.0f;

    // y
    data->vertices[2].colour = grey;  // First vert is at origin, no pos needed.
    data->vertices[3].colour = grey;
    data->vertices[3].position.y = 1.0f;

    // z
    data->vertices[4].colour = grey;  // First vert is at origin, no pos needed.
    data->vertices[5].colour = grey;
    data->vertices[5].position.z = 1.0f;
}

static void create_gizmo_mode_move(editor_gizmo* gizmo) {
    editor_gizmo_mode_data* data = &gizmo->mode_data[EDITOR_GIZMO_MODE_MOVE];

    data->current_axis_index = INVALID_ID_U8;
    data->vertex_count = 18;  // 2 per line, 3 lines + 6 lines
    data->vertices = kallocate(sizeof(colour_vertex_3d) * data->vertex_count, MEMORY_TAG_ARRAY);

    vec4 r = (vec4){1.0f, 0.0f, 0.0f, 1.0f};
    vec4 g = (vec4){0.0f, 1.0f, 0.0f, 1.0f};
    vec4 b = (vec4){0.0f, 0.0f, 1.0f, 1.0f};
    // x
    data->vertices[0].colour = r;
    data->vertices[0].position.x = 0.2f;
    data->vertices[1].colour = r;
    data->vertices[1].position.x = 2.0f;

    // y
    data->vertices[2].colour = g;
    data->vertices[2].position.y = 0.2f;
    data->vertices[3].colour = g;
    data->vertices[3].position.y = 2.0f;

    // z
    data->vertices[4].colour = b;
    data->vertices[4].position.z = 0.2f;
    data->vertices[5].colour = b;
    data->vertices[5].position.z = 2.0f;

    // x "box" lines
    data->vertices[6].colour = r;
    data->vertices[6].position.x = 0.4f;
    data->vertices[7].colour = r;
    data->vertices[7].position.x = 0.4f;
    data->vertices[7].position.y = 0.4f;

    data->vertices[8].colour = r;
    data->vertices[8].position.x = 0.4f;
    data->vertices[9].colour = r;
    data->vertices[9].position.x = 0.4f;
    data->vertices[9].position.z = 0.4f;

    // y "box" lines
    data->vertices[10].colour = g;
    data->vertices[10].position.y = 0.4f;
    data->vertices[11].colour = g;
    data->vertices[11].position.y = 0.4f;
    data->vertices[11].position.z = 0.4f;

    data->vertices[12].colour = g;
    data->vertices[12].position.y = 0.4f;
    data->vertices[13].colour = g;
    data->vertices[13].position.y = 0.4f;
    data->vertices[13].position.x = 0.4f;

    // z "box" lines
    data->vertices[14].colour = b;
    data->vertices[14].position.z = 0.4f;
    data->vertices[15].colour = b;
    data->vertices[15].position.z = 0.4f;
    data->vertices[15].position.y = 0.4f;

    data->vertices[16].colour = b;
    data->vertices[16].position.z = 0.4f;
    data->vertices[17].colour = b;
    data->vertices[17].position.z = 0.4f;
    data->vertices[17].position.x = 0.4f;

    data->extents_count = 7;
    data->mode_extents = kallocate(sizeof(extents_3d) * data->extents_count, MEMORY_TAG_ARRAY);

    // Create boxes for each axis
    // x
    extents_3d* ex = &data->mode_extents[0];
    ex->min = vec3_create(0.4f, -0.2f, -0.2f);
    ex->max = vec3_create(2.1f, 0.2f, 0.2f);

    // y
    ex = &data->mode_extents[1];
    ex->min = vec3_create(-0.2f, 0.4f, -0.2f);
    ex->max = vec3_create(0.2f, 2.1f, 0.2f);

    // z
    ex = &data->mode_extents[2];
    ex->min = vec3_create(-0.2f, -0.2f, 0.4f);
    ex->max = vec3_create(0.2f, 0.2f, 2.1f);

    // Boxes for combo axes.
    // x-y
    ex = &data->mode_extents[3];
    ex->min = vec3_create(0.1f, 0.1f, -0.05f);
    ex->max = vec3_create(0.5f, 0.5f, 0.05f);

    // x-z
    ex = &data->mode_extents[4];
    ex->min = vec3_create(0.1f, -0.05f, 0.1f);
    ex->max = vec3_create(0.5f, 0.05f, 0.5f);

    // y-z
    ex = &data->mode_extents[5];
    ex->min = vec3_create(-0.05f, 0.1f, 0.1f);
    ex->max = vec3_create(0.05f, 0.5f, 0.5f);

    // xyz
    ex = &data->mode_extents[6];
    ex->min = vec3_create(-0.1f, -0.1f, -0.1f);
    ex->max = vec3_create(0.1f, 0.1f, 0.1f);
}

static void create_gizmo_mode_scale(editor_gizmo* gizmo) {
    editor_gizmo_mode_data* data = &gizmo->mode_data[EDITOR_GIZMO_MODE_SCALE];

    data->current_axis_index = INVALID_ID_U8;
    data->vertex_count = 12;  // 2 per line, 3 lines + 3 lines
    data->vertices = kallocate(sizeof(colour_vertex_3d) * data->vertex_count, MEMORY_TAG_ARRAY);

    vec4 r = (vec4){1.0f, 0.0f, 0.0f, 1.0f};
    vec4 g = (vec4){0.0f, 1.0f, 0.0f, 1.0f};
    vec4 b = (vec4){0.0f, 0.0f, 1.0f, 1.0f};

    // x
    data->vertices[0].colour = r;  // First vert is at origin, no pos needed.
    data->vertices[1].colour = r;
    data->vertices[1].position.x = 2.0f;

    // y
    data->vertices[2].colour = g;  // First vert is at origin, no pos needed.
    data->vertices[3].colour = g;
    data->vertices[3].position.y = 2.0f;

    // z
    data->vertices[4].colour = b;  // First vert is at origin, no pos needed.
    data->vertices[5].colour = b;
    data->vertices[5].position.z = 2.0f;

    // x/y outer line
    data->vertices[6].position.x = 0.8f;
    data->vertices[6].colour = r;
    data->vertices[7].position.y = 0.8f;
    data->vertices[7].colour = g;

    // z/y outer line
    data->vertices[8].position.z = 0.8f;
    data->vertices[8].colour = b;
    data->vertices[9].position.y = 0.8f;
    data->vertices[9].colour = g;

    // x/z outer line
    data->vertices[10].position.x = 0.8f;
    data->vertices[10].colour = r;
    data->vertices[11].position.z = 0.8f;
    data->vertices[11].colour = b;

    data->extents_count = 7;
    data->mode_extents = kallocate(sizeof(extents_3d) * data->extents_count, MEMORY_TAG_ARRAY);

    // Create boxes for each axis
    // x
    extents_3d* ex = &data->mode_extents[0];
    ex->min = vec3_create(0.4f, -0.2f, -0.2f);
    ex->max = vec3_create(2.1f, 0.2f, 0.2f);

    // y
    ex = &data->mode_extents[1];
    ex->min = vec3_create(-0.2f, 0.4f, -0.2f);
    ex->max = vec3_create(0.2f, 2.1f, 0.2f);

    // z
    ex = &data->mode_extents[2];
    ex->min = vec3_create(-0.2f, -0.2f, 0.4f);
    ex->max = vec3_create(0.2f, 0.2f, 2.1f);

    // Boxes for combo axes.
    // x-y
    ex = &data->mode_extents[3];
    ex->min = vec3_create(0.1f, 0.1f, -0.05f);
    ex->max = vec3_create(0.5f, 0.5f, 0.05f);

    // x-z
    ex = &data->mode_extents[4];
    ex->min = vec3_create(0.1f, -0.05f, 0.1f);
    ex->max = vec3_create(0.5f, 0.05f, 0.5f);

    // y-z
    ex = &data->mode_extents[5];
    ex->min = vec3_create(-0.05f, 0.1f, 0.1f);
    ex->max = vec3_create(0.05f, 0.5f, 0.5f);

    // xyz
    ex = &data->mode_extents[6];
    ex->min = vec3_create(-0.1f, -0.1f, -0.1f);
    ex->max = vec3_create(0.1f, 0.1f, 0.1f);
}

static void create_gizmo_mode_rotate(editor_gizmo* gizmo) {
    editor_gizmo_mode_data* data = &gizmo->mode_data[EDITOR_GIZMO_MODE_ROTATE];

    data->vertex_count = 12 + (segments * 2 * 3);  // 2 per line, 3 lines + 3 lines
    data->vertices = kallocate(sizeof(colour_vertex_3d) * data->vertex_count, MEMORY_TAG_ARRAY);

    vec4 r = (vec4){1.0f, 0.0f, 0.0f, 1.0f};
    vec4 g = (vec4){0.0f, 1.0f, 0.0f, 1.0f};
    vec4 b = (vec4){0.0f, 0.0f, 1.0f, 1.0f};

    // Start with the center, draw small axes.
    // x
    data->vertices[0].colour = r;  // First vert is at origin, no pos needed.
    data->vertices[1].colour = r;
    data->vertices[1].position.x = 0.2f;

    // y
    data->vertices[2].colour = g;  // First vert is at origin, no pos needed.
    data->vertices[3].colour = g;
    data->vertices[3].position.y = 0.2f;

    // z
    data->vertices[4].colour = b;  // First vert is at origin, no pos needed.
    data->vertices[5].colour = b;
    data->vertices[5].position.z = 0.2f;

    // For each axis, generate points in a circle.
    u32 j = 6;

    // x
    for (u32 i = 0; i < segments; ++i, j += 2) {
        // 2 at a time to form a line.
        f32 theta = (f32)i / segments * K_2PI;
        data->vertices[j].position.y = radius * kcos(theta);
        data->vertices[j].position.z = radius * ksin(theta);
        data->vertices[j].colour = r;

        theta = (f32)((i + 1) % segments) / segments * K_2PI;
        data->vertices[j + 1].position.y = radius * kcos(theta);
        data->vertices[j + 1].position.z = radius * ksin(theta);
        data->vertices[j + 1].colour = r;
    }

    // y
    for (u32 i = 0; i < segments; ++i, j += 2) {
        // 2 at a time to form a line.
        f32 theta = (f32)i / segments * K_2PI;
        data->vertices[j].position.x = radius * kcos(theta);
        data->vertices[j].position.z = radius * ksin(theta);
        data->vertices[j].colour = g;

        theta = (f32)((i + 1) % segments) / segments * K_2PI;
        data->vertices[j + 1].position.x = radius * kcos(theta);
        data->vertices[j + 1].position.z = radius * ksin(theta);
        data->vertices[j + 1].colour = g;
    }

    // z
    for (u32 i = 0; i < segments; ++i, j += 2) {
        // 2 at a time to form a line.
        f32 theta = (f32)i / segments * K_2PI;
        data->vertices[j].position.x = radius * kcos(theta);
        data->vertices[j].position.y = radius * ksin(theta);
        data->vertices[j].colour = b;

        theta = (f32)((i + 1) % segments) / segments * K_2PI;
        data->vertices[j + 1].position.x = radius * kcos(theta);
        data->vertices[j + 1].position.y = radius * ksin(theta);
        data->vertices[j + 1].colour = b;
    }

    // NOTE: Rotation gizmo uses discs, not extents, so this mode doesn't need them.
}

void editor_gizmo_interaction_begin(editor_gizmo* gizmo, camera* c, struct ray* r, editor_gizmo_interaction_type interaction_type) {
    if (!gizmo || !r) {
        return;
    }

    gizmo->interaction = interaction_type;

    if (gizmo->interaction == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
        editor_gizmo_mode_data* data = &gizmo->mode_data[gizmo->mode];

        mat4 gizmo_world = transform_world_get(&gizmo->xform);
        vec3 origin = transform_position_get(&gizmo->xform);
        vec3 plane_dir;
        if (gizmo->mode == EDITOR_GIZMO_MODE_MOVE || gizmo->mode == EDITOR_GIZMO_MODE_SCALE) {
            // Create the plane.
            if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL || gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
                switch (data->current_axis_index) {
                    case 0:  // x axis
                    case 3:  // xy axes
                        plane_dir = vec3_transform(vec3_back(), 0.0f, gizmo_world);
                        break;
                    case 1:  // y axis
                    case 6:  // xyz
                        plane_dir = camera_backward(c);
                        break;
                    case 4:  // xz axes
                        plane_dir = vec3_transform(vec3_up(), 0.0f, gizmo_world);
                        break;
                    case 2:  // z axis
                    case 5:  // yz axes
                        plane_dir = vec3_transform(vec3_right(), 0.0f, gizmo_world);
                        break;
                    default:
                        return;
                }
            } else {
                // TODO: Other orientations.
                return;
            }
            data->interaction_plane = plane_3d_create(origin, plane_dir);
            data->interaction_plane_back = plane_3d_create(origin, vec3_mul_scalar(plane_dir, -1.0f));

#ifdef _DEBUG
            debug_line3d_points_set(&gizmo->plane_normal_line, origin, vec3_add(origin, plane_dir));
#endif

            // Get the initial intersection point of the ray on the plane.
            vec3 intersection = {0};
            f32 distance;
            if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
                // Try from the other direction.
                if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
                    return;
                }
            }
            data->interaction_start_pos = intersection;
            data->last_interaction_pos = intersection;
        } else if (gizmo->mode == EDITOR_GIZMO_MODE_ROTATE) {
            // NOTE: No interaction needed because no current axis.
            if (data->current_axis_index == INVALID_ID_U8) {
                return;
            }
            KINFO("starting rotate interaction");
            // Create the plane.
            mat4 gizmo_world = transform_world_get(&gizmo->xform);
            vec3 origin = transform_position_get(&gizmo->xform);
            vec3 plane_dir;
            switch (data->current_axis_index) {
                case 0:  // x
                    plane_dir = vec3_transform(vec3_left(), 0.0f, gizmo_world);
                    break;
                case 1:  // y
                    plane_dir = vec3_transform(vec3_down(), 0.0f, gizmo_world);
                    break;
                case 2:  // z
                    plane_dir = vec3_transform(vec3_forward(), 0.0f, gizmo_world);
                    break;
            }

            data->interaction_plane = plane_3d_create(origin, plane_dir);
            data->interaction_plane_back = plane_3d_create(origin, vec3_mul_scalar(plane_dir, -1.0f));

#ifdef _DEBUG
            debug_line3d_points_set(&gizmo->plane_normal_line, origin, vec3_add(origin, plane_dir));
#endif

            // Get the initial intersection point of the ray on the plane.
            vec3 intersection = {0};
            f32 distance;
            if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
                // Try from the other direction.
                if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
                    return;
                }
            }
            data->interaction_start_pos = intersection;
            data->last_interaction_pos = intersection;
        }
    }
}

void editor_gizmo_interaction_end(editor_gizmo* gizmo) {
    if (!gizmo) {
        return;
    }

    if (gizmo->interaction == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
        if (gizmo->mode == EDITOR_GIZMO_MODE_ROTATE) {
            KINFO("Ending rotate interaction.");
            if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
                // Reset rotation. Will be applied to selection already.
                transform_rotation_set(&gizmo->xform, quat_identity());
            }
        }
    }

    gizmo->interaction = EDITOR_GIZMO_INTERACTION_TYPE_NONE;
}

void editor_gizmo_handle_interaction(editor_gizmo* gizmo, struct camera* c, struct ray* r, editor_gizmo_interaction_type interaction_type) {
    if (!gizmo || !r) {
        return;
    }

    editor_gizmo_mode_data* data = &gizmo->mode_data[gizmo->mode];
    if (gizmo->mode == EDITOR_GIZMO_MODE_MOVE) {
        if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
            // NOTE: Don't handle interaction if there's no current axis.
            if (data->current_axis_index == INVALID_ID_U8) {
                return;
            }
            mat4 gizmo_world = transform_world_get(&gizmo->xform);

            vec3 intersection = {0};
            f32 distance;
            if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
                // Try from the other direction.
                if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
                    return;
                }
            }
            vec3 diff = vec3_sub(intersection, data->last_interaction_pos);
            vec3 direction;
            vec3 translation;

            if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL ||
                gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
                // move along the current axis' line
                switch (data->current_axis_index) {
                    case 0:  // x
                        direction = vec3_transform(vec3_right(), 0.0f, gizmo_world);
                        // Project diff onto direction.
                        translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
                        break;
                    case 1:  // y
                        direction = vec3_transform(vec3_up(), 0.0f, gizmo_world);
                        // Project diff onto direction.
                        translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
                        break;
                    case 2:  // z
                        direction = vec3_transform(vec3_forward(), 0.0f, gizmo_world);
                        // Project diff onto direction.
                        translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
                        break;
                    case 3:  // xy
                    case 4:  // xz
                    case 5:  // yz
                    case 6:  // xyz
                        translation = diff;
                        break;
                    default:
                        return;
                }
            } else {
                // TODO: Other orientations.
                return;
            }
            transform_translate(&gizmo->xform, translation);
            data->last_interaction_pos = intersection;

            // Apply translation to selection.
            if (gizmo->selected_xform) {
                transform_translate(gizmo->selected_xform, translation);
            }
        } else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
            f32 dist;
            gizmo->xform.is_dirty = true;
            u8 hit_axis = INVALID_ID_U8;

            // Loop through each axis/axis combo. Loop backwards to give priority to combos since
            // those hit boxes are much smaller.
            mat4 gizmo_world = transform_world_get(&gizmo->xform);
            for (i32 i = 6; i > -1; --i) {
                if (raycast_oriented_extents(data->mode_extents[i], gizmo_world, r, &dist)) {
                    hit_axis = i;
                    break;
                }
            }

            // Handle highlighting.
            vec4 y = vec4_create(1.0f, 1.0f, 0.0f, 1.0f);
            vec4 r = vec4_create(1.0f, 0.0f, 0.0f, 1.0f);
            vec4 g = vec4_create(0.0f, 1.0f, 0.0f, 1.0f);
            vec4 b = vec4_create(0.0f, 0.0f, 1.0f, 1.0f);

            if (data->current_axis_index != hit_axis) {
                data->current_axis_index = hit_axis;

                // Main axis colours
                for (u32 i = 0; i < 3; ++i) {
                    if (i == hit_axis) {
                        data->vertices[(i * 2) + 0].colour = y;
                        data->vertices[(i * 2) + 1].colour = y;
                    } else {
                        // Set non-hit axes back to their original colours.
                        data->vertices[(i * 2) + 0].colour = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
                        data->vertices[(i * 2) + 0].colour.elements[i] = 1.0f;
                        data->vertices[(i * 2) + 1].colour = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
                        data->vertices[(i * 2) + 1].colour.elements[i] = 1.0f;
                    }
                }

                // xyz
                if (hit_axis == 6) {
                    // Turn them all yellow.
                    for (u32 i = 0; i < 18; ++i) {
                        data->vertices[i].colour = y;
                    }
                } else {
                    if (hit_axis == 3) {
                        // x/y
                        // 6/7, 12/13
                        data->vertices[6].colour = y;
                        data->vertices[7].colour = y;
                        data->vertices[12].colour = y;
                        data->vertices[13].colour = y;
                    } else {
                        data->vertices[6].colour = r;
                        data->vertices[7].colour = r;
                        data->vertices[12].colour = g;
                        data->vertices[13].colour = g;
                    }

                    if (hit_axis == 4) {
                        // x/z
                        // 8/9, 16/17
                        data->vertices[8].colour = y;
                        data->vertices[9].colour = y;
                        data->vertices[16].colour = y;
                        data->vertices[17].colour = y;
                    } else {
                        data->vertices[8].colour = r;
                        data->vertices[9].colour = r;
                        data->vertices[16].colour = b;
                        data->vertices[17].colour = b;
                    }

                    if (hit_axis == 5) {
                        // y/z
                        // 10/11, 14/15
                        data->vertices[10].colour = y;
                        data->vertices[11].colour = y;
                        data->vertices[14].colour = y;
                        data->vertices[15].colour = y;
                    } else {
                        data->vertices[10].colour = g;
                        data->vertices[11].colour = g;
                        data->vertices[14].colour = b;
                        data->vertices[15].colour = b;
                    }
                }
                renderer_geometry_vertex_update(&data->geo, 0, data->vertex_count, data->vertices);
            }
        }
    } else if (gizmo->mode == EDITOR_GIZMO_MODE_SCALE) {
        if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
            // NOTE: Don't handle interaction if there's no current axis.
            if (data->current_axis_index == INVALID_ID_U8) {
                return;
            }
            mat4 gizmo_world = transform_world_get(&gizmo->xform);

            vec3 intersection = {0};
            f32 distance;
            if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance)) {
                // Try from the other direction.
                if (!raycast_plane_3d(r, &data->interaction_plane_back, &intersection, &distance)) {
                    return;
                }
            }
            // vec3 diff = vec3_sub(intersection, data->last_interaction_pos);
            vec3 direction;
            vec3 scale;
            vec3 origin = transform_position_get(&gizmo->xform);

            // Scale along the current axis' line in local space.
            // This will be transformed to global later if need be.
            switch (data->current_axis_index) {
                case 0:  // x
                    direction = vec3_right();
                    break;
                case 1:  // y
                    direction = vec3_up();
                    break;
                case 2:  // z
                    direction = vec3_forward();
                    break;
                case 3:  // xy
                    // Combine the 2 axes, scale along both.
                    direction = vec3_normalized(vec3_mul_scalar(vec3_add(vec3_right(), vec3_up()), 0.5f));
                    break;
                case 4:  // xz
                    // Combine the 2 axes, scale along both.
                    direction = vec3_normalized(vec3_mul_scalar(vec3_add(vec3_right(), vec3_back()), 0.5f));
                    break;
                case 5:  // yz
                    // Combine the 2 axes, scale along both.
                    direction = vec3_normalized(vec3_mul_scalar(vec3_add(vec3_back(), vec3_up()), 0.5f));
                    break;
                case 6:  // xyz
                    direction = vec3_normalized(vec3_one());
                    break;
                default:
                    return;
            }
            // The distance from the origin ultimately determines scale magnitude.
            f32 dist = vec3_distance(origin, intersection);

            // Get the direction of the intersection from the origin.
            vec3 dir_from_origin = vec3_normalized(vec3_sub(intersection, origin));

            // Get the transformed direction.
            vec3 direction_t;
            if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_LOCAL) {
                if (data->current_axis_index < 6) {
                    direction_t = vec3_transform(direction, 0.0f, gizmo_world);
                } else {
                    // NOTE: In the case of uniform scale, base on the local up vector.
                    direction_t = vec3_transform(vec3_up(), 0.0f, gizmo_world);
                }
            } else if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
                // Use the direction as-is.
                direction_t = direction;
            } else {
                // TODO: Other orientations.

                // Use the direction as-is.
                direction_t = direction;
                return;
            }

            // Determine the sign of the magnitude by taking the dot
            // product between the direction toward the intersection from the
            // origin, then taking its sign.
            f32 d = ksign(vec3_dot(direction_t, dir_from_origin));

            // Calculate the scale difference by taking the
            // signed magnitude and scaling the untransformed directon by it.
            scale = vec3_mul_scalar(direction, d * dist);

            // For global transforms, get the inverse of the rotation and apply that
            // to the scale to scale on absolute (global) axes instead of local.
            if (gizmo->orientation == EDITOR_GIZMO_ORIENTATION_GLOBAL) {
                if (gizmo->selected_xform) {
                    quat q = quat_inverse(transform_rotation_get(gizmo->selected_xform));
                    scale = vec3_rotate(scale, q);
                }
            }

            KTRACE("scale (diff): [%.4f,%.4f,%.4f]", scale.x, scale.y, scale.z);
            // Apply scale to selected object.
            if (gizmo->selected_xform) {
                vec3 current_scale = transform_scale_get(gizmo->selected_xform);

                // Apply scale, but only on axes that have changed.
                for (u8 i = 0; i < 3; ++i) {
                    if (scale.elements[i] != 0.0f) {
                        current_scale.elements[i] = scale.elements[i];
                    }
                }
                KTRACE("Applying scale: [%.4f,%.4f,%.4f]", current_scale.x, current_scale.y, current_scale.z);
                transform_scale_set(gizmo->selected_xform, current_scale);
            }
            data->last_interaction_pos = intersection;
        } else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
            f32 dist;
            gizmo->xform.is_dirty = true;
            u8 hit_axis = INVALID_ID_U8;

            // Loop through each axis/axis combo. Loop backwards to give priority to combos since
            // those hit boxes are much smaller.
            mat4 gizmo_world = transform_world_get(&gizmo->xform);
            for (i32 i = 6; i > -1; --i) {
                if (raycast_oriented_extents(data->mode_extents[i], gizmo_world, r, &dist)) {
                    hit_axis = i;
                    break;
                }
            }

            // Handle highlighting.
            vec4 y = vec4_create(1.0f, 1.0f, 0.0f, 1.0f);
            vec4 r = vec4_create(1.0f, 0.0f, 0.0f, 1.0f);
            vec4 g = vec4_create(0.0f, 1.0f, 0.0f, 1.0f);
            vec4 b = vec4_create(0.0f, 0.0f, 1.0f, 1.0f);

            if (data->current_axis_index != hit_axis) {
                data->current_axis_index = hit_axis;

                // Main axis colours
                for (u32 i = 0; i < 3; ++i) {
                    if (i == hit_axis) {
                        data->vertices[(i * 2) + 0].colour = y;
                        data->vertices[(i * 2) + 1].colour = y;
                    } else {
                        // Set non-hit axes back to their original colours.
                        data->vertices[(i * 2) + 0].colour = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
                        data->vertices[(i * 2) + 0].colour.elements[i] = 1.0f;
                        data->vertices[(i * 2) + 1].colour = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
                        data->vertices[(i * 2) + 1].colour.elements[i] = 1.0f;
                    }
                }

                // xyz
                if (hit_axis == 6) {
                    // Turn them all yellow.
                    for (u32 i = 0; i < 12; ++i) {
                        data->vertices[i].colour = y;
                    }
                } else {
                    // x/y is 6/7
                    if (hit_axis == 3) {
                        data->vertices[6].colour = y;
                        data->vertices[7].colour = y;
                    } else {
                        data->vertices[6].colour = r;
                        data->vertices[7].colour = g;
                    }

                    // x/z is 10/11
                    if (hit_axis == 4) {
                        data->vertices[10].colour = y;
                        data->vertices[11].colour = y;
                    } else {
                        data->vertices[10].colour = r;
                        data->vertices[11].colour = b;
                    }

                    // z/y is 8/9
                    if (hit_axis == 5) {
                        data->vertices[8].colour = y;
                        data->vertices[9].colour = y;
                    } else {
                        data->vertices[8].colour = b;
                        data->vertices[9].colour = g;
                    }
                }
                renderer_geometry_vertex_update(&data->geo, 0, data->vertex_count, data->vertices);
            }
        }
    } else if (gizmo->mode == EDITOR_GIZMO_MODE_ROTATE) {
        if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
            // NOTE: No interaction needed if no current axis.
            if (data->current_axis_index == INVALID_ID_U8) {
                return;
            }

            // mat4 gizmo_world = transform_world_get(&gizmo->xform);
            vec3 origin = transform_position_get(&gizmo->xform);
            vec3 interaction_pos = {0};
            f32 distance;
            if (!raycast_plane_3d(r, &data->interaction_plane, &interaction_pos, &distance)) {
                // Try from the other direction.
                if (!raycast_plane_3d(r, &data->interaction_plane_back, &interaction_pos, &distance)) {
                    return;
                }
            }
            vec3 direction;

            // Get the difference in angle between this interaction and the last and use that as the
            // axis angle for rotation.
            vec3 v_0 = vec3_sub(data->last_interaction_pos, origin);
            vec3 v_1 = vec3_sub(interaction_pos, origin);
            f32 angle = kacos(vec3_dot(vec3_normalized(v_0), vec3_normalized(v_1)));
            // No angle means no change, so boot out.
            // NOTE: Also check for NaN, which can be done because floats have a unique property
            // that (x != x) detects NaN.
            if (angle == 0 || angle != angle) {
                return;
            }
            vec3 cross = vec3_cross(v_0, v_1);
            if (vec3_dot(data->interaction_plane.normal, cross) < 0) {
                angle = -angle;
            }

            mat4 gizmo_world = transform_world_get(&gizmo->xform);
            switch (data->current_axis_index) {
                case 0:  // x
                    direction = vec3_transform(vec3_right(), 0.0f, gizmo_world);
                    break;
                case 1:  // y
                    direction = vec3_transform(vec3_up(), 0.0f, gizmo_world);
                    break;
                case 2:  // z
                    direction = vec3_transform(vec3_back(), 0.0f, gizmo_world);
                    break;
                default:
                    return;
            }

            quat rotation = quat_from_axis_angle(direction, angle, true);
            // Apply rotation to gizmo here so it's visible.
            transform_rotate(&gizmo->xform, rotation);
            data->last_interaction_pos = interaction_pos;

            // Apply rotation.
            if (gizmo->selected_xform) {
                transform_rotate(gizmo->selected_xform, rotation);
            }

        } else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
            f32 dist;
            vec3 point;
            mat4 model = transform_world_get(&gizmo->xform);
            u8 hit_axis = INVALID_ID_U8;

            // Loop through each axis.
            for (u32 i = 0; i < 3; ++i) {
                // Oriented disc.
                vec3 aa_normal = vec3_zero();
                aa_normal.elements[i] = 1.0f;
                aa_normal = vec3_transform(aa_normal, 0.0f, model);
                vec3 center = transform_position_get(&gizmo->xform);
                if (raycast_disc_3d(r, center, aa_normal, radius + 0.05f, radius - 0.05f, &point, &dist)) {
                    hit_axis = i;
                    break;
                } else {
                    // If not, try from the other way.
                    aa_normal = vec3_mul_scalar(aa_normal, -1.0f);
                    if (raycast_disc_3d(r, center, aa_normal, radius + 0.05f, radius - 0.05f, &point, &dist)) {
                        hit_axis = i;
                        break;
                    }
                }
            }

            u32 segments2 = segments * 2;
            if (data->current_axis_index != hit_axis) {
                data->current_axis_index = hit_axis;

                // Main axis colours
                for (u32 i = 0; i < 3; ++i) {
                    vec4 set_colour = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
                    // Yellow for hit axis; otherwise original colour.
                    if (i == hit_axis) {
                        set_colour.r = 1.0f;
                        set_colour.g = 1.0f;
                    } else {
                        set_colour.elements[i] = 1.0f;
                    }

                    // Main axis in center.
                    data->vertices[(i * 2) + 0].colour = set_colour;
                    data->vertices[(i * 2) + 1].colour = set_colour;

                    // Ring
                    u32 ring_offset = 6 + (segments2 * i);
                    for (u32 j = 0; j < segments; ++j) {
                        data->vertices[ring_offset + (j * 2) + 0].colour = set_colour;
                        data->vertices[ring_offset + (j * 2) + 1].colour = set_colour;
                    }
                }
            }

            renderer_geometry_vertex_update(&data->geo, 0, data->vertex_count, data->vertices);
        }
    }
}
