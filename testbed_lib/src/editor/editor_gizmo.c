
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

b8 editor_gizmo_create(editor_gizmo* out_gizmo) {
    if (!out_gizmo) {
        KERROR("Unable to create gizmo with an invalid out pointer.");
        return false;
    }

    out_gizmo->mode = EDITOR_GIZMO_MODE_NONE;
    out_gizmo->xform = transform_create();
    out_gizmo->selected_xform = 0;

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

    return true;
}

b8 editor_gizmo_unload(editor_gizmo* gizmo) {
    if (gizmo) {
    }
    return true;
}

void editor_gizmo_update(editor_gizmo* gizmo) {
    if (gizmo) {
        if (gizmo->selected_xform) {
            transform_position_set(&gizmo->xform, gizmo->selected_xform->position);
            transform_rotation_set(&gizmo->xform, gizmo->selected_xform->rotation);
        }
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
}

static void create_gizmo_mode_rotate(editor_gizmo* gizmo) {
    editor_gizmo_mode_data* data = &gizmo->mode_data[EDITOR_GIZMO_MODE_ROTATE];
    const u8 segments = 32;
    const f32 radius = 1.0f;

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
}

void editor_gizmo_interaction_begin(editor_gizmo* gizmo, camera* c, struct ray* r, editor_gizmo_interaction_type interaction_type) {
    if (!gizmo || !r) {
        return;
    }

    gizmo->interaction = interaction_type;

    if (gizmo->interaction == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
        if (gizmo->mode == EDITOR_GIZMO_MODE_MOVE) {
            // Create the plane.
            editor_gizmo_mode_data* data = &gizmo->mode_data[gizmo->mode];
            mat4 gizmo_world = transform_world_get(&gizmo->xform);
            vec3 origin = transform_position_get(&gizmo->xform);
            vec3 plane_dir;
            switch (data->current_axis_index) {
                case 0:  // x axis
                case 3:  // xy axes
                    plane_dir = mat4_backward(gizmo_world);
                    break;
                case 1:  // y axis
                    plane_dir = camera_backward(c);
                    break;
                case 4:  // xz axes
                    plane_dir = mat4_down(gizmo_world);
                    break;
                case 2:  // z axis
                case 5:  // yz axes
                    plane_dir = mat4_left(gizmo_world);
                    break;
                case 6:  /// xyz
                    plane_dir = camera_backward(c);
                    return;
            }
            data->interaction_plane = plane_3d_create(origin, plane_dir);

            // Get the initial intersection point of the ray on the plane.
            vec3 intersection = {0};
            f32 distance;
            b8 front_facing;
            if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance, &front_facing)) {
                KERROR("Raycast/plane intersection not found. But hwhy?");
                return;
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

    gizmo->interaction = EDITOR_GIZMO_INTERACTION_TYPE_NONE;
}

void editor_gizmo_handle_interaction(editor_gizmo* gizmo, struct camera* c, struct ray* r, editor_gizmo_interaction_type interaction_type) {
    if (!gizmo || !r) {
        return;
    }

    if (gizmo->mode == EDITOR_GIZMO_MODE_MOVE) {
        editor_gizmo_mode_data* data = &gizmo->mode_data[gizmo->mode];
        if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG) {
            mat4 gizmo_world = transform_world_get(&gizmo->xform);

            vec3 intersection = {0};
            f32 distance;
            b8 front_facing;
            if (!raycast_plane_3d(r, &data->interaction_plane, &intersection, &distance, &front_facing)) {
                KERROR("Raycast/plane intersection not found. But hwhy?");
                return;
            }
            vec3 diff = vec3_sub(intersection, data->last_interaction_pos);
            vec3 direction;
            vec3 translation;
            switch (data->current_axis_index) {
                case 0:  // x
                    direction = mat4_left(gizmo_world);
                    // Project diff onto direction.
                    translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
                    break;
                case 1:  // y
                    direction = mat4_up(gizmo_world);
                    // Project diff onto direction.
                    translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
                    break;
                case 2:  // z
                    direction = mat4_backward(gizmo_world);
                    // Project diff onto direction.
                    translation = vec3_mul_scalar(direction, vec3_dot(diff, direction));
                    break;
                case 3:  // xy
                case 4:  // xz
                case 5:  // yz
                case 6:  // xyz
                    // TODO: this just kinda disappears on occasion. Debug this.
                    translation = diff;
                    break;
                default:
                    return;
            }
            transform_translate(&gizmo->xform, translation);
            data->last_interaction_pos = intersection;
        } else if (interaction_type == EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER) {
            f32 dist;
            mat4 model = transform_world_get(&gizmo->xform);
            u8 hit_axis = INVALID_ID_U8;

            f32 scale_scalar = gizmo->scale_scalar;
            mat4 scale = mat4_scale((vec3){scale_scalar, scale_scalar, scale_scalar});
            model = mat4_mul(model, scale);

            // Loop through each axis/axis combo. Loop backwards to give priority to combos since
            // those hit boxes are much smaller.
            for (i32 i = 6; i > -1; --i) {
                if (raycast_oriented_extents(data->mode_extents[i], &model, r, &dist)) {
                    hit_axis = i;
                    break;
                }
            }

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
    }
}
