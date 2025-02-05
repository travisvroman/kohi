#include <math.h>
#include <stdlib.h>

#include <immintrin.h>

#include "kmath.h"

#include "math/math_types.h"
#include "math/mtwister.h" // for 64-bit RNG

static b8 rand_seeded = false;
static mtrand_state rng_u64 = {0}; // State for unsigned 64-bit RNG

static void seed_randoms(void);

// ------------------------------------------
// General math functions
// ------------------------------------------

/**
 * Note that these are here in order to prevent having to import the
 * entire <math.h> everywhere.
 */
f32 ksin(f32 x) { return sinf(x); }

f32 kcos(f32 x) { return cosf(x); }

f32 ktan(f32 x) { return tanf(x); }

f32 katan(f32 x) { return atanf(x); }

f32 katan2(f32 x, f32 y) { return atan2(x, y); }

f32 kasin(f32 x) { return asinf(x); }

f32 kacos(f32 x) { return acosf(x); }

f32 ksqrt(f32 x) { return sqrtf(x); }

f32 kabs(f32 x) { return fabsf(x); }

f32 kfloor(f32 x) { return floorf(x); }

f32 kceil(f32 x) { return ceilf(x); }

f32 klog(f32 x) { return logf(x); }

f32 klog2(f32 x) { return log2f(x); }

f32 kpow(f32 x, f32 y) { return powf(x, y); }

i32 krandom(void) {
    if (!rand_seeded) {
        seed_randoms();
    }
    return rand();
}

i32 krandom_in_range(i32 min, i32 max) {
    if (!rand_seeded) {
        seed_randoms();
    }
    return (rand() % (max - min + 1)) + min;
}

u64 krandom_u64(void) {
    if (!rand_seeded) {
        seed_randoms();
    }
    return mtrand_generate(&rng_u64);
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
    f32 mid = min + half_range;   // midpoint
    f32 distance = kabs(x - mid); // dist from mid
    // scale dist from midpoint to halfrange
    f32 att = KCLAMP((half_range - distance) / half_range, 0, 1);
    return att;
}

// ------------------------------------------
// Vector 2
// ------------------------------------------
f32 vec2_length(vec2 vector) {
    return ksqrt(vec2_length_squared(vector));
}

void vec2_normalize(vec2* vector) {
    const f32 length = vec2_length(*vector);
    vector->x /= length;
    vector->y /= length;
}

vec2 vec2_normalized(vec2 vector) {
    vec2_normalize(&vector);
    return vector;
}

b8 vec2_compare(vec2 vector_0, vec2 vector_1, f32 tolerance) {
    if (kabs(vector_0.x - vector_1.x) > tolerance) {
        return false;
    }

    if (kabs(vector_0.y - vector_1.y) > tolerance) {
        return false;
    }

    return true;
}

f32 vec2_distance(vec2 vector_0, vec2 vector_1) {
    vec2 d = (vec2){vector_0.x - vector_1.x, vector_0.y - vector_1.y};
    return vec2_length(d);
}

f32 vec2_distance_squared(vec2 vector_0, vec2 vector_1) {
    vec2 d = (vec2){vector_0.x - vector_1.x, vector_0.y - vector_1.y};
    return vec2_length_squared(d);
}

// ------------------------------------------
// Vector 3
// ------------------------------------------
f32 vec3_length(vec3 vector) {
    return ksqrt(vec3_length_squared(vector));
}

void vec3_normalize(vec3* vector) {
    const f32 length = vec3_length(*vector);
    vector->x /= length;
    vector->y /= length;
    vector->z /= length;
}

vec3 vec3_normalized(vec3 vector) {
    vec3_normalize(&vector);
    return vector;
}

f32 vec3_dot(vec3 vector_0, vec3 vector_1) {
    f32 p = 0;
    p += vector_0.x * vector_1.x;
    p += vector_0.y * vector_1.y;
    p += vector_0.z * vector_1.z;
    return p;
}

vec3 vec3_cross(vec3 vector_0, vec3 vector_1) {
    return (vec3){vector_0.y * vector_1.z - vector_0.z * vector_1.y,
                  vector_0.z * vector_1.x - vector_0.x * vector_1.z,
                  vector_0.x * vector_1.y - vector_0.y * vector_1.x};
}

b8 vec3_compare(vec3 vector_0, vec3 vector_1, f32 tolerance) {
    if (kabs(vector_0.x - vector_1.x) > tolerance) {
        return false;
    }

    if (kabs(vector_0.y - vector_1.y) > tolerance) {
        return false;
    }

    if (kabs(vector_0.z - vector_1.z) > tolerance) {
        return false;
    }

    return true;
}

f32 vec3_distance(vec3 vector_0, vec3 vector_1) {
    vec3 d = (vec3){
        vector_0.x - vector_1.x,
        vector_0.y - vector_1.y,
        vector_0.z - vector_1.z};
    return vec3_length(d);
}

f32 vec3_distance_squared(vec3 vector_0, vec3 vector_1) {
    vec3 d = (vec3){vector_0.x - vector_1.x, vector_0.y - vector_1.y,
                    vector_0.z - vector_1.z};
    return vec3_length_squared(d);
}

vec3 vec3_project(vec3 v_0, vec3 v_1) {
    f32 length_sq = vec3_length_squared(v_1);
    if (length_sq == 0.0f) {
        // NOTE: handle divide-by-zero case (i.e. v_1 is a zero vector).
        return vec3_zero();
    }
    f32 scalar = vec3_dot(v_0, v_1) / length_sq;
    return vec3_mul_scalar(v_1, scalar);
}

void vec3_project_points_onto_axis(vec3* points, u32 count, vec3 axis, f32* out_min, f32* out_max) {
    *out_min = *out_max = vec3_dot(points[0], axis);
    for (u32 i = 1; i < count; ++i) {
        f32 projection = vec3_dot(points[i], axis);
        if (projection < *out_min) {
            *out_min = projection;
        }
        if (projection > *out_max) {
            *out_max = projection;
        }
    }
}

vec3 vec3_reflect(vec3 v, vec3 normal) {
    normal = vec3_normalized(normal);

    // Dot product of v and normal.
    f32 dot = vec3_dot(v, normal);

    // Get reflection using r = v - 2(v·n)n
    return (vec3){
        v.x - 2.0f * dot * normal.x,
        v.y - 2.0f * dot * normal.y,
        v.z - 2.0f * dot * normal.z,
    };
}

vec3 vec3_transform(vec3 v, f32 w, mat4 m) {
    vec3 out;
    out.x = v.x * m.data[0 + 0] + v.y * m.data[4 + 0] + v.z * m.data[8 + 0] + w * m.data[12 + 0];
    out.y = v.x * m.data[0 + 1] + v.y * m.data[4 + 1] + v.z * m.data[8 + 1] + w * m.data[12 + 1];
    out.z = v.x * m.data[0 + 2] + v.y * m.data[4 + 2] + v.z * m.data[8 + 2] + w * m.data[12 + 2];
    return out;
}

f32 vec3_distance_to_line(vec3 point, vec3 line_start, vec3 line_direction) {
    f32 magnitude = vec3_length(vec3_cross(vec3_sub(point, line_start), line_direction));
    return magnitude / vec3_length(line_direction);
}

vec3 vec3_min(vec3 vector_0, vec3 vector_1) {
    return vec3_create(
        KMIN(vector_0.x, vector_1.y),
        KMIN(vector_0.y, vector_1.y),
        KMIN(vector_0.z, vector_1.z));
}

vec3 vec3_max(vec3 vector_0, vec3 vector_1) {
    return vec3_create(
        KMAX(vector_0.x, vector_1.y),
        KMAX(vector_0.y, vector_1.y),
        KMAX(vector_0.z, vector_1.z));
}

vec3 vec3_sign(vec3 v) {
    return vec3_create(ksign(v.x), ksign(v.y), ksign(v.z));
}

vec3 vec3_rotate(vec3 v, quat q) {
    vec3 u = vec3_create(q.x, q.y, q.z);
    f32 s = q.w;

    return vec3_add(
        vec3_add(
            vec3_mul_scalar(u, 2.0f * vec3_dot(u, v)),
            vec3_mul_scalar(v, (s * s - vec3_dot(u, u)))),
        vec3_mul_scalar(vec3_cross(u, v), 2.0f * s));
}

// ------------------------------------------
// Vector 4
// ------------------------------------------
vec4 vec4_create(f32 x, f32 y, f32 z, f32 w) {
    vec4 out_vector;
#if defined(KUSE_SIMD)
    out_vector.data = _mm_setr_ps(x, y, z, w);
#else
    out_vector.x = x;
    out_vector.y = y;
    out_vector.z = z;
    out_vector.w = w;
#endif
    return out_vector;
}

vec3 vec4_to_vec3(vec4 vector) {
    return (vec3){vector.x, vector.y, vector.z};
}

vec4 vec4_from_vec3(vec3 vector, f32 w) {
#if defined(KUSE_SIMD)
    vec4 out_vector;
    out_vector.data = _mm_setr_ps(x, y, z, w);
    return out_vector;
#else
    return (vec4){vector.x, vector.y, vector.z, w};
#endif
}

vec4 vec4_add(vec4 vector_0, vec4 vector_1) {
    vec4 result;
    for (u64 i = 0; i < 4; ++i) {
        result.elements[i] = vector_0.elements[i] + vector_1.elements[i];
    }
    return result;
}

vec4 vec4_sub(vec4 vector_0, vec4 vector_1) {
    vec4 result;
    for (u64 i = 0; i < 4; ++i) {
        result.elements[i] = vector_0.elements[i] - vector_1.elements[i];
    }
    return result;
}

vec4 vec4_mul(vec4 vector_0, vec4 vector_1) {
    vec4 result;
    for (u64 i = 0; i < 4; ++i) {
        result.elements[i] = vector_0.elements[i] * vector_1.elements[i];
    }
    return result;
}

vec4 vec4_mul_scalar(vec4 vector_0, f32 scalar) {
    return (vec4){vector_0.x * scalar, vector_0.y * scalar, vector_0.z * scalar, vector_0.w * scalar};
}

vec4 vec4_mul_add(vec4 vector_0, vec4 vector_1, vec4 vector_2) {
    return (vec4){
        vector_0.x * vector_1.x + vector_2.x,
        vector_0.y * vector_1.y + vector_2.y,
        vector_0.z * vector_1.z + vector_2.z,
        vector_0.w * vector_1.w + vector_2.w,
    };
}

vec4 vec4_div(vec4 vector_0, vec4 vector_1) {
    vec4 result;
    for (u64 i = 0; i < 4; ++i) {
        result.elements[i] = vector_0.elements[i] / vector_1.elements[i];
    }
    return result;
}

vec4 vec4_div_scalar(vec4 vector_0, f32 scalar) {
    vec4 result;
    for (u64 i = 0; i < 4; ++i) {
        result.elements[i] = vector_0.elements[i] / scalar;
    }
    return result;
}

f32 vec4_length_squared(vec4 vector) {
    return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z +
           vector.w * vector.w;
}

f32 vec4_length(vec4 vector) {
    return ksqrt(vec4_length_squared(vector));
}

void vec4_normalize(vec4* vector) {
    const f32 length = vec4_length(*vector);
    vector->x /= length;
    vector->y /= length;
    vector->z /= length;
    vector->w /= length;
}

vec4 vec4_normalized(vec4 vector) {
    vec4_normalize(&vector);
    return vector;
}

f32 vec4_dot_f32(f32 a0, f32 a1, f32 a2, f32 a3, f32 b0, f32 b1, f32 b2,
                 f32 b3) {
    f32 p;
    p = a0 * b0 + a1 * b1 + a2 * b2 + a3 * b3;
    return p;
}

b8 vec4_compare(vec4 vector_0, vec4 vector_1, f32 tolerance) {
    if (kabs(vector_0.x - vector_1.x) > tolerance) {
        return false;
    }

    if (kabs(vector_0.y - vector_1.y) > tolerance) {
        return false;
    }

    if (kabs(vector_0.z - vector_1.z) > tolerance) {
        return false;
    }

    if (kabs(vector_0.w - vector_1.w) > tolerance) {
        return false;
    }

    return true;
}

void vec4_clamp(vec4* vector, f32 min, f32 max) {
    if (vector) {
        for (u8 i = 0; i < 4; ++i) {
            vector->elements[i] = KCLAMP(vector->elements[i], min, max);
        }
    }
}

vec4 vec4_clamped(vec4 vector, f32 min, f32 max) {
    vec4_clamp(&vector, min, max);
    return vector;
}

// ------------------------------------------
// Mat4 (4x4 matrix)
// ------------------------------------------

mat4 mat4_identity(void) {
    mat4 out_matrix;
    kzero_memory(out_matrix.data, sizeof(f32) * 16);
    out_matrix.data[0] = 1.0f;
    out_matrix.data[5] = 1.0f;
    out_matrix.data[10] = 1.0f;
    out_matrix.data[15] = 1.0f;
    return out_matrix;
}

mat4 mat4_mul(mat4 matrix_0, mat4 matrix_1) {
    mat4 out_matrix = mat4_identity();

    const f32* m1_ptr = matrix_0.data;
    const f32* m2_ptr = matrix_1.data;
    f32* dst_ptr = out_matrix.data;

    for (i32 i = 0; i < 4; ++i) {
        for (i32 j = 0; j < 4; ++j) {
            *dst_ptr = m1_ptr[0] * m2_ptr[0 + j] + m1_ptr[1] * m2_ptr[4 + j] +
                       m1_ptr[2] * m2_ptr[8 + j] + m1_ptr[3] * m2_ptr[12 + j];
            dst_ptr++;
        }
        m1_ptr += 4;
    }
    return out_matrix;
}

mat4 mat4_orthographic(f32 left, f32 right, f32 bottom, f32 top,
                       f32 near_clip, f32 far_clip) {
    mat4 out_matrix = mat4_identity();

    f32 lr = 1.0f / (left - right);
    f32 bt = 1.0f / (bottom - top);
    f32 nf = 1.0f / (near_clip - far_clip);

    out_matrix.data[0] = -2.0f * lr;
    out_matrix.data[5] = -2.0f * bt;
    out_matrix.data[10] = nf;

    out_matrix.data[12] = (left + right) * lr;
    out_matrix.data[13] = (top + bottom) * bt;
    out_matrix.data[14] = -near_clip * nf;

    return out_matrix;
}

mat4 mat4_perspective(f32 fov_radians, f32 aspect_ratio, f32 near_clip, f32 far_clip) {
    f32 half_tan_fov = ktan(fov_radians * 0.5f);
    mat4 out_matrix;
    kzero_memory(out_matrix.data, sizeof(f32) * 16);
    out_matrix.data[0] = 1.0f / (aspect_ratio * half_tan_fov);
    out_matrix.data[5] = 1.0f / half_tan_fov;
    out_matrix.data[10] = far_clip / (near_clip - far_clip);
    out_matrix.data[11] = -1.0f;
    out_matrix.data[14] = (far_clip * near_clip) / (near_clip - far_clip);
    return out_matrix;
}

mat4 mat4_look_at(vec3 position, vec3 target, vec3 up) {
    mat4 out_matrix;
    vec3 z_axis = vec3_normalized(vec3_sub(target, position));
    vec3 x_axis = vec3_normalized(vec3_cross(z_axis, up));
    vec3 y_axis = vec3_cross(x_axis, z_axis);

    out_matrix.data[0] = x_axis.x;
    out_matrix.data[1] = y_axis.x;
    out_matrix.data[2] = -z_axis.x;
    out_matrix.data[3] = 0;
    out_matrix.data[4] = x_axis.y;
    out_matrix.data[5] = y_axis.y;
    out_matrix.data[6] = -z_axis.y;
    out_matrix.data[7] = 0;
    out_matrix.data[8] = x_axis.z;
    out_matrix.data[9] = y_axis.z;
    out_matrix.data[10] = -z_axis.z;
    out_matrix.data[11] = 0;
    out_matrix.data[12] = -vec3_dot(x_axis, position);
    out_matrix.data[13] = -vec3_dot(y_axis, position);
    out_matrix.data[14] = vec3_dot(z_axis, position);
    out_matrix.data[15] = 1.0f;
    return out_matrix;
}

mat4 mat4_transposed(mat4 matrix) {
    mat4 out_matrix;
    out_matrix.data[0] = matrix.data[0];
    out_matrix.data[1] = matrix.data[4];
    out_matrix.data[2] = matrix.data[8];
    out_matrix.data[3] = matrix.data[12];
    out_matrix.data[4] = matrix.data[1];
    out_matrix.data[5] = matrix.data[5];
    out_matrix.data[6] = matrix.data[9];
    out_matrix.data[7] = matrix.data[13];
    out_matrix.data[8] = matrix.data[2];
    out_matrix.data[9] = matrix.data[6];
    out_matrix.data[10] = matrix.data[10];
    out_matrix.data[11] = matrix.data[14];
    out_matrix.data[12] = matrix.data[3];
    out_matrix.data[13] = matrix.data[7];
    out_matrix.data[14] = matrix.data[11];
    out_matrix.data[15] = matrix.data[15];
    return out_matrix;
}

f32 mat4_determinant(mat4 matrix) {
    const f32* m = matrix.data;

    f32 t0 = m[10] * m[15];
    f32 t1 = m[14] * m[11];
    f32 t2 = m[6] * m[15];
    f32 t3 = m[14] * m[7];
    f32 t4 = m[6] * m[11];
    f32 t5 = m[10] * m[7];
    f32 t6 = m[2] * m[15];
    f32 t7 = m[14] * m[3];
    f32 t8 = m[2] * m[11];
    f32 t9 = m[10] * m[3];
    f32 t10 = m[2] * m[7];
    f32 t11 = m[6] * m[3];

    mat3 temp_mat;
    f32* o = temp_mat.data;

    o[0] = (t0 * m[5] + t3 * m[9] + t4 * m[13]) -
           (t1 * m[5] + t2 * m[9] + t5 * m[13]);
    o[1] = (t1 * m[1] + t6 * m[9] + t9 * m[13]) -
           (t0 * m[1] + t7 * m[9] + t8 * m[13]);
    o[2] = (t2 * m[1] + t7 * m[5] + t10 * m[13]) -
           (t3 * m[1] + t6 * m[5] + t11 * m[13]);
    o[3] = (t5 * m[1] + t8 * m[5] + t11 * m[9]) -
           (t4 * m[1] + t9 * m[5] + t10 * m[9]);

    f32 determinant = 1.0f / (m[0] * o[0] + m[4] * o[1] + m[8] * o[2] + m[12] * o[3]);
    return determinant;
}

mat4 mat4_inverse(mat4 matrix) {
    const f32* m = matrix.data;

    f32 t0 = m[10] * m[15];
    f32 t1 = m[14] * m[11];
    f32 t2 = m[6] * m[15];
    f32 t3 = m[14] * m[7];
    f32 t4 = m[6] * m[11];
    f32 t5 = m[10] * m[7];
    f32 t6 = m[2] * m[15];
    f32 t7 = m[14] * m[3];
    f32 t8 = m[2] * m[11];
    f32 t9 = m[10] * m[3];
    f32 t10 = m[2] * m[7];
    f32 t11 = m[6] * m[3];
    f32 t12 = m[8] * m[13];
    f32 t13 = m[12] * m[9];
    f32 t14 = m[4] * m[13];
    f32 t15 = m[12] * m[5];
    f32 t16 = m[4] * m[9];
    f32 t17 = m[8] * m[5];
    f32 t18 = m[0] * m[13];
    f32 t19 = m[12] * m[1];
    f32 t20 = m[0] * m[9];
    f32 t21 = m[8] * m[1];
    f32 t22 = m[0] * m[5];
    f32 t23 = m[4] * m[1];

    mat4 out_matrix;
    f32* o = out_matrix.data;

    o[0] = (t0 * m[5] + t3 * m[9] + t4 * m[13]) -
           (t1 * m[5] + t2 * m[9] + t5 * m[13]);
    o[1] = (t1 * m[1] + t6 * m[9] + t9 * m[13]) -
           (t0 * m[1] + t7 * m[9] + t8 * m[13]);
    o[2] = (t2 * m[1] + t7 * m[5] + t10 * m[13]) -
           (t3 * m[1] + t6 * m[5] + t11 * m[13]);
    o[3] = (t5 * m[1] + t8 * m[5] + t11 * m[9]) -
           (t4 * m[1] + t9 * m[5] + t10 * m[9]);

    f32 d = 1.0f / (m[0] * o[0] + m[4] * o[1] + m[8] * o[2] + m[12] * o[3]);

    // Check for singular matrix (determinant near zero)
    if (kabs(d) < 1e-6f) {
        // Return identity matrix if the determinant is close to zero (singular matrix)
        return mat4_identity();
    }

    o[0] = d * o[0];
    o[1] = d * o[1];
    o[2] = d * o[2];
    o[3] = d * o[3];
    o[4] = d * ((t1 * m[4] + t2 * m[8] + t5 * m[12]) -
                (t0 * m[4] + t3 * m[8] + t4 * m[12]));
    o[5] = d * ((t0 * m[0] + t7 * m[8] + t8 * m[12]) -
                (t1 * m[0] + t6 * m[8] + t9 * m[12]));
    o[6] = d * ((t3 * m[0] + t6 * m[4] + t11 * m[12]) -
                (t2 * m[0] + t7 * m[4] + t10 * m[12]));
    o[7] = d * ((t4 * m[0] + t9 * m[4] + t10 * m[8]) -
                (t5 * m[0] + t8 * m[4] + t11 * m[8]));
    o[8] = d * ((t12 * m[7] + t15 * m[11] + t16 * m[15]) -
                (t13 * m[7] + t14 * m[11] + t17 * m[15]));
    o[9] = d * ((t13 * m[3] + t18 * m[11] + t21 * m[15]) -
                (t12 * m[3] + t19 * m[11] + t20 * m[15]));
    o[10] = d * ((t14 * m[3] + t19 * m[7] + t22 * m[15]) -
                 (t15 * m[3] + t18 * m[7] + t23 * m[15]));
    o[11] = d * ((t17 * m[3] + t20 * m[7] + t23 * m[11]) -
                 (t16 * m[3] + t21 * m[7] + t22 * m[11]));
    o[12] = d * ((t14 * m[10] + t17 * m[14] + t13 * m[6]) -
                 (t16 * m[14] + t12 * m[6] + t15 * m[10]));
    o[13] = d * ((t20 * m[14] + t12 * m[2] + t19 * m[10]) -
                 (t18 * m[10] + t21 * m[14] + t13 * m[2]));
    o[14] = d * ((t18 * m[6] + t23 * m[14] + t15 * m[2]) -
                 (t22 * m[14] + t14 * m[2] + t19 * m[6]));
    o[15] = d * ((t22 * m[10] + t16 * m[2] + t21 * m[6]) -
                 (t20 * m[6] + t23 * m[10] + t17 * m[2]));

    return out_matrix;
}

mat4 mat4_translation(vec3 position) {
    mat4 out_matrix = mat4_identity();
    out_matrix.data[12] = position.x;
    out_matrix.data[13] = position.y;
    out_matrix.data[14] = position.z;
    return out_matrix;
}

mat4 mat4_scale(vec3 scale) {
    mat4 out_matrix = mat4_identity();
    out_matrix.data[0] = scale.x;
    out_matrix.data[5] = scale.y;
    out_matrix.data[10] = scale.z;
    return out_matrix;
}

mat4 mat4_from_translation_rotation_scale(vec3 t, quat r, vec3 s) {
    mat4 out_matrix;

    out_matrix.data[0] = (1.0f - 2.0f * (r.y * r.y + r.z * r.z)) * s.x;
    out_matrix.data[1] = (r.x * r.y + r.z * r.w) * s.x * 2.0f;
    out_matrix.data[2] = (r.x * r.z - r.y * r.w) * s.x * 2.0f;
    out_matrix.data[3] = 0.0f;
    out_matrix.data[4] = (r.x * r.y - r.z * r.w) * s.y * 2.0f;
    out_matrix.data[5] = (1.0f - 2.0f * (r.x * r.x + r.z * r.z)) * s.y;
    out_matrix.data[6] = (r.y * r.z + r.x * r.w) * s.y * 2.0f;
    out_matrix.data[7] = 0.0f;
    out_matrix.data[8] = (r.x * r.z + r.y * r.w) * s.z * 2.0f;
    out_matrix.data[9] = (r.y * r.z - r.x * r.w) * s.z * 2.0f;
    out_matrix.data[10] = (1.0f - 2.0f * (r.x * r.x + r.y * r.y)) * s.z;
    out_matrix.data[11] = 0.0f;
    out_matrix.data[12] = t.x;
    out_matrix.data[13] = t.y;
    out_matrix.data[14] = t.z;
    out_matrix.data[15] = 1.0f;

    return out_matrix;
}

mat4 mat4_euler_x(f32 angle_radians) {
    mat4 out_matrix = mat4_identity();
    f32 c = kcos(angle_radians);
    f32 s = ksin(angle_radians);

    out_matrix.data[5] = c;
    out_matrix.data[6] = s;
    out_matrix.data[9] = -s;
    out_matrix.data[10] = c;
    return out_matrix;
}

mat4 mat4_euler_y(f32 angle_radians) {
    mat4 out_matrix = mat4_identity();
    f32 c = kcos(angle_radians);
    f32 s = ksin(angle_radians);

    out_matrix.data[0] = c;
    out_matrix.data[2] = -s;
    out_matrix.data[8] = s;
    out_matrix.data[10] = c;
    return out_matrix;
}

mat4 mat4_euler_z(f32 angle_radians) {
    mat4 out_matrix = mat4_identity();

    f32 c = kcos(angle_radians);
    f32 s = ksin(angle_radians);

    out_matrix.data[0] = c;
    out_matrix.data[1] = s;
    out_matrix.data[4] = -s;
    out_matrix.data[5] = c;
    return out_matrix;
}

mat4 mat4_euler_xyz(f32 x_radians, f32 y_radians, f32 z_radians) {
    mat4 rx = mat4_euler_x(x_radians);
    mat4 ry = mat4_euler_y(y_radians);
    mat4 rz = mat4_euler_z(z_radians);
    mat4 out_matrix = mat4_mul(rx, ry);
    out_matrix = mat4_mul(out_matrix, rz);
    return out_matrix;
}

vec3 mat4_forward(mat4 matrix) {
    vec3 forward;
    forward.x = -matrix.data[8];
    forward.y = -matrix.data[9];
    forward.z = -matrix.data[10];
    vec3_normalize(&forward);
    return forward;
}

vec3 mat4_backward(mat4 matrix) {
    vec3 backward;
    backward.x = matrix.data[8];
    backward.y = matrix.data[9];
    backward.z = matrix.data[10];
    vec3_normalize(&backward);
    return backward;
}

vec3 mat4_up(mat4 matrix) {
    vec3 up;
    up.x = matrix.data[1];
    up.y = matrix.data[5];
    up.z = matrix.data[9];
    vec3_normalize(&up);
    return up;
}

vec3 mat4_down(mat4 matrix) {
    vec3 down;
    down.x = -matrix.data[1];
    down.y = -matrix.data[5];
    down.z = -matrix.data[9];
    vec3_normalize(&down);
    return down;
}

vec3 mat4_left(mat4 matrix) {
    vec3 left;
    left.x = -matrix.data[0];
    left.y = -matrix.data[1];
    left.z = -matrix.data[2];
    vec3_normalize(&left);
    return left;
}

vec3 mat4_right(mat4 matrix) {
    vec3 right;
    right.x = matrix.data[0];
    right.y = matrix.data[1];
    right.z = matrix.data[2];
    vec3_normalize(&right);
    return right;
}

vec3 mat4_position_get(const mat4* matrix) {
    vec3 pos;
    pos.x = matrix->data[12];
    pos.y = matrix->data[13];
    pos.z = matrix->data[14];
    return pos;
}

quat mat4_rotation_get(const mat4* matrix) {
    f32 trace = matrix->data[0] + matrix->data[5] + matrix->data[10]; // Sum of diagonal (xx + yy + zz)
    quat q;

    if (trace > 0.0f) {
        f32 s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (matrix->data[9] - matrix->data[6]) / s;
        q.y = (matrix->data[2] - matrix->data[8]) / s;
        q.z = (matrix->data[4] - matrix->data[1]) / s;
    } else {
        if (matrix->data[0] > matrix->data[5] && matrix->data[0] > matrix->data[10]) {
            f32 s = sqrtf(1.0f + matrix->data[0] - matrix->data[5] - matrix->data[10]) * 2.0f;
            q.w = (matrix->data[9] - matrix->data[6]) / s;
            q.x = 0.25f * s;
            q.y = (matrix->data[1] + matrix->data[4]) / s;
            q.z = (matrix->data[2] + matrix->data[8]) / s;
        } else if (matrix->data[5] > matrix->data[10]) {
            f32 s = sqrtf(1.0f + matrix->data[5] - matrix->data[0] - matrix->data[10]) * 2.0f;
            q.w = (matrix->data[2] - matrix->data[8]) / s;
            q.x = (matrix->data[1] + matrix->data[4]) / s;
            q.y = 0.25f * s;
            q.z = (matrix->data[6] + matrix->data[9]) / s;
        } else {
            f32 s = sqrtf(1.0f + matrix->data[10] - matrix->data[0] - matrix->data[5]) * 2.0f;
            q.w = (matrix->data[4] - matrix->data[1]) / s;
            q.x = (matrix->data[2] + matrix->data[8]) / s;
            q.y = (matrix->data[6] + matrix->data[9]) / s;
            q.z = 0.25f * s;
        }
    }

    return q;
}

vec3 mat4_scale_get(const mat4* matrix) {
    vec3 scale;
    scale.x = sqrtf(matrix->data[0] * matrix->data[0] + matrix->data[1] * matrix->data[1] + matrix->data[2] * matrix->data[2]);
    scale.y = sqrtf(matrix->data[4] * matrix->data[4] + matrix->data[5] * matrix->data[5] + matrix->data[6] * matrix->data[6]);
    scale.z = sqrtf(matrix->data[8] * matrix->data[8] + matrix->data[9] * matrix->data[9] + matrix->data[10] * matrix->data[10]);
    return scale;
}

vec3 mat4_mul_vec3(mat4 m, vec3 v) {
    return (vec3){
        v.x * m.data[0] + v.y * m.data[4] + v.z * m.data[8] + m.data[12],
        v.x * m.data[1] + v.y * m.data[5] + v.z * m.data[9] + m.data[13],
        v.x * m.data[2] + v.y * m.data[6] + v.z * m.data[10] + m.data[14]};
}

vec3 vec3_mul_mat4(vec3 v, mat4 m) {
    return (vec3){
        v.x * m.data[0] + v.y * m.data[4] + v.z * m.data[8] + m.data[12],
        v.x * m.data[1] + v.y * m.data[5] + v.z * m.data[9] + m.data[13],
        v.x * m.data[2] + v.y * m.data[6] + v.z * m.data[10] + m.data[14]};
}

vec4 mat4_mul_vec4(mat4 m, vec4 v) {
    return (vec4){
        v.x * m.data[0] + v.y * m.data[1] + v.z * m.data[2] + v.w * m.data[3],
        v.x * m.data[4] + v.y * m.data[5] + v.z * m.data[6] + v.w * m.data[7],
        v.x * m.data[8] + v.y * m.data[9] + v.z * m.data[10] + v.w * m.data[11],
        v.x * m.data[12] + v.y * m.data[13] + v.z * m.data[14] + v.w * m.data[15]};
}

vec4 vec4_mul_mat4(vec4 v, mat4 m) {
    return (vec4){
        v.x * m.data[0] + v.y * m.data[4] + v.z * m.data[8] + v.w * m.data[12],
        v.x * m.data[1] + v.y * m.data[5] + v.z * m.data[9] + v.w * m.data[13],
        v.x * m.data[2] + v.y * m.data[6] + v.z * m.data[10] + v.w * m.data[14],
        v.x * m.data[3] + v.y * m.data[7] + v.z * m.data[11] + v.w * m.data[15]};
}

// ------------------------------------------
// Quaternion
// ------------------------------------------

quat quat_identity(void) { return (quat){0, 0, 0, 1.0f}; }

quat quat_from_surface_normal(vec3 normal, vec3 reference_up) {
    normal = vec3_normalized(normal);
    reference_up = vec3_normalized(reference_up);

    // Compute rotation axis as the cross product
    vec3 axis = vec3_cross(reference_up, normal);
    f32 dot = vec3_dot(reference_up, normal);

    // If dot is near 1, the vectors are already aligned, return identity quaternion
    if (dot > 0.9999f) {
        return quat_identity();
    }

    // If dot is near -1, the vectors are opposite, use an arbitrary perpendicular axis
    if (dot < -0.9999f) {
        axis = vec3_normalized(vec3_cross(reference_up, (vec3){1, 0, 0})); // Try X-axis
        if (vec3_length_squared(axis) < K_FLOAT_EPSILON) {
            axis = vec3_normalized(vec3_cross(reference_up, (vec3){0, 0, 1})); // Try Z-axis
        }
    }

    // Compute the quaternion components
    f32 angle = kacos(dot); // Angle between the vectors
    return quat_from_axis_angle(axis, angle, false);
}

f32 quat_normal(quat q) {
    return ksqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

quat quat_normalize(quat q) {
    f32 normal = quat_normal(q);
    return (quat){q.x / normal, q.y / normal, q.z / normal, q.w / normal};
}

quat quat_conjugate(quat q) {
    return (quat){-q.x, -q.y, -q.z, q.w};
}

quat quat_inverse(quat q) {
    return quat_normalize(quat_conjugate(q));
}

quat quat_mul(quat q_0, quat q_1) {
    quat out_quaternion;

    out_quaternion.x =
        q_0.x * q_1.w + q_0.y * q_1.z - q_0.z * q_1.y + q_0.w * q_1.x;

    out_quaternion.y =
        -q_0.x * q_1.z + q_0.y * q_1.w + q_0.z * q_1.x + q_0.w * q_1.y;

    out_quaternion.z =
        q_0.x * q_1.y - q_0.y * q_1.x + q_0.z * q_1.w + q_0.w * q_1.z;

    out_quaternion.w =
        -q_0.x * q_1.x - q_0.y * q_1.y - q_0.z * q_1.z + q_0.w * q_1.w;

    return out_quaternion;
}

f32 quat_dot(quat q_0, quat q_1) {
    return q_0.x * q_1.x + q_0.y * q_1.y + q_0.z * q_1.z + q_0.w * q_1.w;
}

mat4 quat_to_mat4(quat q) {
    mat4 out_matrix = mat4_identity();

    // https://stackoverflow.com/questions/1556260/convert-quaternion-rotation-to-rotation-matrix

    quat n = quat_normalize(q);

    out_matrix.data[0] = 1.0f - 2.0f * n.y * n.y - 2.0f * n.z * n.z;
    out_matrix.data[1] = 2.0f * n.x * n.y - 2.0f * n.z * n.w;
    out_matrix.data[2] = 2.0f * n.x * n.z + 2.0f * n.y * n.w;

    out_matrix.data[4] = 2.0f * n.x * n.y + 2.0f * n.z * n.w;
    out_matrix.data[5] = 1.0f - 2.0f * n.x * n.x - 2.0f * n.z * n.z;
    out_matrix.data[6] = 2.0f * n.y * n.z - 2.0f * n.x * n.w;

    out_matrix.data[8] = 2.0f * n.x * n.z - 2.0f * n.y * n.w;
    out_matrix.data[9] = 2.0f * n.y * n.z + 2.0f * n.x * n.w;
    out_matrix.data[10] = 1.0f - 2.0f * n.x * n.x - 2.0f * n.y * n.y;

    return out_matrix;
}

mat4 quat_to_rotation_matrix(quat q, vec3 center) {
    mat4 out_matrix;

    f32* o = out_matrix.data;
    o[0] = (q.x * q.x) - (q.y * q.y) - (q.z * q.z) + (q.w * q.w);
    o[1] = 2.0f * ((q.x * q.y) + (q.z * q.w));
    o[2] = 2.0f * ((q.x * q.z) - (q.y * q.w));
    o[3] = center.x - center.x * o[0] - center.y * o[1] - center.z * o[2];

    o[4] = 2.0f * ((q.x * q.y) - (q.z * q.w));
    o[5] = -(q.x * q.x) + (q.y * q.y) - (q.z * q.z) + (q.w * q.w);
    o[6] = 2.0f * ((q.y * q.z) + (q.x * q.w));
    o[7] = center.y - center.x * o[4] - center.y * o[5] - center.z * o[6];

    o[8] = 2.0f * ((q.x * q.z) + (q.y * q.w));
    o[9] = 2.0f * ((q.y * q.z) - (q.x * q.w));
    o[10] = -(q.x * q.x) - (q.y * q.y) + (q.z * q.z) + (q.w * q.w);
    o[11] = center.z - center.x * o[8] - center.y * o[9] - center.z * o[10];

    o[12] = 0.0f;
    o[13] = 0.0f;
    o[14] = 0.0f;
    o[15] = 1.0f;
    return out_matrix;
}

quat quat_from_axis_angle(vec3 axis, f32 angle, b8 normalize) {
    const f32 half_angle = 0.5f * angle;
    f32 s = ksin(half_angle);
    f32 c = kcos(half_angle);

    quat q = (quat){s * axis.x, s * axis.y, s * axis.z, c};
    if (normalize) {
        return quat_normalize(q);
    }
    return q;
}

quat quat_slerp(quat q_0, quat q_1, f32 percentage) {
    quat out_quaternion;
    // Source: https://en.wikipedia.org/wiki/Slerp
    // Only unit quaternions are valid rotations.
    // Normalize to avoid undefined behavior.
    quat v0 = quat_normalize(q_0);
    quat v1 = quat_normalize(q_1);

    // Compute the cosine of the angle between the two vectors.
    f32 dot = quat_dot(v0, v1);

    // If the dot product is negative, slerp won't take
    // the shorter path. Note that v1 and -v1 are equivalent when
    // the negation is applied to all four components. Fix by
    // reversing one quaternion.
    if (dot < 0.0f) {
        v1.x = -v1.x;
        v1.y = -v1.y;
        v1.z = -v1.z;
        v1.w = -v1.w;
        dot = -dot;
    }

    const f32 DOT_THRESHOLD = 0.9995f;
    if (dot > DOT_THRESHOLD) {
        // If the inputs are too close for comfort, linearly interpolate
        // and normalize the result.
        out_quaternion = (quat){v0.x + ((v1.x - v0.x) * percentage),
                                v0.y + ((v1.y - v0.y) * percentage),
                                v0.z + ((v1.z - v0.z) * percentage),
                                v0.w + ((v1.w - v0.w) * percentage)};

        return quat_normalize(out_quaternion);
    }

    // Since dot is in range [0, DOT_THRESHOLD], acos is safe
    f32 theta_0 = kacos(dot);         // theta_0 = angle between input vectors
    f32 theta = theta_0 * percentage; // theta = angle between v0 and result
    f32 sin_theta = ksin(theta);      // compute this value only once
    f32 sin_theta_0 = ksin(theta_0);  // compute this value only once

    f32 s0 =
        kcos(theta) -
        dot * sin_theta / sin_theta_0; // == sin(theta_0 - theta) / sin(theta_0)
    f32 s1 = sin_theta / sin_theta_0;

    return (quat){(v0.x * s0) + (v1.x * s1), (v0.y * s0) + (v1.y * s1),
                  (v0.z * s0) + (v1.z * s1), (v0.w * s0) + (v1.w * s1)};
}

// ------------------------------------------
// Plane3D
// ------------------------------------------

plane_3d plane_3d_create(vec3 position, vec3 normal) {
    plane_3d p;
    p.normal = vec3_normalized(normal);
    p.distance = vec3_dot(p.normal, position);
    return p;
}

f32 plane_signed_distance(const plane_3d* p, const vec3* position) {
    return vec3_dot(p->normal, *position) - p->distance;
}

b8 plane_intersects_sphere(const plane_3d* p, const vec3* center, f32 radius) {
    return plane_signed_distance(p, center) > -radius;
}

b8 plane_intersects_aabb(const plane_3d* p, const vec3* center, const vec3* extents) {
    f32 r = extents->x * kabs(p->normal.x) +
            extents->y * kabs(p->normal.y) +
            extents->z * kabs(p->normal.z);

    f32 distance = plane_signed_distance(p, center);

    if (distance <= -r) {
        return false;
    }

    return true;
}

// ------------------------------------------
// Frustum
// ------------------------------------------

frustum frustum_create(const vec3* position, const vec3* target, const vec3* up, f32 aspect, f32 fov, f32 near, f32 far) {
    frustum f;

    // Calculate the forward vector (negative Z direction for right-handed systems)
    vec3 forward = vec3_normalized(vec3_sub(*target, *position));

    // Calculate the right vector (X-axis), ensuring a right-handed system
    vec3 right = vec3_normalized(vec3_cross(forward, *up));

    // Recalculate the true up vector (Y-axis) to ensure orthogonality
    vec3 adjusted_up = vec3_cross(right, forward);

    // Half dimensions at the far plane
    f32 half_v = far * tanf(fov * 0.5f); // Vertical half
    f32 half_h = half_v * aspect;        // Horizontal half

    vec3 forward_far = vec3_mul_scalar(forward, far);
    vec3 forward_near = vec3_mul_scalar(forward, near);
    vec3 right_half_h = vec3_mul_scalar(right, half_h);
    vec3 up_half_v = vec3_mul_scalar(adjusted_up, half_v);

    // Top plane
    f.sides[FRUSTUM_SIDE_TOP] = plane_3d_create(
        vec3_add(*position, forward_far),
        vec3_cross(right, vec3_sub(forward_far, up_half_v)));

    // Bottom plane
    f.sides[FRUSTUM_SIDE_BOTTOM] = plane_3d_create(
        vec3_add(*position, forward_far),
        vec3_cross(vec3_add(forward_far, up_half_v), right));

    // Right plane
    f.sides[FRUSTUM_SIDE_RIGHT] = plane_3d_create(
        vec3_add(*position, forward_far),
        vec3_cross(adjusted_up, vec3_sub(forward_far, right_half_h)));

    // Left plane
    f.sides[FRUSTUM_SIDE_LEFT] = plane_3d_create(
        vec3_add(*position, forward_far),
        vec3_cross(vec3_add(forward_far, right_half_h), adjusted_up));

    // Far plane
    f.sides[FRUSTUM_SIDE_FAR] = plane_3d_create(
        vec3_add(*position, forward_far),
        vec3_mul_scalar(forward, -1.0f) // Normal points back toward the camera
    );

    // Near plane
    f.sides[FRUSTUM_SIDE_NEAR] = plane_3d_create(
        vec3_add(*position, forward_near),
        forward // Normal points away from the camera
    );

    return f;
}

frustum frustum_from_view_projection(mat4 view_projection) {
    frustum f;

    // Get the inverse of the view_projection matrix.
    mat4 inv = mat4_inverse(view_projection);
    f32* md = inv.data;

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

b8 frustum_intersects_sphere(const frustum* f, const vec3* center, f32 radius) {
    for (u8 i = 0; i < 6; ++i) {
        if (!plane_intersects_sphere(&f->sides[i], center, radius)) {
            return false;
        }
    }
    return true;
}

b8 frustum_intersects_aabb(const frustum* f, const vec3* center, const vec3* extents) {
    for (u8 i = 0; i < 6; ++i) {
        if (!plane_intersects_aabb(&f->sides[i], center, extents)) {
            return false;
        }
    }
    return true;
}

void frustum_corner_points_world_space(mat4 projection_view, vec4* corners) {
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

// ------------------------------------------
// Oriented Bounding Box (OBB)
// ------------------------------------------

f32 oriented_bounding_box_project(const oriented_bounding_box* obb, vec3 axis) {
    vec3 right = vec3_rotate((vec3){1, 0, 0}, obb->rotation);
    vec3 up = vec3_rotate((vec3){0, 1, 0}, obb->rotation);
    vec3 forward = vec3_rotate((vec3){0, 0, 1}, obb->rotation);

    return kabs(vec3_dot(axis, right)) * obb->half_extents.x +
           kabs(vec3_dot(axis, up)) * obb->half_extents.y +
           kabs(vec3_dot(axis, forward)) * obb->half_extents.z;
}

static void seed_randoms(void) {
    u32 ptime_u32;
    u32 ptime_u64;
#ifdef KOHI_DEBUG
    // NOTE: Use a predetermined seed for debug builds for testing purposes.
    ptime_u32 = 42;
    ptime_u64 = 42;
#else
    // TODO: Might need to use current date/time for this in case this
    // as using the absolute time is the application _run_ time, which
    // might not be random _enough_ for this to be truly useful.
    ptime_u32 = (u32)platform_get_absolute_time();
    ptime_u64 = (u64)platform_get_absolute_time();
#endif

    // Seed standard random number generator.
    srand(ptime_u32);
    // 64-bit RNG
    rng_u64 = mtrand_create(ptime_u64);

    rand_seeded = true;
}
