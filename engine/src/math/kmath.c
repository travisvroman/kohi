#include "kmath.h"

#include <math.h>
#include <stdlib.h>

#include "platform/platform.h"

static b8 rand_seeded = false;

/**
 * Note that these are here in order to prevent having to import the
 * entire <math.h> everywhere.
 */
f32 ksin(f32 x) { return sinf(x); }

f32 kcos(f32 x) { return cosf(x); }

f32 ktan(f32 x) { return tanf(x); }

f32 kacos(f32 x) { return acosf(x); }

f32 ksqrt(f32 x) { return sqrtf(x); }

f32 kabs(f32 x) { return fabsf(x); }

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

plane_3d plane_3d_create(vec3 p1, vec3 norm) {
    plane_3d p;
    p.normal = vec3_normalized(norm);
    p.distance = vec3_dot(p.normal, p1);
    return p;
}

frustum frustom_create(const vec3 *position, const vec3 *forward,
                       const vec3 *right, const vec3 *up, f32 aspect, f32 fov,
                       f32 near, f32 far) {
    frustum f;

    f32 half_v = far * tanf(fov * 0.5f);
    f32 half_h = half_v * aspect;
    vec3 fwd = *forward;
    vec3 forward_far = vec3_mul_scalar(fwd, far);

    // Top, bottom, right, left, far, near
    f.sides[0] =
        plane_3d_create(vec3_add(vec3_mul_scalar(fwd, near), *position), fwd);
    f.sides[1] = plane_3d_create(vec3_add(*position, forward_far),
                                 vec3_mul_scalar(fwd, -1.0f));
    f.sides[2] = plane_3d_create(
        *position,
        vec3_cross(*up, vec3_add(forward_far, vec3_mul_scalar(*right, half_h))));
    f.sides[3] = plane_3d_create(
        *position,
        vec3_cross(vec3_sub(forward_far, vec3_mul_scalar(*right, half_h)), *up));
    f.sides[4] = plane_3d_create(
        *position,
        vec3_cross(*right, vec3_sub(forward_far, vec3_mul_scalar(*up, half_v))));
    f.sides[5] = plane_3d_create(
        *position,
        vec3_cross(vec3_add(forward_far, vec3_mul_scalar(*up, half_v)), *right));

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

b8 plane_intersects_aabb(const plane_3d *p, const vec3 *center,
                         const vec3 *extents) {
    f32 r = extents->x * kabs(p->normal.x) + extents->y * kabs(p->normal.y) +
            extents->z * kabs(p->normal.z);

    return -r <= plane_signed_distance(p, center);
}

b8 frustum_intersects_aabb(const frustum *f, const vec3 *center,
                           const vec3 *extents) {
    for (u8 i = 0; i < 6; ++i) {
        if (!plane_intersects_aabb(&f->sides[i], center, extents)) {
            return false;
        }
    }
    return true;
}
