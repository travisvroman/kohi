#include "geometry_3d.h"

#include "math/kmath.h"

ray ray_create(vec3 position, vec3 direction) {
    ray r = {0};
    r.origin = position;
    r.direction = direction;
    return r;
}

ray ray_from_screen(vec2 screen_pos, vec2 viewport_size, vec3 origin, mat4 view, mat4 projection) {
    ray r = {0};
    r.origin = origin;

    // Get normalized device coordinates (i.e. -1:1 range).
    vec3 ray_ndc;
    ray_ndc.x = (2.0f * screen_pos.x) / viewport_size.x - 1.0f;
    ray_ndc.y = 1.0f - (2.0f * screen_pos.y) / viewport_size.y;
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
