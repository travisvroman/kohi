#include "kmath.h"

#include <math.h>
#include <stdlib.h>

#include "math/math_types.h"
#include "platform/platform.h"

static b8 rand_seeded = false;

/**
 * Note that these are here in order to prevent having to import the
 * entire <math.h> everywhere.
 */
f32 ksin(f32 x) { return sinf(x); }

f32 kcos(f32 x) { return cosf(x); }

f32 ktan(f32 x) { return tanf(x); }

f32 katan(f32 x) { return atanf(x); }

f32 kacos(f32 x) { return acosf(x); }

f32 ksqrt(f32 x) { return sqrtf(x); }

f32 kabs(f32 x) { return fabsf(x); }

f32 kfloor(f32 x) { return floorf(x); }

f32 kceil(f32 x) { return ceilf(x); }

f32 klog2(f32 x) { return log2f(x); }

f32 kpow(f32 x, f32 y) { return powf(x, y); }

i32 krandom(void) {
    if (!rand_seeded) {
        srand((u32)platform_get_absolute_time());
        rand_seeded = true;
    }
    return rand();
}

i32 krandom_in_range(i32 min, i32 max) {
    if (!rand_seeded) {
        srand((u32)platform_get_absolute_time());
        rand_seeded = true;
    }
    return (rand() % (max - min + 1)) + min;
}

f32 kfrandom(void) { return (float)krandom() / (f32)RAND_MAX; }

f32 kfrandom_in_range(f32 min, f32 max) {
    return min + ((float)krandom() / ((f32)RAND_MAX / (max - min)));
}

f32 kattenuation_min_max(f32 min, f32 max, f32 x) {
    // TODO: Maybe a good function here would be one with a min/max and falloff value...
    // so if the range was 0.4 to 0.8 with a falloff of 1.0, weight for x between
    // 0.5 and 0.7 would be 1.0, with it dwindling to 0 as it approaches 0.4 or 0.8.
    f32 half_range = kabs(max - min) * 0.5;
    f32 mid = min + half_range;    // midpoint
    f32 distance = kabs(x - mid);  // dist from mid
    // scale dist from midpoint to halfrange
    f32 att = KCLAMP((half_range - distance) / half_range, 0, 1);
    return att;
}

plane_3d plane_3d_create(vec3 p1, vec3 norm) {
    plane_3d p;
    p.normal = vec3_normalized(norm);
    p.distance = vec3_dot(p.normal, p1);
    return p;
}

frustum frustum_from_view_projection(mat4 view_projection) {
    frustum f;

    // Get the inverse of the view_projection matrix.
    mat4 inv = mat4_inverse(view_projection);
    f32 *md = inv.data;

    // Extract the rows.
    vec4 mat0 = {md[0], md[1], md[2], md[3]};
    vec4 mat1 = {md[4], md[5], md[6], md[7]};
    vec4 mat2 = {md[8], md[9], md[10], md[11]};
    vec4 mat3 = {md[12], md[13], md[14], md[15]};

    // Calculate the projection planes and normalize them, including distances.
    vec4 sides[6];
    sides[FRUSTUM_SIDE_LEFT] = vec4_normalized(vec4_add(mat3, mat0));
    sides[FRUSTUM_SIDE_RIGHT] = vec4_normalized(vec4_sub(mat3, mat0));
    sides[FRUSTUM_SIDE_TOP] = vec4_normalized(vec4_sub(mat3, mat1));
    sides[FRUSTUM_SIDE_BOTTOM] = vec4_normalized(vec4_add(mat3, mat1));
    sides[FRUSTUM_SIDE_NEAR] = vec4_normalized(vec4_add(mat3, mat2));
    sides[FRUSTUM_SIDE_FAR] = vec4_normalized(vec4_sub(mat3, mat2));

    // Extract normals and distances to planes.
    for (u32 i = 0; i < 6; ++i) {
        f.sides[i].normal = vec3_from_vec4(sides[i]);
        f.sides[i].distance = sides[i].w;
    }

    return f;
}

frustum frustum_create(const vec3 *position, const vec3 *forward,
                       const vec3 *right, const vec3 *up, f32 aspect, f32 fov,
                       f32 near, f32 far) {
    frustum f;

    f32 half_v = far * tanf(fov * 0.5f);
    f32 half_h = half_v * aspect;
    vec3 fwd = *forward;
    vec3 forward_far = vec3_mul_scalar(fwd, far);
    vec3 right_half_h = vec3_mul_scalar(*right, half_h);
    vec3 up_half_v = vec3_mul_scalar(*up, half_v);

    // Top, bottom, right, left, far, near
    f.sides[FRUSTUM_SIDE_TOP] = plane_3d_create(vec3_add(vec3_mul_scalar(fwd, near), *position), fwd);
    f.sides[FRUSTUM_SIDE_BOTTOM] = plane_3d_create(vec3_add(*position, forward_far), vec3_mul_scalar(fwd, -1.0f));
    f.sides[FRUSTUM_SIDE_RIGHT] = plane_3d_create(*position, vec3_cross(*up, vec3_add(forward_far, right_half_h)));
    f.sides[FRUSTUM_SIDE_LEFT] = plane_3d_create(*position, vec3_cross(vec3_sub(forward_far, right_half_h), *up));
    f.sides[FRUSTUM_SIDE_FAR] = plane_3d_create(*position, vec3_cross(*right, vec3_sub(forward_far, up_half_v)));
    f.sides[FRUSTUM_SIDE_NEAR] = plane_3d_create(*position, vec3_cross(vec3_add(forward_far, up_half_v), *right));

    return f;
}

f32 plane_signed_distance(const plane_3d *p, const vec3 *position) {
    return vec3_dot(p->normal, *position) - p->distance;
}

b8 plane_intersects_sphere(const plane_3d *p, const vec3 *center, f32 radius) {
    return plane_signed_distance(p, center) > -radius;
}

b8 frustum_intersects_sphere(const frustum *f, const vec3 *center, f32 radius) {
    for (u8 i = 0; i < 6; ++i) {
        if (!plane_intersects_sphere(&f->sides[i], center, radius)) {
            return false;
        }
    }
    return true;
}

b8 plane_intersects_aabb(const plane_3d *p, const vec3 *center, const vec3 *extents) {
    f32 r = extents->x * kabs(p->normal.x) +
            extents->y * kabs(p->normal.y) +
            extents->z * kabs(p->normal.z);

    return -r <= plane_signed_distance(p, center);
}

b8 frustum_intersects_aabb(const frustum *f, const vec3 *center, const vec3 *extents) {
    for (u8 i = 0; i < 6; ++i) {
        if (!plane_intersects_aabb(&f->sides[i], center, extents)) {
            return false;
        }
    }
    return true;
}

void frustum_corner_points_world_space(mat4 projection_view, vec4 *corners) {
    mat4 inverse_view_proj = mat4_inverse(projection_view);

    corners[0] = (vec4){-1.0f, -1.0f, 0.0f, 1.0f};
    corners[1] = (vec4){1.0f, -1.0f, 0.0f, 1.0f};
    corners[2] = (vec4){1.0f, 1.0f, 0.0f, 1.0f};
    corners[3] = (vec4){-1.0f, 1.0f, 0.0f, 1.0f};

    corners[4] = (vec4){-1.0f, -1.0f, 1.0f, 1.0f};
    corners[5] = (vec4){1.0f, -1.0f, 1.0f, 1.0f};
    corners[6] = (vec4){1.0f, 1.0f, 1.0f, 1.0f};
    corners[7] = (vec4){-1.0f, 1.0f, 1.0f, 1.0f};

    for (u32 i = 0; i < 8; ++i) {
        vec4 point = mat4_mul_vec4(inverse_view_proj, corners[i]);
        corners[i] = vec4_div_scalar(point, point.w);
    }
}
