#include "geometry_3d.h"

#include "defines.h"
#include "math/kmath.h"
#include "math/math_types.h"

ray ray_create(vec3 position, vec3 direction) {
    ray r = {0};
    r.origin = position;
    r.direction = direction;
    return r;
}

ray ray_from_screen(vec2 screen_pos, rect_2d viewport_rect, vec3 origin, mat4 view, mat4 projection) {
    ray r = {0};
    r.origin = origin;

    // Get normalized device coordinates (i.e. -1:1 range).
    vec3 ray_ndc;
    ray_ndc.x = (2.0f * (screen_pos.x - viewport_rect.x)) / viewport_rect.width - 1.0f;
    ray_ndc.y = 1.0f - (2.0f * (screen_pos.y - viewport_rect.y)) / viewport_rect.height;
    ray_ndc.z = 1.0f;

    // Clip space
    vec4 ray_clip = vec4_create(ray_ndc.x, ray_ndc.y, -1.0f, 1.0f);

    // Eye/Camera
    vec4 ray_eye = mat4_mul_vec4(mat4_inverse(projection), ray_clip);

    // Unproject xy, change wz to "forward".
    ray_eye = vec4_create(ray_eye.x, ray_eye.y, -1.0f, 0.0f);

    // Convert to world coordinates;
    r.direction = vec3_from_vec4(mat4_mul_vec4(view, ray_eye));
    vec3_normalize(&r.direction);

    return r;
}

b8 raycast_aabb(extents_3d bb_extents, const ray* r, vec3* out_point) {
    // Based on Graphics Gems Fast Ray-Box Intersection implementation.
    b8 inside = true;
    i8 quadrant[3];
    vec3 max_t;
    vec3 candidate_plane;

    for (u32 i = 0; i < 3; ++i) {
        if (r->origin.elements[i] < bb_extents.min.elements[i]) {
            quadrant[i] = 1;  // left
            candidate_plane.elements[i] = bb_extents.min.elements[i];
            inside = false;
        } else if (r->origin.elements[i] > bb_extents.max.elements[i]) {
            quadrant[i] = 0;  // right
            candidate_plane.elements[i] = bb_extents.max.elements[i];
            inside = false;
        } else {
            quadrant[i] = 2;  // middle
        }
    }

    // Ray origin inside bounding box.
    if (inside) {
        *out_point = r->origin;
        return true;
    }

    // Calculate distances to candidate planes.
    for (u32 i = 0; i < 3; ++i) {
        if (quadrant[i] != 2 && r->direction.elements[i] != 0.0f) {
            max_t.elements[i] = (candidate_plane.elements[i] - r->origin.elements[i]) / r->direction.elements[i];
        } else {
            max_t.elements[i] = -1.0f;
        }
    }

    // Get largest of the max_ts for final choice of intersection.
    i32 which_plane = 0;
    for (u32 i = 1; i < 3; ++i) {
        if (max_t.elements[which_plane] < max_t.elements[i]) {
            which_plane = i;
        }
    }

    // Check final candidate actually inside box.
    if (max_t.elements[which_plane] < 0.0f) {
        return false;
    }
    for (i32 i = 0; i < 3; ++i) {
        if (which_plane != i) {
            out_point->elements[i] = r->origin.elements[i] + max_t.elements[which_plane] * r->direction.elements[i];
            if (out_point->elements[i] < bb_extents.min.elements[i] || out_point->elements[i] > bb_extents.max.elements[i]) {
                return false;
            }
        } else {
            out_point->elements[i] = candidate_plane.elements[i];
        }
    }

    // Hits box.
    return true;
}

b8 raycast_oriented_extents(extents_3d bb_extents, mat4 model, const ray* r, f32* out_dist) {
    mat4 inv = mat4_inverse(model);

    // Transform the ray to AABB space.
    ray transformed_ray;
    transformed_ray.origin = vec3_transform(r->origin, 1.0f, inv);
    transformed_ray.direction = vec3_transform(r->direction, 0.0f, inv);

    vec3 out_point;
    b8 result = raycast_aabb(bb_extents, &transformed_ray, &out_point);

    // If there was a hit, transform the point to oriented space, then
    // calculate the hit distance based on that transformed position versus the
    // original, untransformed array.
    if (result) {
        out_point = vec3_transform(out_point, 1.0f, model);
        *out_dist = vec3_distance(out_point, r->origin);
    }

    return result;
}

b8 raycast_plane_3d(const ray* r, const plane_3d* p, vec3* out_point, f32* out_distance) {
    f32 normal_dir = vec3_dot(r->direction, p->normal);
    f32 point_normal = vec3_dot(r->origin, p->normal);

    // If the ray and plane normal point in the same direction, there can't be a hit.
    if (normal_dir >= 0.0f) {
        return false;
    }

    // Calculate the distance.
    f32 t = (p->distance - point_normal) / normal_dir;

    // Distance must be positive or 0, otherwise the ray hits behind the plane,
    // which technically isn't a hit at all.
    if (t >= 0.0f) {
        *out_distance = t;
        *out_point = vec3_add(r->origin, vec3_mul_scalar(r->direction, t));
        return true;
    }

    return false;
}

b8 raycast_disc_3d(const ray* r, vec3 center, vec3 normal, f32 outer_radius, f32 inner_radius, vec3* out_point, f32* out_distance) {
    if (!r) {
        return false;
    }

    plane_3d p = plane_3d_create(center, normal);
    if (raycast_plane_3d(r, &p, out_point, out_distance)) {
        // Square the radii and compare against squared distance
        f32 orad_sq = outer_radius * outer_radius;
        f32 irad_sq = inner_radius * inner_radius;
        f32 dist_sq = vec3_distance_squared(center, *out_point);
        if (dist_sq > orad_sq) {
            return false;
        }
        if (inner_radius > 0 && dist_sq < irad_sq) {
            return false;
        }
        return true;
    }

    return false;
}
