#include "track.h"
#include "core/engine.h"
#include "defines.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "systems/material_system.h"

#include <containers/darray.h>

static vec3 calculate_normal(vec3 segment_vector, f32 rotation_y);

static vec3 calculate_bezier(vec3 point_0, vec3 point_1, vec3 control_point_0, vec3 control_point_1, f32 t) {
    f32 u = 1.0f - t;
    f32 tt = t * t;
    f32 uu = u * u;
    f32 uuu = uu * u;
    f32 ttt = tt * t;

    vec3 result = {
        uuu * point_0.x + 3 * uu * t * control_point_0.x + 3 * u * tt * control_point_1.x + ttt * point_1.x,
        uuu * point_0.y + 3 * uu * t * control_point_0.y + 3 * u * tt * control_point_1.y + ttt * point_1.y,
        uuu * point_0.z + 3 * uu * t * control_point_0.z + 3 * u * tt * control_point_1.z + ttt * point_1.z};
    return result;
}

static vec3 default_handle(vec3 position, f32 rotation_y, f32 handle_factor) {
    vec3 direction = {kcos(rotation_y), 0.0f, ksin(rotation_y)};
    direction = vec3_mul_scalar(direction, handle_factor);
    return vec3_add(direction, position);
    /* vec3 direction = vec3_normalized(vec3_sub(point_1, point_0));
    f32 d = handle_factor * vec3_distance(point_0, point_1);
    return vec3_add(point_0, vec3_mul_scalar(direction, d)); */
}

b8 track_create(track* out_track) {
    if (!out_track) {
        return false;
    }

    // HACK: defining some hardcoded stuff for now - should be configurable.
    out_track->points = darray_reserve(track_point, 5);
    darray_length_set(out_track->points, 5);

    // Position, left_width, left_height, right_width, right_height, rotation_y
    out_track->points[0] = (track_point){{0.0f, 1.0f, 0.0f}, 2.0f, 0.0f, 4.0f, 0.25f, deg_to_rad(0.0f)};
    out_track->points[1] = (track_point){{5.0f, 2.0f, 5.0f}, 3.0f, 0.25f, 3.0f, 0.5f, deg_to_rad(45.0f)};
    out_track->points[2] = (track_point){{5.0f, 5.0f, 10.0f}, 4.0f, -0.5f, 6.0f, 1.0f, deg_to_rad(90.0f)};
    out_track->points[3] = (track_point){{7.5f, 6.0f, 20.0f}, 5.0f, 1.0f, 5.0f, 1.5f, deg_to_rad(135.0f)};
    out_track->points[4] = (track_point){{2.0f, 6.0f, 23.0f}, 5.0f, 1.0f, 5.0f, 1.5f, deg_to_rad(180.0f)};

    // Number of divisions to make per segment.
    out_track->segment_resolution = 5;

    out_track->segments = darray_reserve(track_segment, darray_length(out_track->points) - 1);
    darray_length_set(out_track->segments, darray_length(out_track->points) - 1);

    return true;
}

b8 track_initialize(track* trk) {
    if (!trk) {
        return false;
    }

    u32 point_count = darray_length(trk->points);

    // Track segments

    // Each segment is defined by a start and end track point.
    for (u32 i = 0; i < point_count - 1; ++i) {

        u32 vi = 0;
        u32 ii = 0;

        track_segment* segment = &trk->segments[i];

        segment->start = &trk->points[i];
        segment->end = &trk->points[i + 1];

        // Generate geometry.
        segment->geometry.type = KGEOMETRY_TYPE_3D_STATIC;
        segment->geometry.name = kname_create("track_segment");

        segment->geometry.vertex_element_size = sizeof(vertex_3d);
        segment->geometry.index_element_size = sizeof(u32);
        segment->geometry.generation = INVALID_ID_U16;
        segment->geometry.vertex_buffer_offset = INVALID_ID_U64;
        segment->geometry.index_buffer_offset = INVALID_ID_U64;
        segment->geometry.vertex_count = 3 + (3 * trk->segment_resolution);
        segment->geometry.vertices = kallocate(sizeof(vertex_3d) * segment->geometry.vertex_count, MEMORY_TAG_ARRAY);
        segment->geometry.index_count = 12 * trk->segment_resolution; // 6 per face, 2 faces per segment Tessellation
        segment->geometry.indices = kallocate(sizeof(u32) * segment->geometry.index_count, MEMORY_TAG_ARRAY);

        vertex_3d* verts = (vertex_3d*)segment->geometry.vertices;
        u32* indices = (u32*)segment->geometry.indices;

        // Get the direction of the current segment.
        /* vec3 segment_direction = vec3_sub(end->position, start->position); */

        vec3 start_direction = {kcos(segment->start->rotation_y), 0.0f, ksin(segment->start->rotation_y)};
        vec3 start_normal = vec3_cross(start_direction, vec3_up());
        vec3 end_direction = {kcos(segment->end->rotation_y), 0.0f, ksin(segment->end->rotation_y)};
        vec3 end_normal = vec3_cross(end_direction, vec3_up());

        vec3 start_left = vec3_add(segment->start->position, vec3_mul_scalar(start_normal, -segment->start->left_width));
        start_left.y += segment->start->left_height;
        vec3 start_right = vec3_add(segment->start->position, vec3_mul_scalar(start_normal, segment->start->right_width));
        start_right.y += segment->start->right_height;

        vec3 end_left = vec3_add(segment->end->position, vec3_mul_scalar(end_normal, -segment->end->left_width));
        end_left.y += segment->end->left_height;
        vec3 end_right = vec3_add(segment->end->position, vec3_mul_scalar(end_normal, segment->end->right_width));
        end_right.y += segment->end->right_height;

        // Tessellation
        for (u32 t = 0; t <= trk->segment_resolution; ++t, vi += 3) {
            // How far into the segment this iteration is.
            f32 pct = (f32)t / (f32)trk->segment_resolution;

            // Interpolate center using bezier
            f32 handle_factor = vec3_distance(segment->start->position, segment->end->position);
            handle_factor *= 0.5f;                                                                                                       // proportion
            vec3 center_handle_0 = default_handle(segment->start->position, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 center_handle_1 = default_handle(segment->end->position, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 center = calculate_bezier(segment->start->position, segment->end->position, center_handle_0, center_handle_1, pct);

            // left
            handle_factor = vec3_distance(start_left, end_left);
            handle_factor *= 0.5f;                                                                                       // proportion
            vec3 left_handle_0 = default_handle(start_left, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 left_handle_1 = default_handle(end_left, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 left = calculate_bezier(start_left, end_left, left_handle_0, left_handle_1, pct);

            // right
            handle_factor = vec3_distance(start_right, end_right);
            handle_factor *= 0.5f;                                                                                         // proportion
            vec3 right_handle_0 = default_handle(start_right, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 right_handle_1 = default_handle(end_right, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 right = calculate_bezier(start_right, end_right, right_handle_0, right_handle_1, pct);

            // Vertex data.

            // store in the vertex array.
            // Left
            verts[vi + 0].position = left;
            verts[vi + 0].normal = (vec3){0, 0, 1};
            verts[vi + 0].texcoord.u = -1.0f;
            verts[vi + 0].texcoord.v = pct;
            // Center
            verts[vi + 1].position = center;
            verts[vi + 1].normal = (vec3){0, 0, 1};
            verts[vi + 1].texcoord.u = 0.0f;
            verts[vi + 1].texcoord.v = pct;
            // Right
            verts[vi + 2].position = right;
            verts[vi + 2].normal = (vec3){0, 0, 1};
            verts[vi + 2].texcoord.u = 1.0f;
            verts[vi + 2].texcoord.v = pct;

            // Generate index data per segment. This looks forward to the next
            // tessellated face. So skip the last iteration.
            if (t < trk->segment_resolution) {
                // left
                indices[ii + 0] = vi + 0;
                indices[ii + 1] = vi + 1;
                indices[ii + 2] = vi + 3;
                indices[ii + 3] = vi + 1;
                indices[ii + 4] = vi + 4;
                indices[ii + 5] = vi + 3;

                // right
                indices[ii + 6] = vi + 1;
                indices[ii + 7] = vi + 2;
                indices[ii + 8] = vi + 4;
                indices[ii + 9] = vi + 2;
                indices[ii + 10] = vi + 5;
                indices[ii + 11] = vi + 4;

                ii += 12;
            }
        }

        geometry_generate_normals(segment->geometry.vertex_count, verts, segment->geometry.index_count, indices);
        geometry_generate_tangents(segment->geometry.vertex_count, verts, segment->geometry.index_count, indices);
    }

    return true;
}

b8 track_load(track* t) {
    if (!t) {
        return false;
    }

    // Get a material for the track. This is only needed when visualizing the track.
    t->material = material_system_get_default_standard(engine_systems_get()->material_system);

    // Upload the geometry to the GPU. This is only needed to visualize the track.
    u32 segment_count = darray_length(t->segments);
    for (u32 i = 0; i < segment_count; ++i) {
        track_segment* segment = &t->segments[i];
        if (!renderer_geometry_upload(&segment->geometry)) {
            KERROR("Failed to upload geometry for track segment. Track load failed.");
            return false;
        }
    }

    return true;
}

void track_unload(track* t) {
    if (t) {

        material_system_release(engine_systems_get()->material_system, &t->material);

        // Upload the geometry to the GPU. This is only needed to visualize the track.
        u32 segment_count = darray_length(t->segments);
        for (u32 i = 0; i < segment_count; ++i) {
            renderer_geometry_destroy(&t->segments[i].geometry);
        }
    }
}

void track_destroy(track* t) {
    if (t) {
        u32 segment_count = darray_length(t->segments);
        for (u32 i = 0; i < segment_count; ++i) {
            track_segment* segment = &t->segments[i];
            if (segment->geometry.vertices && segment->geometry.vertex_count) {
                kfree(segment->geometry.vertices, sizeof(vertex_3d) * segment->geometry.vertex_count, MEMORY_TAG_ARRAY);
            }
            if (segment->geometry.indices && segment->geometry.index_count) {
                kfree(segment->geometry.indices, sizeof(u32) * segment->geometry.index_count, MEMORY_TAG_ARRAY);
            }
        }

        darray_destroy(t->segments);

        kzero_memory(t, sizeof(track));
    }
}
