#include "debug_box3d.h"

#include "core/identifier.h"
#include "core/khandle.h"
#include "core/kmemory.h"
#include "defines.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "systems/xform_system.h"

static void recalculate_extents(debug_box3d* box, extents_3d extents);
static void update_vert_colour(debug_box3d* box);

b8 debug_box3d_create(vec3 size, k_handle parent_xform, debug_box3d* out_box) {
    if (!out_box) {
        return false;
    }
    out_box->vertex_count = 0;
    out_box->vertices = 0;
    out_box->xform = xform_create();
    out_box->parent_xform = parent_xform;
    // out_box->name // TODO: name?
    out_box->size = size;
    out_box->id = identifier_create();
    out_box->colour = vec4_one(); // Default to white.

    out_box->geo.id = INVALID_ID;
    out_box->geo.generation = INVALID_ID_U16;
    out_box->is_dirty = true;

    return true;
}

void debug_box3d_destroy(debug_box3d* box) {
    // TODO: zero out, etc.
    box->id.uniqueid = INVALID_ID_U64;
}

void debug_box3d_parent_set(debug_box3d* box, k_handle parent_xform) {
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
        if (box->geo.generation != INVALID_ID_U16 && box->vertex_count && box->vertices) {
            update_vert_colour(box);
            box->is_dirty = true;
        }
    }
}

void debug_box3d_extents_set(debug_box3d* box, extents_3d extents) {
    if (box) {
        if (box->geo.generation != INVALID_ID_U16 && box->vertex_count && box->vertices) {
            recalculate_extents(box, extents);
            box->is_dirty = true;
        }
    }
}

void debug_box3d_points_set(debug_box3d* box, vec4* points) {
    if (box && points) {
        if (box->geo.generation != INVALID_ID_U16 && box->vertex_count && box->vertices) {
            // for (u32 i = 0; i < 8; ++i) {
            //     box->vertices[i].position = points[i];
            // }

            // Front lines
            {
                // top
                box->vertices[0].position = points[2];
                box->vertices[1].position = points[3];
                // right
                box->vertices[2].position = points[1];
                box->vertices[3].position = points[2];
                // bottom
                box->vertices[4].position = points[0];
                box->vertices[5].position = points[1];
                // left
                box->vertices[6].position = points[3];
                box->vertices[7].position = points[0];
            }
            // back lines
            {
                // top
                box->vertices[8].position = points[6];
                box->vertices[9].position = points[7];
                // right
                box->vertices[10].position = points[5];
                box->vertices[11].position = points[6];
                // bottom
                box->vertices[12].position = points[4];
                box->vertices[13].position = points[5];
                // left
                box->vertices[14].position = points[7];
                box->vertices[15].position = points[4];
            }

            // top connecting lines
            {
                // left
                box->vertices[16].position = points[3];
                box->vertices[17].position = points[7];
                // right
                box->vertices[18].position = points[2];
                box->vertices[19].position = points[6];
            }
            // bottom connecting lines
            {
                // left
                box->vertices[20].position = points[0];
                box->vertices[21].position = points[4];
                // right
                box->vertices[22].position = points[1];
                box->vertices[23].position = points[5];
            }
            box->is_dirty = true;
        }
    }
}

void debug_box3d_render_frame_prepare(debug_box3d* box, const struct frame_data* p_frame_data) {
    if (!box || !box->is_dirty) {
        return;
    }

    // Upload the new vertex data.
    renderer_geometry_vertex_update(&box->geo, 0, box->vertex_count, box->vertices, true);

    box->geo.generation++;

    // Roll this over to zero so we don't lock ourselves out of updating.
    if (box->geo.generation == INVALID_ID_U16) {
        box->geo.generation = 0;
    }

    box->is_dirty = false;
}

b8 debug_box3d_initialize(debug_box3d* box) {
    if (!box) {
        return false;
    }

    box->vertex_count = 2 * 12; // 12 lines to make a cube.
    box->vertices = kallocate(sizeof(colour_vertex_3d) * box->vertex_count, MEMORY_TAG_ARRAY);

    extents_3d extents = {0};
    extents.min.x = -box->size.x * 0.5f;
    extents.min.y = -box->size.y * 0.5f;
    extents.min.z = -box->size.z * 0.5f;
    extents.max.x = box->size.x * 0.5f;
    extents.max.y = box->size.y * 0.5f;
    extents.max.z = box->size.z * 0.5f;
    recalculate_extents(box, extents);

    update_vert_colour(box);

    return true;
}

b8 debug_box3d_load(debug_box3d* box) {
    if (!renderer_geometry_create(&box->geo, sizeof(colour_vertex_3d), box->vertex_count, box->vertices, 0, 0, 0)) {
        return false;
    }
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(&box->geo)) {
        return false;
    }
    if (box->geo.generation == INVALID_ID_U16) {
        box->geo.generation = 0;
    } else {
        box->geo.generation++;
    }
    return true;
}

b8 debug_box3d_unload(debug_box3d* box) {
    renderer_geometry_destroy(&box->geo);

    return true;
}

b8 debug_box3d_update(debug_box3d* box) {
    return true;
}

static void recalculate_extents(debug_box3d* box, extents_3d extents) {
    // Front lines
    {
        // top
        box->vertices[0].position = (vec4){extents.min.x, extents.min.y, extents.min.z, 1.0f};
        box->vertices[1].position = (vec4){extents.max.x, extents.min.y, extents.min.z, 1.0f};
        // right
        box->vertices[2].position = (vec4){extents.max.x, extents.min.y, extents.min.z, 1.0f};
        box->vertices[3].position = (vec4){extents.max.x, extents.max.y, extents.min.z, 1.0f};
        // bottom
        box->vertices[4].position = (vec4){extents.max.x, extents.max.y, extents.min.z, 1.0f};
        box->vertices[5].position = (vec4){extents.min.x, extents.max.y, extents.min.z, 1.0f};
        // left
        box->vertices[6].position = (vec4){extents.min.x, extents.min.y, extents.min.z, 1.0f};
        box->vertices[7].position = (vec4){extents.min.x, extents.max.y, extents.min.z, 1.0f};
    }
    // back lines
    {
        // top
        box->vertices[8].position = (vec4){extents.min.x, extents.min.y, extents.max.z, 1.0f};
        box->vertices[9].position = (vec4){extents.max.x, extents.min.y, extents.max.z, 1.0f};
        // right
        box->vertices[10].position = (vec4){extents.max.x, extents.min.y, extents.max.z, 1.0f};
        box->vertices[11].position = (vec4){extents.max.x, extents.max.y, extents.max.z, 1.0f};
        // bottom
        box->vertices[12].position = (vec4){extents.max.x, extents.max.y, extents.max.z, 1.0f};
        box->vertices[13].position = (vec4){extents.min.x, extents.max.y, extents.max.z, 1.0f};
        // left
        box->vertices[14].position = (vec4){extents.min.x, extents.min.y, extents.max.z, 1.0f};
        box->vertices[15].position = (vec4){extents.min.x, extents.max.y, extents.max.z, 1.0f};
    }

    // top connecting lines
    {
        // left
        box->vertices[16].position = (vec4){extents.min.x, extents.min.y, extents.min.z, 1.0f};
        box->vertices[17].position = (vec4){extents.min.x, extents.min.y, extents.max.z, 1.0f};
        // right
        box->vertices[18].position = (vec4){extents.max.x, extents.min.y, extents.min.z, 1.0f};
        box->vertices[19].position = (vec4){extents.max.x, extents.min.y, extents.max.z, 1.0f};
    }
    // bottom connecting lines
    {
        // left
        box->vertices[20].position = (vec4){extents.min.x, extents.max.y, extents.min.z, 1.0f};
        box->vertices[21].position = (vec4){extents.min.x, extents.max.y, extents.max.z, 1.0f};
        // right
        box->vertices[22].position = (vec4){extents.max.x, extents.max.y, extents.min.z, 1.0f};
        box->vertices[23].position = (vec4){extents.max.x, extents.max.y, extents.max.z, 1.0f};
    }
}

static void update_vert_colour(debug_box3d* box) {
    if (box) {
        if (box->vertex_count && box->vertices) {
            for (u32 i = 0; i < box->vertex_count; ++i) {
                box->vertices[i].colour = box->colour;
            }
        }
    }
}
