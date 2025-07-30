#include "debug_box3d.h"

#include "core_resource_types.h"
#include "defines.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_frontend.h"
#include "systems/xform_system.h"

static void update_vert_colour(debug_box3d* box);

b8 debug_box3d_create(vec3 size, ktransform parent_xform, debug_box3d* out_box) {
    if (!out_box) {
        return false;
    }
    out_box->xform = xform_create();
    out_box->parent_xform = parent_xform;
    // out_box->name // TODO: name?
    out_box->size = size;
    out_box->colour = vec4_one(); // Default to white.

    out_box->geometry.type = KGEOMETRY_TYPE_3D_STATIC_COLOUR_ONLY;
    out_box->geometry.generation = INVALID_ID_U16;
    out_box->is_dirty = true;

    return true;
}

void debug_box3d_destroy(debug_box3d* box) {
    // TODO: zero out, etc.
}

void debug_box3d_parent_set(debug_box3d* box, ktransform parent_xform) {
    if (box) {
        box->parent_xform = parent_xform;
    }
}

void debug_box3d_colour_set(debug_box3d* box, vec4 colour) {
    if (box) {
        if (colour.a == 0) {
            colour.a = 1.0f;
        }
        box->colour = colour;
        vec4_clamp(&box->colour, 0.0f, 1.0f);
        if (box->geometry.generation != INVALID_ID_U16 && box->geometry.vertex_count && box->geometry.vertices) {
            update_vert_colour(box);
            box->is_dirty = true;
        }
    }
}

void debug_box3d_extents_set(debug_box3d* box, extents_3d extents) {
    if (box) {
        if (box->geometry.generation != INVALID_ID_U16 && box->geometry.vertex_count && box->geometry.vertices) {
            geometry_recalculate_line_box3d_by_extents(&box->geometry, extents);
            box->is_dirty = true;
        }
    }
}

void debug_box3d_points_set(debug_box3d* box, vec3 points[8]) {
    if (box && points) {
        if (box->geometry.generation != INVALID_ID_U16 && box->geometry.vertex_count && box->geometry.vertices) {
            geometry_recalculate_line_box3d_by_points(&box->geometry, points);

            box->is_dirty = true;
        }
    }
}

void debug_box3d_render_frame_prepare(debug_box3d* box, const struct frame_data* p_frame_data) {
    if (!box || !box->is_dirty) {
        return;
    }

    // Upload the new vertex data.
    renderer_geometry_vertex_update(&box->geometry, 0, box->geometry.vertex_count, box->geometry.vertices, true);

    box->geometry.generation++;

    // Roll this over to zero so we don't lock ourselves out of updating.
    if (box->geometry.generation == INVALID_ID_U16) {
        box->geometry.generation = 0;
    }

    box->is_dirty = false;
}

b8 debug_box3d_initialize(debug_box3d* box) {
    if (!box) {
        return false;
    }

    box->geometry = geometry_generate_line_box3d(box->size, box->name);

    update_vert_colour(box);

    return true;
}

b8 debug_box3d_load(debug_box3d* box) {
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(&box->geometry)) {
        return false;
    }
    if (box->geometry.generation == INVALID_ID_U16) {
        box->geometry.generation = 0;
    } else {
        box->geometry.generation++;
    }
    return true;
}

b8 debug_box3d_unload(debug_box3d* box) {
    renderer_geometry_destroy(&box->geometry);

    return true;
}

b8 debug_box3d_update(debug_box3d* box) {
    return true;
}

static void update_vert_colour(debug_box3d* box) {
    if (box) {
        if (box->geometry.vertex_count && box->geometry.vertices) {
            colour_vertex_3d* verts = (colour_vertex_3d*)box->geometry.vertices;
            for (u32 i = 0; i < box->geometry.vertex_count; ++i) {
                verts[i].colour = box->colour;
            }
            box->is_dirty = true;
        }
    }
}
