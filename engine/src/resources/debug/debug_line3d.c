#include "debug_line3d.h"

#include "core/identifier.h"
#include "core/kmemory.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "renderer/renderer_frontend.h"

static void recalculate_points(debug_line3d *line);
static void update_vert_colour(debug_line3d *line);

b8 debug_line3d_create(vec3 point_0, vec3 point_1, transform *parent, debug_line3d *out_line) {
    if (!out_line) {
        return false;
    }
    out_line->vertex_count = 0;
    out_line->vertices = 0;
    out_line->xform = transform_create();
    if (parent) {
        transform_parent_set(&out_line->xform, parent);
    }
    // out_line->name // TODO: name?
    out_line->point_0 = point_0;
    out_line->point_1 = point_1;
    out_line->id = identifier_create();
    out_line->colour = vec4_one();  // Default to white.

    out_line->geo.id = INVALID_ID;
    out_line->geo.generation = INVALID_ID_U16;
    out_line->geo.internal_id = INVALID_ID;

    return true;
}

void debug_line3d_destroy(debug_line3d *line) {
    // TODO: zero out, etc.
    line->id.uniqueid = INVALID_ID_U64;
}

void debug_line3d_parent_set(debug_line3d *line, transform *parent) {
    if (line) {
        transform_parent_set(&line->xform, parent);
    }
}

void debug_line3d_colour_set(debug_line3d *line, vec4 colour) {
    if (line) {
        if (colour.a == 0) {
            colour.a = 1.0f;
        }
        line->colour = colour;
        if (line->geo.generation != INVALID_ID_U16 && line->vertex_count && line->vertices) {
            update_vert_colour(line);

            // Upload the new vertex data.
            renderer_geometry_vertex_update(&line->geo, 0, line->vertex_count, line->vertices);

            line->geo.generation++;

            // Roll this over to zero so we don't lock ourselves out of updating.
            if (line->geo.generation == INVALID_ID_U16) {
                line->geo.generation = 0;
            }
        }
    }
}

void debug_line3d_points_set(debug_line3d *line, vec3 point_0, vec3 point_1) {
    if (line) {
        if (line->geo.generation != INVALID_ID_U16 && line->vertex_count && line->vertices) {
            line->point_0 = point_0;
            line->point_1 = point_1;
            recalculate_points(line);

            // Upload the new vertex data.
            renderer_geometry_vertex_update(&line->geo, 0, line->vertex_count, line->vertices);

            line->geo.generation++;

            // Roll this over to zero so we don't lock ourselves out of updating.
            if (line->geo.generation == INVALID_ID_U16) {
                line->geo.generation = 0;
            }
        }
    }
}

b8 debug_line3d_initialize(debug_line3d *line) {
    if (!line) {
        return false;
    }

    line->vertex_count = 2;  // Just 2 points for a line.
    line->vertices = kallocate(sizeof(colour_vertex_3d) * line->vertex_count, MEMORY_TAG_ARRAY);

    recalculate_points(line);
    update_vert_colour(line);

    return true;
}

b8 debug_line3d_load(debug_line3d *line) {
    if (!renderer_geometry_create(&line->geo, sizeof(colour_vertex_3d), line->vertex_count, line->vertices, 0, 0, 0)) {
        return false;
    }
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(&line->geo)) {
        return false;
    }
    if (line->geo.generation == INVALID_ID_U16) {
        line->geo.generation = 0;
    } else {
        line->geo.generation++;
    }
    return true;
}

b8 debug_line3d_unload(debug_line3d *line) {
    renderer_geometry_destroy(&line->geo);

    return true;
}

b8 debug_line3d_update(debug_line3d *line) {
    return true;
}

static void recalculate_points(debug_line3d *line) {
    if (line) {
        line->vertices[0].position = (vec4){line->point_0.x, line->point_0.y, line->point_0.z, 1.0f};
        line->vertices[1].position = (vec4){line->point_1.x, line->point_1.y, line->point_1.z, 1.0f};
    }
}

static void update_vert_colour(debug_line3d *line) {
    if (line) {
        if (line->vertex_count && line->vertices) {
            for (u32 i = 0; i < line->vertex_count; ++i) {
                line->vertices[i].colour = line->colour;
            }
        }
    }
}
