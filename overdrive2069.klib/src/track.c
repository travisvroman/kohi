#include "track.h"
#include "core/engine.h"
#include "core_physics_types.h"
#include "defines.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "physics/kphysics_system.h"
#include "physics/physics_types.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/material_system.h"

#include <containers/darray.h>

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
}

b8 track_create(track* out_track) {
    if (!out_track) {
        return false;
    }

    // HACK: defining some hardcoded stuff for now - should be configurable.
    const u32 point_count = 11;
    out_track->points = darray_reserve(track_point, point_count);
    darray_length_set(out_track->points, point_count);

    // Position, left_width, left_height, right_width, right_height, rotation_y, left_rail_height, left_rail_width, right_rail_height, right_rail_width
    out_track->points[0] = (track_point){{-10.0f, -0.5f, 0.0f}, 10.0f, 0.0f, 12.0f, 0.25f, deg_to_rad(0.0f), 1.0f, 3.0f, 1.0f, 3.0f};
    out_track->points[1] = (track_point){{10.0f, 2.0f, 00.0f}, 8.0f, 0.25f, 3.0f, 0.5f, deg_to_rad(45.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[2] = (track_point){{50.0f, 5.0f, 100.0f}, 9.0f, -0.5f, 6.0f, 1.0f, deg_to_rad(90.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[3] = (track_point){{75.0f, 6.0f, 200.0f}, 6.0f, 1.0f, 10.0f, 1.5f, deg_to_rad(135.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[4] = (track_point){{20.0f, 6.0f, 230.0f}, 5.0f, 1.0f, 15.0f, 1.5f, deg_to_rad(180.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[5] = (track_point){{-50.0f, 5.0f, 200.0f}, 4.0f, 1.0f, 15.0f, 1.5f, deg_to_rad(270.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[6] = (track_point){{-50.0f, 10.0f, 159.0f}, 4.0f, 1.0f, 15.0f, 1.5f, deg_to_rad(270.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[7] = (track_point){{-50.0f, -1.0f, 158.0f}, 4.0f, 1.0f, 15.0f, 1.5f, deg_to_rad(270.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[8] = (track_point){{-50.0f, 2.0f, 100.0f}, 8.0f, 1.0f, 8.0f, 1.5f, deg_to_rad(270.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[9] = (track_point){{-25.0f, 2.0f, 10.0f}, 8.0f, 1.0f, 8.0f, 1.5f, deg_to_rad(270.0f), 2.0f, 0.0f, 2.0f, 0.0f};
    out_track->points[10] = (track_point){{-10.0f, -0.5f, 0.0f}, 10.0f, 0.0f, 12.0f, 0.25f, deg_to_rad(0.0f), 1.0f, 3.0f, 1.0f, 3.0f}; // last should be the same as the first to loop

    // Number of divisions to make per segment.
    out_track->segment_resolution = 10;

    u32 segment_count = darray_length(out_track->points) - 1;
    out_track->segments = darray_reserve(track_segment, segment_count);
    darray_length_set(out_track->segments, segment_count);

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

        // Index into the vertex array.
        u32 vi = 0;
        // Index into the index array.
        u32 ii = 0;

        track_segment* segment = &trk->segments[i];
        segment->index = i;

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
        segment->geometry.vertex_count = 5 + (5 * trk->segment_resolution);
        segment->geometry.vertices = KALLOC_TYPE_CARRAY(vertex_3d, segment->geometry.vertex_count);
        segment->geometry.index_count = 24 * trk->segment_resolution; // 6 per face, 4 faces per segment Tessellation
        segment->geometry.indices = KALLOC_TYPE_CARRAY(u32, segment->geometry.index_count);

        // Triangle and adjacency data.
        segment->triangle_count = 4 * trk->segment_resolution;
        segment->triangles = KALLOC_TYPE_CARRAY(triangle_with_adjacency, segment->triangle_count);

        vertex_3d* verts = (vertex_3d*)segment->geometry.vertices;
        u32* indices = (u32*)segment->geometry.indices;

        // Get the direction of the current segment.
        /* vec3 segment_direction = vec3_sub(end->position, start->position); */

        vec3 start_direction = {kcos(segment->start->rotation_y), 0.0f, ksin(segment->start->rotation_y)};
        vec3 start_normal = vec3_cross(start_direction, vec3_up());
        vec3 end_direction = {kcos(segment->end->rotation_y), 0.0f, ksin(segment->end->rotation_y)};
        vec3 end_normal = vec3_cross(end_direction, vec3_up());

        // Start: Left side of the track
        vec3 start_left = vec3_add(segment->start->position, vec3_mul_scalar(start_normal, -segment->start->left_width));
        start_left.y += segment->start->left_height;

        // Start: Left railing, which can further extend outward
        vec3 start_left_rail = vec3_add(start_left, vec3_mul_scalar(start_normal, -segment->start->left_rail_width));
        start_left_rail.y = start_left.y + segment->start->left_rail_height;

        // Start: Right side of the track.
        vec3 start_right = vec3_add(segment->start->position, vec3_mul_scalar(start_normal, segment->start->right_width));
        start_right.y += segment->start->right_height;

        // Start: Right railing, which can further extend outward
        vec3 start_right_rail = vec3_add(start_right, vec3_mul_scalar(start_normal, segment->start->right_rail_width));
        start_right_rail.y = start_right.y + segment->start->right_rail_height;

        // End: Left side of the track
        vec3 end_left = vec3_add(segment->end->position, vec3_mul_scalar(end_normal, -segment->end->left_width));
        end_left.y += segment->end->left_height;

        // End: Left railing, which can further extend outward
        vec3 end_left_rail = vec3_add(end_left, vec3_mul_scalar(end_normal, -segment->end->left_rail_width));
        end_left_rail.y = end_left.y + segment->end->left_rail_height;

        // End: Right side of the track
        vec3 end_right = vec3_add(segment->end->position, vec3_mul_scalar(end_normal, segment->end->right_width));
        end_right.y += segment->end->right_height;

        // End: Right railing, which can further extend outward
        vec3 end_right_rail = vec3_add(end_right, vec3_mul_scalar(end_normal, segment->end->right_rail_width));
        end_right_rail.y = end_right.y + segment->end->right_rail_height;

        // Save off the end points for later.
        segment->start->left = start_left;
        segment->start->right = start_right;
        segment->end->left = end_left;
        segment->end->right = end_right;

        // Tessellation
        for (u32 t = 0; t <= trk->segment_resolution; ++t, vi += 5) {
            // How far into the segment this iteration is.
            f32 pct = (f32)t / (f32)trk->segment_resolution;

            // Interpolate center using bezier
            f32 handle_factor = vec3_distance(segment->start->position, segment->end->position);
            handle_factor *= 0.5f; // proportion
            // center
            vec3 center_handle_0 = default_handle(segment->start->position, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 center_handle_1 = default_handle(segment->end->position, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 center = calculate_bezier(segment->start->position, segment->end->position, center_handle_0, center_handle_1, pct);

            // left rail
            handle_factor = vec3_distance(start_left_rail, end_left_rail);
            handle_factor *= 0.5f;                                                                                                 // proportion
            vec3 left_rail_handle_0 = default_handle(start_left_rail, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 left_rail_handle_1 = default_handle(end_left_rail, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 left_rail = calculate_bezier(start_left_rail, end_left_rail, left_rail_handle_0, left_rail_handle_1, pct);

            // left
            handle_factor = vec3_distance(start_left, end_left);
            handle_factor *= 0.5f;                                                                                       // proportion
            vec3 left_handle_0 = default_handle(start_left, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 left_handle_1 = default_handle(end_left, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 left = calculate_bezier(start_left, end_left, left_handle_0, left_handle_1, pct);

            // right rail
            handle_factor = vec3_distance(start_right_rail, end_right_rail);
            handle_factor *= 0.5f;                                                                                                   // proportion
            vec3 right_rail_handle_0 = default_handle(start_right_rail, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 right_rail_handle_1 = default_handle(end_right_rail, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 right_rail = calculate_bezier(start_right_rail, end_right_rail, right_rail_handle_0, right_rail_handle_1, pct);

            // right
            handle_factor = vec3_distance(start_right, end_right);
            handle_factor *= 0.5f;                                                                                         // proportion
            vec3 right_handle_0 = default_handle(start_right, segment->start->rotation_y, handle_factor);                  // forward handle
            vec3 right_handle_1 = default_handle(end_right, segment->end->rotation_y + deg_to_rad(180.0f), handle_factor); // backward handle
            vec3 right = calculate_bezier(start_right, end_right, right_handle_0, right_handle_1, pct);

            // Vertex data.

            // store in the vertex array.
            // Left rail
            verts[vi + 0].position = left_rail;
            verts[vi + 0].normal = (vec3){0, 0, 1};
            verts[vi + 0].texcoord.u = -2.0f;
            verts[vi + 0].texcoord.v = pct;
            // Left
            verts[vi + 1].position = left;
            verts[vi + 1].normal = (vec3){0, 0, 1};
            verts[vi + 1].texcoord.u = -1.0f;
            verts[vi + 1].texcoord.v = pct;
            // Center
            verts[vi + 2].position = center;
            verts[vi + 2].normal = (vec3){0, 0, 1};
            verts[vi + 2].texcoord.u = 0.0f;
            verts[vi + 2].texcoord.v = pct;
            // Right
            verts[vi + 3].position = right;
            verts[vi + 3].normal = (vec3){0, 0, 1};
            verts[vi + 3].texcoord.u = 1.0f;
            verts[vi + 3].texcoord.v = pct;
            // Right rail
            verts[vi + 4].position = right_rail;
            verts[vi + 4].normal = (vec3){0, 0, 1};
            verts[vi + 4].texcoord.u = 2.0f;
            verts[vi + 4].texcoord.v = pct;

            // Generate index data per segment. This looks forward to the next
            // tessellated face. So skip the last iteration. Also generate triangle
            // adjacency data
            if (t < trk->segment_resolution) {

                // left rail, first triangle
                indices[ii + 0] = vi + 0;
                indices[ii + 1] = vi + 1;
                indices[ii + 2] = vi + 5;

                // left rail, second triangle
                indices[ii + 3] = vi + 1;
                indices[ii + 4] = vi + 6;
                indices[ii + 5] = vi + 5;

                // left, first triangle
                indices[ii + 6] = vi + 1;
                indices[ii + 7] = vi + 2;
                indices[ii + 8] = vi + 6;

                // left, second triangle
                indices[ii + 9] = vi + 2;
                indices[ii + 10] = vi + 7;
                indices[ii + 11] = vi + 6;

                // right, first triangle
                indices[ii + 12] = vi + 2;
                indices[ii + 13] = vi + 3;
                indices[ii + 14] = vi + 7;

                // right, second triangle
                indices[ii + 15] = vi + 3;
                indices[ii + 16] = vi + 8;
                indices[ii + 17] = vi + 7;

                // right rail, first triangle
                indices[ii + 18] = vi + 3;
                indices[ii + 19] = vi + 4;
                indices[ii + 20] = vi + 8;

                // right rail, second triangle
                indices[ii + 21] = vi + 4;
                indices[ii + 22] = vi + 9;
                indices[ii + 23] = vi + 8;

                ii += 24;
            }
        }

        // geometry_generate_normals(segment->geometry.vertex_count, verts, segment->geometry.index_count, indices);
        geometry_generate_tangents(segment->geometry.vertex_count, verts, segment->geometry.index_count, indices);
    }

    return true;
}

b8 track_load(track* t, kphysics_world* physics_world) {
    if (!t) {
        return false;
    }

    // Get a material for the track. This is only needed when visualizing the track.
    // HACK: hardcoded material for now. Should be able to vary by segment, perhaps?
    if (!material_system_acquire(engine_systems_get()->material_system, kname_create("testcube_mat"), &t->material)) {
        KERROR("Failed to load material!");
    }

    t->pjysics_world = physics_world;

    // Upload the geometry to the GPU. This is only needed to visualize the track.
    u32 segment_count = darray_length(t->segments);
    for (u32 i = 0; i < segment_count; ++i) {
        track_segment* segment = &t->segments[i];
        if (!renderer_geometry_upload(&segment->geometry)) {
            KERROR("Failed to upload geometry for track segment. Track load failed.");
            return false;
        }

        // Ensure the segment has triangle data loaded for its geometry.
        if (!segment->geometry.tris) {
            if (!geometry_calculate_triangles(&segment->geometry)) {
                KERROR("Triangle generation failed for track segment geometry. See logs for details.");
                return false;
            }
        }

        // Create a static physics body for each segment.
        char* seg_name = string_format("segment_%u", i);
        // Add to physics world (pass in after querying from scene)
        if (!kphysics_body_create_mesh(engine_systems_get()->physics_system, kname_create(seg_name), vec3_zero(), segment->geometry.triangle_count, segment->geometry.tris, KPHYSICS_BODY_TYPE_STATIC, &segment->physics_body)) {
            KERROR("Failed to create mesh body for physics body attachment. See logs for details.");
            continue;
        }
        string_free(seg_name);

        // Add to physics world.
        if (!kphysics_world_add_body(engine_systems_get()->physics_system, t->pjysics_world, segment->physics_body)) {
            KERROR("Failed to add track segment to physics world. See logs for details.");
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
            track_segment* segment = &t->segments[i];

            kphysics_world_remove_body(engine_systems_get()->physics_system, t->pjysics_world, segment->physics_body);
            // Destroy physics body.
            kphysics_body_destroy(engine_systems_get()->physics_system, &segment->physics_body);

            // Destroy the geometry.
            renderer_geometry_destroy(&segment->geometry);
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

b8 is_point_inside_triangle(vec3 point, const triangle_3d* tri) {
    vec3 edge_0 = vec3_sub(tri->verts[2], tri->verts[0]);
    vec3 edge_1 = vec3_sub(tri->verts[1], tri->verts[0]);
    vec3 v0_to_point = vec3_sub(point, tri->verts[0]);

    f32 dot_0_0 = vec3_dot(edge_0, edge_0);
    f32 dot_0_1 = vec3_dot(edge_0, edge_1);
    f32 dot_0_2 = vec3_dot(edge_0, v0_to_point);
    f32 dot_1_1 = vec3_dot(edge_1, edge_1);
    f32 dot_1_2 = vec3_dot(edge_1, v0_to_point);

    // Calculate barycentric coordinates.
    f32 determinant = (dot_0_0 * dot_1_1 - dot_0_1 * dot_0_1);
    if (kabs(determinant) < 1e-6) {
        KTRACE("degenerate tri");
        return false; // Degenerate triangle.
    }
    f32 inverted_determinant = 1.0f / determinant;

    f32 u = (dot_1_1 * dot_0_2 - dot_0_1 * dot_1_2) * inverted_determinant;
    f32 v = (dot_0_0 * dot_1_2 - dot_0_1 * dot_0_2) * inverted_determinant;

    b8 point_is_inside = (u >= 0.0f) * (v >= 0.0f) && (u + v <= 1.0f);
    return point_is_inside;
}

vec3 get_closest_point_on_edge(vec3 point, vec3 edge_start, vec3 edge_end) {
    vec3 edge = vec3_sub(edge_end, edge_start);
    f32 edge_length_sq = vec3_length_squared(edge);

    if (edge_length_sq == 0.0f) {
        // Degenerate edge, just use the edge's start point.
        return edge_start;
    }

    // Project the point onto the edge, clamping it to within the edge segment as well.
    vec3 point_to_start = vec3_sub(point, edge_start);
    f32 t = vec3_dot(point_to_start, edge) / edge_length_sq;
    t = KCLAMP(t, 0.0f, 1.0f);

    // Interpolate along the edge to find the closest point.
    return vec3_add(edge_start, vec3_mul_scalar(edge, t));
}

vec3 get_closest_point_on_triangle_edges(vec3 point, const triangle_3d* tri) {
    vec3 closest_on_edge[3];
    f32 distances[3];
    for (u8 i = 0; i < 3; ++i) {
        closest_on_edge[i] = get_closest_point_on_edge(point, tri->verts[i], tri->verts[(i + 1) % 3]);
        distances[i] = vec3_distance(point, closest_on_edge[i]);
    }
    if (distances[0] < distances[1] && distances[0] < distances[2]) {
        return closest_on_edge[0];
    } else if (distances[1] < distances[2]) {
        return closest_on_edge[1];
    } else {
        return closest_on_edge[2];
    }
}

triangle_3d find_closest_triangle(vec3 point, const kgeometry* geometry) {
    vertex_3d* verts = geometry->vertices;
    u32* indices = geometry->indices;
    // TODO: use a BVH or something here
    f32 closest_distance = K_FLOAT_MAX;
    triangle_3d closest_triangle;

    for (u32 i = 0; i < geometry->index_count; i += 3) {
        triangle_3d t;
        for (u32 j = 0; j < 3; ++j) {
            t.verts[j] = verts[indices[i + j]].position;
        }

        // Check all 3 points
        for (u32 j = 0; j < 3; ++j) {
            f32 dist_squared = vec3_distance_squared(point, t.verts[j]);
            if (dist_squared < closest_distance) {
                closest_distance = dist_squared;
                closest_triangle = t;
            }
        }
    }

    return closest_triangle;
}

vec3 get_closest_point_on_triangle(vec3 point, const triangle_3d* tri) {
    vec3 p0 = tri->verts[0];
    vec3 p1 = tri->verts[1];
    vec3 p2 = tri->verts[2];

    vec3 closest_0_1 = get_closest_point_on_edge(point, p0, p1);
    vec3 closest_1_2 = get_closest_point_on_edge(point, p1, p2);
    vec3 closest_2_0 = get_closest_point_on_edge(point, p2, p0);

    f32 dist_0 = vec3_distance(point, closest_0_1);
    f32 dist_1 = vec3_distance(point, closest_1_2);
    f32 dist_2 = vec3_distance(point, closest_2_0);

    if (dist_0 < dist_1 && dist_0 < dist_2) {
        return closest_0_1;
    } else if (dist_1 < dist_2) {
        return closest_1_2;
    } else {
        return closest_2_0;
    }
}

triangle_with_adjacency* find_closest_triangle_with_adjacency(vec3 point, u32 count, triangle_with_adjacency* tris) {

    // TODO: use a BVH or something here
    f32 closest_distance = K_FLOAT_MAX;
    triangle_with_adjacency* closest_triangle = 0;

    for (u32 i = 0; i < count; i++) {
        triangle_with_adjacency* current_tri = &tris[i];

        // If the point is inside the triangle, use it.
        if (is_point_inside_triangle(point, &current_tri->tri)) {
            return current_tri;
        }

        // Find the closest point on the triangle's edges or surface.
        vec3 closest_point = get_closest_point_on_triangle(point, &current_tri->tri);

        f32 distance = vec3_distance(point, closest_point);

        if (distance < closest_distance) {
            closest_distance = distance;
            closest_triangle = current_tri;
        }
    }

    // KTRACE("closest tri is idx %u (point=%.2f,%2f,%.2f)", closest_triangle.index, point.x, point.y, point.z);

    return closest_triangle;
}

// returns nonzero if a segment change is needed.
i32 constrain_to_track_segment(vec3 point, vec3 velocity, track* trk, track_segment* segment, vec3* out_position, vec3* out_surface_normal) {

    // Closest triangle
    triangle_with_adjacency* closest_triangle = find_closest_triangle_with_adjacency(point, segment->triangle_count, segment->triangles);
    if (!closest_triangle) {
        return 1;
    }

    // Project the position onto the triangle's plane
    vec3 normal = vec3_normalized(vec3_cross(
        vec3_sub(closest_triangle->tri.verts[1], closest_triangle->tri.verts[0]),
        vec3_sub(closest_triangle->tri.verts[2], closest_triangle->tri.verts[0])));
    vec3 projected_position = vec3_sub(
        point,
        vec3_mul_scalar(
            normal,
            vec3_dot(
                vec3_sub(point, closest_triangle->tri.verts[0]),
                normal)));

    // Get and report the triangle's surface normal.
    *out_surface_normal = triangle_3d_get_normal(&closest_triangle->tri);

    // Check if inside the triangle.
    if (is_point_inside_triangle(projected_position, &closest_triangle->tri)) {
        // Constrain velocity to plane
        vec3 velocity_on_plane = vec3_sub(velocity, vec3_mul_scalar(normal, vec3_dot(velocity, normal)));
        KTRACE("point inside, applying velocity (tri idx=%u)", closest_triangle->index);
        *out_position = vec3_add(projected_position, velocity_on_plane);
        return 0;
    } else {

        // Get the the closest edge.
        f32 closest_edge_dist = K_FLOAT_MAX;
        vec3 closest_edge_start;
        vec3 closest_edge_end;
        vec3 closest_point_on_edge;
        for (u8 i = 0; i < 3; ++i) {
            vec3 edge_start = closest_triangle->tri.verts[i];
            vec3 edge_end = closest_triangle->tri.verts[(i + 1) % 3];
            vec3 closest_on_edge = get_closest_point_on_edge(point, edge_start, edge_end);
            f32 dist = vec3_distance_squared(closest_on_edge, projected_position);
            if (dist < closest_edge_dist) {
                closest_edge_dist = dist;
                closest_edge_start = edge_start;
                closest_edge_end = edge_end;
                closest_point_on_edge = closest_on_edge;
            }
        }

        // If the closest edge shares two vertices with the end (either on the left or
        // the right), then need to transition to the next segment. 2 is done so that the
        // the transition only happens when on the triangle with a full side is done.

        u32 shared_start_points = 0;
        shared_start_points += vec3_compare(closest_edge_start, segment->start->left, K_FLOAT_EPSILON);
        shared_start_points += vec3_compare(closest_edge_start, segment->start->position, K_FLOAT_EPSILON);
        shared_start_points += vec3_compare(closest_edge_start, segment->start->right, K_FLOAT_EPSILON);
        shared_start_points += vec3_compare(closest_edge_end, segment->start->left, K_FLOAT_EPSILON);
        shared_start_points += vec3_compare(closest_edge_end, segment->start->position, K_FLOAT_EPSILON);
        shared_start_points += vec3_compare(closest_edge_end, segment->start->right, K_FLOAT_EPSILON);

        u32 shared_end_points = 0;
        shared_end_points += vec3_compare(closest_edge_start, segment->end->left, K_FLOAT_EPSILON);
        shared_end_points += vec3_compare(closest_edge_start, segment->end->position, K_FLOAT_EPSILON);
        shared_end_points += vec3_compare(closest_edge_start, segment->end->right, K_FLOAT_EPSILON);
        shared_end_points += vec3_compare(closest_edge_end, segment->end->left, K_FLOAT_EPSILON);
        shared_end_points += vec3_compare(closest_edge_end, segment->end->position, K_FLOAT_EPSILON);
        shared_end_points += vec3_compare(closest_edge_end, segment->end->right, K_FLOAT_EPSILON);

        // FIXME: This leaks somehow when hugging the border during a transition. Suspect an additional
        // check of some sort might be required to see if within a certain radius of the left/right most
        // points, and somehow force along the track instead of allowing to move forward and outside the track.
        if (shared_start_points >= 2) {
            KTRACE("transition previous.");
            *out_position = point;
            return -1; // Move to the previous segment.
        } else if (shared_end_points >= 2) {
            KTRACE("transition next.");
            *out_position = point;
            return 1; // Move to the next segment.
        }

        KTRACE("sliding along edge.");

        // Project the velocity onto the edge
        vec3 edge = vec3_normalized(vec3_sub(closest_edge_end, closest_edge_start));

        f32 projected = vec3_dot(velocity, edge);

        vec3 edge_offset = vec3_sub(closest_point_on_edge, projected_position);
        f32 d = vec3_dot(edge_offset, velocity);

        // If approaching the edge, snap to it. Otherwise allow it to pull away.
        if (d < 0.0f) {
            velocity = vec3_mul_scalar(edge, projected);
        }
        *out_position = vec3_add(projected_position, velocity);

        return 0;
    }
}

vec3 constrain_to_track(vec3 vehicle_point, vec3 velocity, track* t, vec3* out_surface_normal) {
    // TODO: get nearest segment

    vec3 out_position = vec3_zero();

    // TODO: maintain this elsewhere.
    static i32 segment_index = 0;
    track_segment* segment = &t->segments[segment_index];
    i32 point_count = (i32)darray_length(t->points);
    i32 segment_count = point_count - 1;
    i32 iterated = 0;
    while (segment) {
        i32 segment_change = constrain_to_track_segment(vehicle_point, velocity, t, segment, &out_position, out_surface_normal);
        if (!segment_change) {
            // Done
            break;
        } else {
            KTRACE("Not contained in segment %u. Segment change needed", segment_index);
            segment_index += segment_change;
            if (segment_index < 0) {
                segment_index = segment_count - 1;
            } else {
                segment_index %= segment_count;
            }

            segment = &t->segments[segment_index];
        }
        iterated++;

        if (iterated > segment_count) {
            KWARN("No segments found that contain the point. Giving up.");
            break;
        }
    }

    return out_position;
}
