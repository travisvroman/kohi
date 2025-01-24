#include "track.h"
#include "core/engine.h"
#include "defines.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "systems/material_system.h"

#include <containers/darray.h>

static vec3 calculate_normal(vec3 segment_vector, f32 rotation_y);

b8 track_create(track* out_track) {
    if (!out_track) {
        return false;
    }

    // HACK: defining some hardcoded stuff for now - should be configurable.
    out_track->points = darray_reserve(track_point, 4);
    darray_length_set(out_track->points, 4);

    // Position, left_width, left_height, right_width, right_height
    out_track->points[0] = (track_point){{0.0f, 0.0f, 0.0f}, 2.0f, 0.0f, 4.0f, 0.25f, deg_to_rad(13.0f)};
    out_track->points[1] = (track_point){{5.0f, 1.0f, 5.0f}, 3.0f, 0.25f, 3.0f, 0.5f, deg_to_rad(25.0f)};
    out_track->points[2] = (track_point){{10.0f, 5.0f, 10.0f}, 4.0f, -0.5f, 6.0f, 1.0f, deg_to_rad(45.0f)};
    out_track->points[3] = (track_point){{15.0f, 6.0f, 15.0f}, 5.0f, 1.0f, 5.0f, 1.5f, deg_to_rad(60.0f)};

    return true;
}

b8 track_initialize(track* trk) {
    if (!trk) {
        return false;
    }

    // Generate geometry.
    trk->geometry.type = KGEOMETRY_TYPE_3D_STATIC;
    trk->geometry.name = kname_create("track_base_collision");

    // Number of divisions to make per segment.
    trk->segment_resolution = 5;

    u32 point_count = darray_length(trk->points);
    trk->geometry.vertex_element_size = sizeof(vertex_3d);
    trk->geometry.index_element_size = sizeof(u32);
    trk->geometry.generation = INVALID_ID_U16;
    trk->geometry.vertex_buffer_offset = INVALID_ID_U64;
    trk->geometry.index_buffer_offset = INVALID_ID_U64;

    // HACK: test
    /* t->geometry.vertex_count = 4;
    t->geometry.vertices = kallocate(sizeof(vertex_3d) * t->geometry.vertex_count, MEMORY_TAG_ARRAY);
    t->geometry.index_count = 6;
    t->geometry.indices = kallocate(sizeof(u32) * t->geometry.index_count, MEMORY_TAG_ARRAY);

    vertex_3d* verts = (vertex_3d*)t->geometry.vertices;
    u32* indices = (u32*)t->geometry.indices;

    verts[0].position = (vec3){0, 0, 0};
    verts[1].position = (vec3){1, 0, 0};
    verts[2].position = (vec3){1, 0, 1};
    verts[3].position = (vec3){0, 0, 1};

    indices[0] = 2;
    indices[1] = 1;
    indices[2] = 0;
    indices[3] = 3;
    indices[4] = 2;
    indices[5] = 0;
    if (point_count) {
    } */

    trk->geometry.vertex_count = point_count * 6 * trk->segment_resolution; // 2 faces, start/end left, start/end center, start/end right.
    trk->geometry.vertices = kallocate(sizeof(vertex_3d) * trk->geometry.vertex_count, MEMORY_TAG_ARRAY);
    trk->geometry.index_count = point_count * 12 * trk->segment_resolution; // 6 per face, 2 faces per segment Tessellation
    trk->geometry.indices = kallocate(sizeof(u32) * trk->geometry.index_count, MEMORY_TAG_ARRAY);

    vertex_3d* verts = (vertex_3d*)trk->geometry.vertices;
    u32* indices = (u32*)trk->geometry.indices;

    // Each segment is defined by a start and end track point.
    u32 vi = 0;
    u32 ii = 0;
    for (u32 i = 0; i < point_count - 1; ++i) {

        track_point* start = &trk->points[i];
        track_point* end = &trk->points[i + 1];

        vec3 segment_vector = vec3_sub(end->position, start->position);

        // Tessellation
        for (u32 t = 0; t <= trk->segment_resolution; ++t, vi += 3) {
            f32 pct = (f32)t / trk->segment_resolution;

            vec3 center = vec3_add(start->position, vec3_mul_scalar(segment_vector, pct));

            f32 left_width = klerp(start->left_width, end->left_width, pct);
            f32 right_width = klerp(start->right_width, end->right_width, pct);
            f32 rotation_y = klerp(start->rotation_y, end->rotation_y, pct);

            vec3 normal = calculate_normal(segment_vector, rotation_y);

            // Vertex data.
            vec3 left = (vec3){
                center.x - normal.x * left_width,
                center.y - normal.y,
                center.z - normal.z * left_width};

            vec3 right = (vec3){
                center.x + normal.x * right_width,
                center.y + normal.y,
                center.z + normal.z * right_width};

            // store in the vertex array.
            verts[vi + 0].position = left;
            verts[vi + 1].position = center;
            verts[vi + 2].position = right;

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
    }
    return true;
}

b8 track_load(track* t) {
    if (!t) {
        return false;
    }

    t->material = material_system_get_default_standard(engine_systems_get()->material_system);

    return renderer_geometry_upload(&t->geometry);
}

void track_unload(track* t) {
    if (t) {
        renderer_geometry_destroy(&t->geometry);
    }
}

void track_destroy(track* t) {
    if (t) {
        if (t->geometry.vertices && t->geometry.vertex_count) {
            kfree(t->geometry.vertices, sizeof(vertex_3d) * t->geometry.vertex_count, MEMORY_TAG_ARRAY);
        }
        if (t->geometry.indices && t->geometry.index_count) {
            kfree(t->geometry.indices, sizeof(u32) * t->geometry.index_count, MEMORY_TAG_ARRAY);
        }

        kzero_memory(t, sizeof(track));
        t->geometry.generation = INVALID_ID_U16;
    }
}

static f32 compute_interpolated_width(vec3 point, vec3 segment_start, vec3 segment_end, f32 start_width, f32 end_width) {

    // Get the vector along the segment.
    vec3 segment_vector = vec3_sub(segment_end, segment_start);

    // From start to point.
    vec3 to_point = vec3_sub(point, segment_start);

    // Projection
    f32 segment_length_sq = vec3_dot(segment_vector, segment_vector);

    // Figure out how far along the segment the point is.
    f32 t = vec3_dot(to_point, segment_vector) / segment_length_sq;

    // Clamp t to [0-1] to ensure it is within the segment.
    t = KCLAMP(t, 0, 1);

    // Interpolate between start and end widths based on t.
    return klerp(start_width, end_width, t);
}

vec3 project_point_onto_segment(vec3 point, vec3 segment_start, vec3 segment_end) {
    // Get the vector along the segment.
    vec3 segment_vector = vec3_sub(segment_end, segment_start);

    // From start to point.
    vec3 to_point = vec3_sub(point, segment_start);

    // Projection
    f32 segment_length_sq = vec3_dot(segment_vector, segment_vector);

    // Edge case: segment is a single point
    if (segment_length_sq == 0.0f) {
        // Segment start and end are at the same point.
        return segment_start;
    }

    // Figure out how far along the segment the point is.
    f32 t = vec3_dot(to_point, segment_vector) / segment_length_sq;

    // Clamp t to [0-1] to ensure it is within the segment.
    t = KCLAMP(t, 0, 1);

    // Calculate the projected point using the clamped t value.
    vec3 projected_point = {
        segment_start.x + t * segment_vector.x,
        segment_start.y + t * segment_vector.y,
        segment_start.z + t * segment_vector.z};

    return projected_point;
}

static vec3 calculate_normal(vec3 segment_vector, f32 rotation_y) {
    vec3 normalized_segment = vec3_normalized(segment_vector);

    vec3 raw_normal = {
        -normalized_segment.z,
        0.0f,
        normalized_segment.x};

    // Rotate the normal around the y axis.
    f32 cos_theta = kcos(rotation_y);
    f32 sin_theta = ksin(rotation_y);

    vec3 rotated_normal = {
        raw_normal.x * cos_theta - raw_normal.z * sin_theta,
        raw_normal.y,
        raw_normal.x * sin_theta + raw_normal.z * cos_theta};

    return rotated_normal;
}

vec3 track_constrain_object(vec3 object_position, track* t) {
    vec3 constrained_pos = object_position;

    f32 closest_dist_sq = 999999999.0f;

    u32 point_count = darray_length(t->points);
    for (u32 i = 0; i < point_count - 1; ++i) {
        vec3 segment_start = t->points[i].position;
        vec3 segment_end = t->points[i + 1].position;

        f32 start_width_left = t->points[i].left_width;
        f32 start_width_right = t->points[i].right_width;
        f32 end_width_left = t->points[i + 1].left_width;
        f32 end_width_right = t->points[i + 1].right_width;

        f32 start_height_left = t->points[i].position.y + t->points[i].left_height;
        f32 start_height_right = t->points[i].position.y + t->points[i].right_height;
        f32 start_height_center = t->points[i].position.y;
        f32 start_rotation_y = t->points[i].rotation_y;

        f32 end_height_left = t->points[i + 1].position.y + t->points[i + 1].left_height;
        f32 end_height_right = t->points[i + 1].position.y + t->points[i + 1].right_height;
        f32 end_height_center = t->points[i + 1].position.y;
        f32 end_rotation_y = t->points[i + 1].rotation_y;

        // Project the point onto the current segment.
        vec3 closest_point = project_point_onto_segment(object_position, segment_start, segment_end);

        f32 interpolated_rotation_y = compute_interpolated_width(closest_point, segment_start, segment_end, start_rotation_y, end_rotation_y);

        // Interpolate widths and heights at the closest point.
        float width_left_at_closest = compute_interpolated_width(closest_point, segment_start, segment_end, start_width_left, end_width_left);
        float width_right_at_closest = compute_interpolated_width(closest_point, segment_start, segment_end, start_width_right, end_width_right);
        float height_left_at_closest = compute_interpolated_width(closest_point, segment_start, segment_end, start_height_left, end_height_left);
        float height_right_at_closest = compute_interpolated_width(closest_point, segment_start, segment_end, start_height_right, end_height_right);
        float height_center_at_closest = compute_interpolated_width(closest_point, segment_start, segment_end, start_height_center, end_height_center);

        // Calculate the distance to the centerline and normal direction.
        vec3 segment_vector = vec3_sub(segment_end, segment_start);
        vec3 normal = calculate_normal(segment_vector, interpolated_rotation_y);
        vec3 to_point = vec3_sub(object_position, closest_point);

        f32 distance_to_center = vec3_dot(to_point, normal);

        // Clamp horizontally
        if (distance_to_center < -width_left_at_closest) {
            // Outside on the left side
            closest_point = vec3_add(closest_point, vec3_mul_scalar(normal, -width_left_at_closest - distance_to_center));
        } else if (distance_to_center > width_right_at_closest) {
            // Outside on the right side
            closest_point = vec3_add(closest_point, vec3_mul_scalar(normal, -width_right_at_closest - distance_to_center));
        }

        // Adjust for height based on horizontal position.
        f32 t = (distance_to_center + width_left_at_closest) / (width_left_at_closest + width_right_at_closest);
        f32 height_at_closest = klerp(height_left_at_closest, height_right_at_closest, t);

        // Interpolate the center height as well
        f32 height_difference = height_at_closest - height_center_at_closest;
        closest_point.y = height_center_at_closest + height_difference;

        // Update constrained position if the segment is closer.
        f32 dist_sq = vec3_dot(to_point, to_point);
        if (dist_sq < closest_dist_sq) {
            closest_dist_sq = dist_sq;
            constrained_pos = closest_point;
        }
    }

    return constrained_pos;
}
