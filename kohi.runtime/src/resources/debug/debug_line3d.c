#include "debug_line3d.h"

#include "core_resource_types.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "systems/ktransform_system.h"

static void recalculate_points(debug_line3d* line);
static void update_vert_colour(debug_line3d* line);

b8 debug_line3d_create(vec3 point_0, vec3 point_1, ktransform parent_ktransform, debug_line3d* out_line) {
    if (!out_line) {
        return false;
    }
    out_line->ktransform = ktransform_create(0);
    out_line->ktransform_parent = parent_ktransform;
    // out_line->name // TODO: name?
    out_line->point_0 = point_0;
    out_line->point_1 = point_1;
    out_line->colour = vec4_one(); // Default to white.

    out_line->geometry.type = KGEOMETRY_TYPE_3D_STATIC_COLOUR;
    out_line->geometry.generation = INVALID_ID_U16;
    out_line->is_dirty = true;

    return true;
}

void debug_line3d_destroy(debug_line3d* line) {
    if (line) {
        geometry_destroy(&line->geometry);
        kzero_memory(line, sizeof(debug_line3d));
    }
}

void debug_line3d_parent_set(debug_line3d* line, ktransform parent_ktransform) {
    if (line) {
        line->ktransform_parent = parent_ktransform;
    }
}

void debug_line3d_colour_set(debug_line3d* line, vec4 colour) {
    if (line) {
        if (colour.a == 0) {
            colour.a = 1.0f;
        }
        line->colour = colour;
        if (line->geometry.generation != INVALID_ID_U16 && line->geometry.vertex_count && line->geometry.vertices) {
            update_vert_colour(line);
            line->is_dirty = true;
        }
    }
}

void debug_line3d_points_set(debug_line3d* line, vec3 point_0, vec3 point_1) {
    if (line) {
        if (line->geometry.generation != INVALID_ID_U16 && line->geometry.vertex_count && line->geometry.vertices) {
            line->point_0 = point_0;
            line->point_1 = point_1;
            recalculate_points(line);
            line->is_dirty = true;
        }
    }
}

void debug_line3d_render_frame_prepare(debug_line3d* line, const struct frame_data* p_frame_data) {
    if (!line || !line->is_dirty) {
        return;
    }

    // Upload the new vertex data.
    renderer_geometry_vertex_update(&line->geometry, 0, line->geometry.vertex_count, line->geometry.vertices, true);

    line->geometry.generation++;

    // Roll this over to zero so we don't lock ourselves out of updating.
    if (line->geometry.generation == INVALID_ID_U16) {
        line->geometry.generation = 0;
    }

    line->is_dirty = false;
}

b8 debug_line3d_initialize(debug_line3d* line) {
    if (!line) {
        return false;
    }

    line->geometry = geometry_generate_line3d(line->point_0, line->point_1, INVALID_KNAME);

    recalculate_points(line);
    update_vert_colour(line);

    return true;
}

b8 debug_line3d_load(debug_line3d* line) {
    // Send the geometry off to the renderer to be uploaded to the GPU.
    return renderer_geometry_upload(&line->geometry);
}

b8 debug_line3d_unload(debug_line3d* line) {
    renderer_geometry_destroy(&line->geometry);

    return true;
}

b8 debug_line3d_update(debug_line3d* line) {
    return true;
}

static void recalculate_points(debug_line3d* line) {
    if (line) {
        colour_vertex_3d* verts = (colour_vertex_3d*)line->geometry.vertices;
        verts[0].position = vec4_from_vec3(line->point_0, 1.0f);
        verts[1].position = vec4_from_vec3(line->point_1, 1.0f);
    }
}

static void update_vert_colour(debug_line3d* line) {
    if (line) {
        if (line->geometry.vertex_count && line->geometry.vertices) {
            colour_vertex_3d* verts = (colour_vertex_3d*)line->geometry.vertices;
            for (u32 i = 0; i < line->geometry.vertex_count; ++i) {
                verts[i].colour = line->colour;
            }
        }
    }
}
