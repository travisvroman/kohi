/**
 * @file kmath.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains definitions for various important constant values
 * as well as functions for many common math types. Note that this math library
 * is all written to be right-handed (-z forward, +y up) and in column-major format.
 * @version 2.0
 * @date 2025-01-26
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2025
 *
 */

#pragma once

#include "defines.h"
#include "math_types.h"
#include "memory/kmemory.h"

/** @brief An approximate representation of PI. */
#define K_PI 3.14159265358979323846f

/** @brief An approximate representation of PI multiplied by 2. */
#define K_2PI (2.0f * K_PI)

/** @brief An approximate representation of PI multiplied by 4. */
#define K_4PI (4.0f * K_PI)

/** @brief An approximate representation of PI divided by 2. */
#define K_HALF_PI (0.5f * K_PI)

/** @brief An approximate representation of PI divided by 4. */
#define K_QUARTER_PI (0.25f * K_PI)

/** @brief One divided by an approximate representation of PI. */
#define K_ONE_OVER_PI (1.0f / K_PI)

/** @brief One divided by half of an approximate representation of PI. */
#define K_ONE_OVER_TWO_PI (1.0f / K_2PI)

/** @brief An approximation of the square root of 2. */
#define K_SQRT_TWO 1.41421356237309504880f

/** @brief An approximation of the square root of 3. */
#define K_SQRT_THREE 1.73205080756887729352f

/** @brief One divided by an approximation of the square root of 2. */
#define K_SQRT_ONE_OVER_TWO 0.70710678118654752440f

/** @brief One divided by an approximation of the square root of 3. */
#define K_SQRT_ONE_OVER_THREE 0.57735026918962576450f

/** @brief A multiplier used to convert degrees to radians. */
#define K_DEG2RAD_MULTIPLIER (K_PI / 180.0f)

/** @brief A multiplier used to convert radians to degrees. */
#define K_RAD2DEG_MULTIPLIER (180.0f / K_PI)

/** @brief The multiplier to convert seconds to microseconds. */
#define K_SEC_TO_US_MULTIPLIER (1000.0f * 1000.0f)

/** @brief The multiplier to convert seconds to milliseconds. */
#define K_SEC_TO_MS_MULTIPLIER 1000.0f

/** @brief The multiplier to convert milliseconds to seconds. */
#define K_MS_TO_SEC_MULTIPLIER 0.001f

/** @brief A huge number that should be larger than any valid number used. */
#define K_INFINITY (1e30f * 1e30f)

/** @brief Smallest positive number where 1.0 + FLOAT_EPSILON != 0 */
#define K_FLOAT_EPSILON 1.192092896e-07f

#define K_FLOAT_MIN -3.40282e+38F

#define K_FLOAT_MAX 3.40282e+38F

// ------------------------------------------
// General math functions
// ------------------------------------------

/**
 * Swaps the values in the given float pointers.
 * @param a A pointer to the first float.
 * @param b A pointer to the second float.
 */
KINLINE void kswapf(f32* a, f32* b) {
    f32 temp = *a;
    *a = *b;
    *b = temp;
}

#define KSWAP(type, a, b) \
    {                     \
        type temp = a;    \
        a = b;            \
        b = temp;         \
    }

/** @brief Returns 0.0f if x == 0.0f, -1.0f if negative, otherwise 1.0f. */
KINLINE f32 ksign(f32 x) {
    return x == 0.0f ? 0.0f : x < 0.0f ? -1.0f
                                       : 1.0f;
}

/** @brief Compares x to edge, returning 0 if x < edge; otherwise 1.0f; */
KINLINE f32 kstep(f32 edge, f32 x) {
    return x < edge ? 0.0f : 1.0f;
}

/**
 * @brief Calculates the sine of x.
 *
 * @param x The number to calculate the sine of.
 * @return The sine of x.
 */
KAPI f32 ksin(f32 x);

/**
 * @brief Calculates the cosine of x.
 *
 * @param x The number to calculate the cosine of.
 * @return The cosine of x.
 */
KAPI f32 kcos(f32 x);

/**
 * @brief Calculates the tangent of x.
 *
 * @param x The number to calculate the tangent of.
 * @return The tangent of x.
 */
KAPI f32 ktan(f32 x);

/**
 * @brief Calculates the arctangent of x.
 *
 * @param x The number to calculate the arctangent of.
 * @return The arctangent of x.
 */
KAPI f32 katan(f32 x);

KAPI f32 katan2(f32 x, f32 y);

KAPI f32 kasin(f32 x);

/**
 * @brief Calculates the arc cosine of x.
 *
 * @param x The number to calculate the arc cosine of.
 * @return The arc cosine of x.
 */
KAPI f32 kacos(f32 x);

/**
 * @brief Calculates the square root of x.
 *
 * @param x The number to calculate the square root of.
 * @return The square root of x.
 */
KAPI f32 ksqrt(f32 x);

/**
 * @brief Calculates the absolute value of x.
 *
 * @param x The number to get the absolute value of.
 * @return The absolute value of x.
 */
KAPI f32 kabs(f32 x);

/**
 * @brief Returns the largest integer value less than or equal to x.
 *
 * @param x The value to be examined.
 * @return the largest integer value less than or equal to x.
 */
KAPI f32 kfloor(f32 x);

/**
 * @brief Returns the smallest integer value greater than or equal to x.
 *
 * @param x The value to be examined.
 * @return the smallest integer value greater than or equal to x.
 */
KAPI f32 kceil(f32 x);

/**
 * @brief Computes the logarithm of x.
 *
 * @param x The value to be examined.
 * @return The logarithm of x.
 */
KAPI f32 klog(f32 x);

/**
 * @brief Computes the base-2 logarithm of x (i.e. how many times x can be divided by 2).
 *
 * @param x The value to be examined.
 * @return The base-2 logarithm of x.
 */
KAPI f32 klog2(f32 x);

/**
 * @brief Raises x to the power of y.
 *
 * @param x The number to be raised.
 * @param y The exponent to raise to.
 * @return The value of x raised to the power of y.
 */
KAPI f32 kpow(f32 x, f32 y);

KAPI f32 kexp(f32 x);

/**
 * @brief Calculates a linear value between a and b based on parameter t.
 *
 * Any value of t outside the range of 0-1 extends beyond a or b, respectively.
 *
 * @param a The start number.
 * @param b The target number.
 * @param t Interpolation parameter, typically between 0 and 1.
 */
KINLINE f32 klerp(f32 a, f32 b, f32 t) {
    return a + t * (b - a);
}

/**
 * @brief Indicates if the value is a power of 2. 0 is considered _not_ a power
 * of 2.
 * @param value The value to be interpreted.
 * @returns True if a power of 2, otherwise false.
 */
KINLINE b8 is_power_of_2(u64 value) {
    return (value != 0) && ((value & (value - 1)) == 0);
}

/**
 * @brief Returns a random integer.
 *
 * @return A random integer.
 */
KAPI i32 krandom(void);

/**
 * @brief Returns a random integer that is within the given range (inclusive).
 *
 * @param min The minimum of the range.
 * @param max The maximum of the range.
 * @return A random integer.
 */
KAPI i32 krandom_in_range(i32 min, i32 max);

/**
 * @brief Returns a random unsigned 64-bit integer.
 * @return A random unsigned 64-bit integer.
 */
KAPI u64 krandom_u64(void);

/**
 * @brief Returns a random floating-point number.
 *
 * @return A random floating-point number.
 */
KAPI f32 kfrandom(void);

/**
 * @brief Returns a random floating-point number that is within the given range
 * (inclusive).
 *
 * @param min The minimum of the range.
 * @param max The maximum of the range.
 * @return A random floating-point number.
 */
KAPI f32 kfrandom_in_range(f32 min, f32 max);

/**
 * @brief Perform Hermite interpolation between two values.
 *
 * @param edge_0 The lower edge of the Hermite function.
 * @param edge_1 The upper edge of the Hermite function.
 * @param x The value to interpolate.
 * @return The interpolated value.
 */
KINLINE f32 ksmoothstep(f32 edge_0, f32 edge_1, f32 x) {
    f32 t = KCLAMP((x - edge_0) / (edge_1 - edge_0), 0.0f, 1.0f);
    return t * t * (3.0 - 2.0 * t);
}

/**
 * @brief Returns the attenuation of x based off distance from the midpoint of min and max.
 *
 * @param min The minimum value.
 * @param max The maximum value.
 * @param x The value to attenuate.
 * @return The attenuation of x based on distance of the midpoint of min and max.
 */
KAPI f32 kattenuation_min_max(f32 min, f32 max, f32 x);

/**
 * @brief Compares the two floats and returns true if both are less
 * than K_FLOAT_EPSILON apart; otherwise false.
 */
KINLINE b8 kfloat_compare(f32 f_0, f32 f_1) {
    return kabs(f_0 - f_1) < K_FLOAT_EPSILON;
}

/**
 * @brief Converts provided degrees to radians.
 *
 * @param degrees The degrees to be converted.
 * @return The amount in radians.
 */
KINLINE f32 deg_to_rad(f32 degrees) { return degrees * K_DEG2RAD_MULTIPLIER; }

/**
 * @brief Converts provided radians to degrees.
 *
 * @param radians The radians to be converted.
 * @return The amount in degrees.
 */
KINLINE f32 rad_to_deg(f32 radians) { return radians * K_RAD2DEG_MULTIPLIER; }

/**
 * @brief Converts value from the "old" range to the "new" range.
 *
 * @param value The value to be converted.
 * @param from_min The minimum value from the old range.
 * @param from_max The maximum value from the old range.
 * @param to_min The minimum value from the new range.
 * @param to_max The maximum value from the new range.
 * @return The converted value.
 */
KINLINE f32 range_convert_f32(f32 value, f32 old_min, f32 old_max, f32 new_min,
                              f32 new_max) {
    return (((value - old_min) * (new_max - new_min)) / (old_max - old_min)) +
           new_min;
}

/**
 * @brief Converts rgb int values [0-255] to a single 32-bit integer.
 *
 * @param r The red value [0-255].
 * @param g The green value [0-255].
 * @param b The blue value [0-255].
 * @param out_u32 A pointer to hold the resulting integer.
 */
KINLINE void rgbu_to_u32(u32 r, u32 g, u32 b, u32* out_u32) {
    *out_u32 = (((r & 0x0FF) << 16) | ((g & 0x0FF) << 8) | (b & 0x0FF));
}

/**
 * @brief Converts the given 32-bit integer to rgb values [0-255].
 *
 * @param rgbu The integer holding a rgb value.
 * @param out_r A pointer to hold the red value.
 * @param out_g A pointer to hold the green value.
 * @param out_b A pointer to hold the blue value.
 */
KINLINE void u32_to_rgb(u32 rgbu, u32* out_r, u32* out_g, u32* out_b) {
    *out_r = (rgbu >> 16) & 0x0FF;
    *out_g = (rgbu >> 8) & 0x0FF;
    *out_b = (rgbu) & 0x0FF;
}

/**
 * @brief Converts rgb integer values [0-255] to a vec3 of floating-point values
 * [0.0-1.0]
 *
 * @param r The red value [0-255].
 * @param g The green value [0-255].
 * @param b The blue value [0-255].
 * @param out_v A pointer to hold the vector of floating-point values.
 */
KINLINE void rgb_u32_to_vec3(u32 r, u32 g, u32 b, vec3* out_v) {
    out_v->r = r / 255.0f;
    out_v->g = g / 255.0f;
    out_v->b = b / 255.0f;
}

/**
 * @brief Converts a vec3 of rgbvalues [0.0-1.0] to integer rgb values [0-255].
 *
 * @param v The vector of rgb values [0.0-1.0] to be converted.
 * @param out_r A pointer to hold the red value.
 * @param out_g A pointer to hold the green value.
 * @param out_b A pointer to hold the blue value.
 */
KINLINE void vec3_to_rgb_u32(vec3 v, u32* out_r, u32* out_g, u32* out_b) {
    *out_r = v.r * 255;
    *out_g = v.g * 255;
    *out_b = v.b * 255;
}

// ------------------------------------------
// Vector 2
// ------------------------------------------

/**
 * @brief Creates and returns a new 2-element vector using the supplied values.
 *
 * @param x The x value.
 * @param y The y value.
 * @return A new 2-element vector.
 */
KINLINE vec2 vec2_create(f32 x, f32 y) {
    return (vec2){
        .x = x,
        .y = y};
}

/**
 * @brief Creates and returns a 2-component vector with all components set to
 * 0.0f.
 */
KINLINE vec2 vec2_zero(void) { return (vec2){0.0f, 0.0f}; }

/**
 * @brief Creates and returns a 2-component vector with all components set
 * to 1.0f.
 */
KINLINE vec2 vec2_one(void) { return (vec2){1.0f, 1.0f}; }

/**
 * @brief Creates and returns a 2-component vector pointing up (0, 1).
 */
KINLINE vec2 vec2_up(void) { return (vec2){0.0f, 1.0f}; }

/**
 * @brief Creates and returns a 2-component vector pointing down (0, -1).
 */
KINLINE vec2 vec2_down(void) { return (vec2){0.0f, -1.0f}; }

/**
 * @brief Creates and returns a 2-component vector pointing left (-1, 0).
 */
KINLINE vec2 vec2_left(void) { return (vec2){-1.0f, 0.0f}; }

/**
 * @brief Creates and returns a 2-component vector pointing right (1, 0).
 */
KINLINE vec2 vec2_right(void) { return (vec2){1.0f, 0.0f}; }

/**
 * @brief Adds vector_1 to vector_0 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec2 vec2_add(vec2 vector_0, vec2 vector_1) {
    return (vec2){vector_0.x + vector_1.x, vector_0.y + vector_1.y};
}

/**
 * @brief Subtracts vector_1 from vector_0 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec2 vec2_sub(vec2 vector_0, vec2 vector_1) {
    return (vec2){vector_0.x - vector_1.x, vector_0.y - vector_1.y};
}

/**
 * @brief Multiplies vector_0 by vector_1 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec2 vec2_mul(vec2 vector_0, vec2 vector_1) {
    return (vec2){vector_0.x * vector_1.x, vector_0.y * vector_1.y};
}

/**
 * @brief Multiplies all elements of vector_0 by scalar and returns a copy of
 * the result.
 *
 * @param vector_0 The vector to be multiplied.
 * @param scalar The scalar value.
 * @return A copy of the resulting vector.
 */
KINLINE vec2 vec2_mul_scalar(vec2 vector_0, f32 scalar) {
    return (vec2){vector_0.x * scalar, vector_0.y * scalar};
}

/**
 * @brief Multiplies vector_0 by vector_1, then adds the result to vector_2.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param vector_2 The third vector.
 * @return The resulting vector.
 */
KINLINE vec2 vec2_mul_add(vec2 vector_0, vec2 vector_1, vec2 vector_2) {
    return (vec2){
        vector_0.x * vector_1.x + vector_2.x,
        vector_0.y * vector_1.y + vector_2.y};
}

/**
 * @brief Multiplies vector_0 by scalar, then adds to vector_1.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param scalar The scalar value.
 * @return The resulting vector.
 */
KINLINE vec2 vec2_mul_add_scalar(vec2 vector_0, f32 scalar, vec2 vector_1) {
    return (vec2){
        vector_0.x * scalar + vector_1.x,
        vector_0.y * scalar + vector_1.y};
}

/**
 * @brief Divides vector_0 by vector_1 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec2 vec2_div(vec2 vector_0, vec2 vector_1) {
    return (vec2){vector_0.x / vector_1.x, vector_0.y / vector_1.y};
}

/**
 * @brief Returns the squared length of the provided vector.
 *
 * @param vector The vector to retrieve the squared length of.
 * @return The squared length.
 */
KINLINE f32 vec2_length_squared(vec2 vector) {
    return vector.x * vector.x + vector.y * vector.y;
}

/**
 * @brief Returns the length of the provided vector.
 *
 * @param vector The vector to retrieve the length of.
 * @return The length.
 */
KAPI f32 vec2_length(vec2 vector);

/**
 * @brief Normalizes the provided vector in place to a unit vector.
 *
 * @param vector A pointer to the vector to be normalized.
 */
KAPI void vec2_normalize(vec2* vector);

/**
 * @brief Returns a normalized copy of the supplied vector.
 *
 * @param vector The vector to be normalized.
 * @return A normalized copy of the supplied vector
 */
KAPI vec2 vec2_normalized(vec2 vector);

/**
 * @brief Compares all elements of vector_0 and vector_1 and ensures the
 * difference is less than tolerance.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param tolerance The difference tolerance. Typically K_FLOAT_EPSILON or similar.
 *
 * @return True if within tolerance; otherwise false.
 */
KAPI b8 vec2_compare(vec2 vector_0, vec2 vector_1, f32 tolerance);

/**
 * @brief Returns the distance between vector_0 and vector_1.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The distance between vector_0 and vector_1.
 */
KAPI f32 vec2_distance(vec2 vector_0, vec2 vector_1);

/**
 * @brief Returns the squared distance between vector_0 and vector_1.
 * NOTE: If purely for comparison purposes, prefer this over non-squared version to avoid a sqrt call.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The distance between vector_0 and vector_1.
 */
KAPI f32 vec2_distance_squared(vec2 vector_0, vec2 vector_1);

// ------------------------------------------
// Vector 3
// ------------------------------------------

/**
 * @brief Creates and returns a new 3-element vector using the supplied values.
 *
 * @param x The x value.
 * @param y The y value.
 * @param z The z value.
 * @return A new 3-element vector.
 */
KINLINE vec3 vec3_create(f32 x, f32 y, f32 z) { return (vec3){x, y, z}; }

/*
 * @brief Returns a new vec3 containing the x, y and z components of the
 * supplied vec4, essentially dropping the w component.
 *
 * @param vector The 4-component vector to extract from.
 * @return A new vec3
 */
KINLINE vec3 vec3_from_vec4(vec4 vector) {
    return (vec3){vector.x, vector.y, vector.z};
}

/*
 * @brief Returns a new vec3 containing the x and y components of the
 * supplied vec2, with a z component specified.
 *
 * @param vector The 2-component vector to extract from.
 * @param z The value to use for the z element.
 * @return A new vec3
 */
KINLINE vec3 vec3_from_vec2(vec2 vector, f32 z) {
    return (vec3){vector.x, vector.y, z};
}

/**
 * @brief Returns a new vec4 using vector as the x, y and z components and w for w.
 *
 * @param vector The 3-component vector.
 * @param w The w component.
 * @return A new vec4
 */
KINLINE vec4 vec3_to_vec4(vec3 vector, f32 w) {
    return (vec4){vector.x, vector.y, vector.z, w};
}

/**
 * @brief Creates and returns a 3-component vector with all components set to
 * 0.0f.
 */
KINLINE vec3 vec3_zero(void) { return (vec3){0.0f, 0.0f, 0.0f}; }

/**
 * @brief Creates and returns a 3-component vector with all components set
 * to 1.0f.
 */
KINLINE vec3 vec3_one(void) { return (vec3){1.0f, 1.0f, 1.0f}; }

/**
 * @brief Creates and returns a 3-component vector pointing up (0, 1, 0).
 */
KINLINE vec3 vec3_up(void) { return (vec3){0.0f, 1.0f, 0.0f}; }

/**
 * @brief Creates and returns a 3-component vector pointing down (0, -1, 0).
 */
KINLINE vec3 vec3_down(void) { return (vec3){0.0f, -1.0f, 0.0f}; }

/**
 * @brief Creates and returns a 3-component vector pointing left (-1, 0, 0).
 */
KINLINE vec3 vec3_left(void) { return (vec3){-1.0f, 0.0f, 0.0f}; }

/**
 * @brief Creates and returns a 3-component vector pointing right (1, 0, 0).
 */
KINLINE vec3 vec3_right(void) { return (vec3){1.0f, 0.0f, 0.0f}; }

/**
 * @brief Creates and returns a 3-component vector pointing forward (0, 0, -1).
 */
KINLINE vec3 vec3_forward(void) { return (vec3){0.0f, 0.0f, -1.0f}; }

/**
 * @brief Creates and returns a 3-component vector pointing backward (0, 0, 1).
 */
KINLINE vec3 vec3_backward(void) { return (vec3){0.0f, 0.0f, 1.0f}; }

/**
 * @brief Adds vector_1 to vector_0 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec3 vec3_add(vec3 vector_0, vec3 vector_1) {
    return (vec3){vector_0.x + vector_1.x, vector_0.y + vector_1.y, vector_0.z + vector_1.z};
}

/**
 * @brief Subtracts vector_1 from vector_0 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec3 vec3_sub(vec3 vector_0, vec3 vector_1) {
    return (vec3){vector_0.x - vector_1.x, vector_0.y - vector_1.y, vector_0.z - vector_1.z};
}

/**
 * @brief Multiplies vector_0 by vector_1 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec3 vec3_mul(vec3 vector_0, vec3 vector_1) {
    return (vec3){vector_0.x * vector_1.x, vector_0.y * vector_1.y, vector_0.z * vector_1.z};
}

/**
 * @brief Multiplies all elements of vector_0 by scalar and returns a copy of
 * the result.
 *
 * @param vector_0 The vector to be multiplied.
 * @param scalar The scalar value.
 * @return A copy of the resulting vector.
 */
KINLINE vec3 vec3_mul_scalar(vec3 vector_0, f32 scalar) {
    return (vec3){vector_0.x * scalar, vector_0.y * scalar, vector_0.z * scalar};
}

/**
 * @brief Multiplies vector_0 by vector_1, then adds the result to vector_2.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param vector_2 The third vector.
 * @return The resulting vector.
 */
KINLINE vec3 vec3_mul_add(vec3 vector_0, vec3 vector_1, vec3 vector_2) {
    return (vec3){
        vector_0.x * vector_1.x + vector_2.x,
        vector_0.y * vector_1.y + vector_2.y,
        vector_0.z * vector_1.z + vector_2.z};
}

/**
 * @brief Multiplies vector_0 by scalar, then add that result to vector_1.
 *
 * @param vector_0 The first vector.
 * @param scalar The scalar value.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec3 vec3_mul_add_scalar(vec3 vector_0, f32 scalar, vec3 vector_1) {
    return (vec3){
        vector_0.x * scalar + vector_1.x,
        vector_0.y * scalar + vector_1.y,
        vector_0.z * scalar + vector_1.z};
}

/**
 * @brief Divides vector_0 by vector_1 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KINLINE vec3 vec3_div(vec3 vector_0, vec3 vector_1) {
    return (vec3){
        vector_0.x / vector_1.x,
        vector_0.y / vector_1.y,
        vector_0.z / vector_1.z};
}

KINLINE vec3 vec3_div_scalar(vec3 vector_0, f32 scalar) {
    vec3 result;
    for (u64 i = 0; i < 3; ++i) {
        result.elements[i] = vector_0.elements[i] / scalar;
    }
    return result;
}

/**
 * @brief Returns the squared length of the provided vector.
 *
 * @param vector The vector to retrieve the squared length of.
 * @return The squared length.
 */
KINLINE f32 vec3_length_squared(vec3 vector) {
    return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
}

/**
 * @brief Returns the length of the provided vector.
 *
 * @param vector The vector to retrieve the length of.
 * @return The length.
 */
KAPI f32 vec3_length(vec3 vector);

/**
 * @brief Normalizes the provided vector in place to a unit vector.
 *
 * @param vector A pointer to the vector to be normalized.
 */
KAPI void vec3_normalize(vec3* vector);

/**
 * @brief Returns a normalized copy of the supplied vector.
 *
 * @param vector The vector to be normalized.
 * @return A normalized copy of the supplied vector
 */
KAPI vec3 vec3_normalized(vec3 vector);

/**
 * @brief Returns the dot product between the provided vectors. Typically used
 * to calculate the difference in direction.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The dot product.
 */
KAPI f32 vec3_dot(vec3 vector_0, vec3 vector_1);

/**
 * @brief Calculates and returns the cross product of the supplied vectors.
 * The cross product is a new vector which is orthoganal to both provided
 * vectors.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The cross product.
 */
KAPI vec3 vec3_cross(vec3 vector_0, vec3 vector_1);

/**
 * @brief Returns the midpoint between two vectors.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The midpoint vector.
 */
KINLINE vec3 vec3_mid(vec3 vector_0, vec3 vector_1) {
    return (vec3){
        (vector_0.x - vector_1.x) * 0.5f,
        (vector_0.y - vector_1.y) * 0.5f,
        (vector_0.z - vector_1.z) * 0.5f};
}

/**
 * @brief Linearly interpolates between the first and second vectors based on parameter t.
 *
 * Performs linear interpolation between vector_0 and vector_1 based on parameter t, where:
 * - When t = 0.0, returns vector_0
 * - When t = 1.0, returns vector_1
 * - When t = 0.5, returns the midpoint of vector_0 and vector_1
 * - Values of t outside [0,1] extrapolate beyond the endpoints
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param t The interpolation factor (typically between 0 and 1).
 * @return The interpolated vector.
 */
KINLINE vec3 vec3_lerp(vec3 vector_0, vec3 vector_1, f32 t) {
    return (vec3){
        vector_0.x + (vector_1.x - vector_0.x) * t,
        vector_0.y + (vector_1.y - vector_0.y) * t,
        vector_0.z + (vector_1.z - vector_0.z) * t};
}

/**
 * @brief Compares all elements of vector_0 and vector_1 and ensures the
 * difference is less than tolerance.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param tolerance The difference tolerance. Typically K_FLOAT_EPSILON or
 * similar.
 * @return True if within tolerance; otherwise false.
 */
KAPI b8 vec3_compare(vec3 vector_0, vec3 vector_1, f32 tolerance);

/**
 * @brief Returns the distance between vector_0 and vector_1.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The distance between vector_0 and vector_1.
 */
KAPI f32 vec3_distance(vec3 vector_0, vec3 vector_1);

/**
 * @brief Returns the squared distance between vector_0 and vector_1.
 * Less intensive than calling the non-squared version due to sqrt.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The squared distance between vector_0 and vector_1.
 */
KAPI f32 vec3_distance_squared(vec3 vector_0, vec3 vector_1);

/**
 * @brief Projects v_0 onto v_1.
 *
 * @param v_0 The first vector.
 * @param v_1 The second vector.
 * @return The projected vector.
 */
KAPI vec3 vec3_project(vec3 v_0, vec3 v_1);

KAPI void vec3_project_points_onto_axis(vec3* points, u32 count, vec3 axis, f32* out_min, f32* out_max);

/**
 * @brief Reflects vector v along the given normal using r = v - 2(v·n)n
 *
 * @param v The vector to be reflected.
 * @param normal The normal to reflect along.
 * @returns The reflected vector.
 */
KAPI vec3 vec3_reflect(vec3 v, vec3 normal);

/**
 * @brief Transform v by m.
 * @param v The vector to transform.
 * @param w Pass 1.0f for a point, or 0.0f for a direction.
 * @param m The matrix to transform by.
 * @return A transformed copy of v.
 */
KAPI vec3 vec3_transform(vec3 v, f32 w, mat4 m);

/**
 * @brief Calculates the shortest Euclidean distance from a point to a line in 3D space.
 *
 * This function uses the cross product method to efficiently calculate the shortest
 * distance between a point and a line. The line is defined by a starting point and
 * a direction vector. The calculation is based on the geometric property that the
 * magnitude of the cross product of two vectors equals the area of the parallelogram
 * they form, divided by the length of one vector.
 *
 * @note If the line direction vector has zero length, returns the distance from the point to the line start point.
 * @warning The line direction vector should not be zero-length for proper line distance calculation.
 *
 * @param point The point to calculate distance from.
 * @param line_start Starting point of the line.
 * @param line_direction Direction vector of the line.
 *
 * @return The shortest Euclidean distance from the point to the line.
 */
KAPI f32 vec3_distance_to_line(vec3 point, vec3 line_start, vec3 line_direction);

KAPI vec3 vec3_min(vec3 vector_0, vec3 vector_1);

KAPI vec3 vec3_max(vec3 vector_0, vec3 vector_1);

KAPI vec3 vec3_sign(vec3 v);

KAPI vec3 vec3_rotate(vec3 v, quat q);

// ------------------------------------------
// Vector 4
// ------------------------------------------

/**
 * @brief Creates and returns a new 4-element vector using the supplied values.
 *
 * @param x The x value.
 * @param y The y value.
 * @param z The z value.
 * @param w The w value.
 * @return A new 4-element vector.
 */
KAPI vec4 vec4_create(f32 x, f32 y, f32 z, f32 w);

/**
 * @brief Returns a new vec3 containing the x, y and z components of the
 * supplied vec4, essentially dropping the w component.
 *
 * @param vector The 4-component vector to extract from.
 * @return A new vec3
 */
KAPI vec3 vec4_to_vec3(vec4 vector);

/**
 * @brief Returns a new vec4 using vector as the x, y and z components and w for
 * w.
 *
 * @param vector The 3-component vector.
 * @param w The w component.
 * @return A new vec4
 */
KAPI vec4 vec4_from_vec3(vec3 vector, f32 w);

/**
 * @brief Creates and returns a 4-component vector with all components set to
 * 0.0f.
 */
KINLINE vec4 vec4_zero(void) { return (vec4){0.0f, 0.0f, 0.0f, 0.0f}; }

/**
 * @brief Creates and returns a 4-component vector with all components set
 * to 1.0f.
 */
KINLINE vec4 vec4_one(void) { return (vec4){1.0f, 1.0f, 1.0f, 1.0f}; }

/**
 * @brief Adds vector_1 to vector_0 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KAPI vec4 vec4_add(vec4 vector_0, vec4 vector_1);

/**
 * @brief Subtracts vector_1 from vector_0 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KAPI vec4 vec4_sub(vec4 vector_0, vec4 vector_1);

/**
 * @brief Multiplies vector_0 by vector_1 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KAPI vec4 vec4_mul(vec4 vector_0, vec4 vector_1);

/**
 * @brief Multiplies all elements of vector_0 by scalar and returns a copy of
 * the result.
 *
 * @param vector_0 The vector to be multiplied.
 * @param scalar The scalar value.
 * @return A copy of the resulting vector.
 */
KAPI vec4 vec4_mul_scalar(vec4 vector_0, f32 scalar);

/**
 * @brief Multiplies vector_0 by vector_1, then adds the result to vector_2.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param vector_2 The third vector.
 * @return The resulting vector.
 */
KAPI vec4 vec4_mul_add(vec4 vector_0, vec4 vector_1, vec4 vector_2);

/**
 * @brief Divides vector_0 by vector_1 and returns a copy of the result.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @return The resulting vector.
 */
KAPI vec4 vec4_div(vec4 vector_0, vec4 vector_1);

KAPI vec4 vec4_div_scalar(vec4 vector_0, f32 scalar);

/**
 * @brief Returns the squared length of the provided vector.
 *
 * @param vector The vector to retrieve the squared length of.
 * @return The squared length.
 */
KAPI f32 vec4_length_squared(vec4 vector);

/**
 * @brief Returns the length of the provided vector.
 *
 * @param vector The vector to retrieve the length of.
 * @return The length.
 */
KAPI f32 vec4_length(vec4 vector);

/**
 * @brief Normalizes the provided vector in place to a unit vector.
 *
 * @param vector A pointer to the vector to be normalized.
 */
KAPI void vec4_normalize(vec4* vector);

/**
 * @brief Returns a normalized copy of the supplied vector.
 *
 * @param vector The vector to be normalized.
 * @return A normalized copy of the supplied vector
 */
KAPI vec4 vec4_normalized(vec4 vector);

/**
 * @brief Calculates the dot product using the elements of vec4s provided in
 * split-out format.
 *
 * @param a0 The first element of the a vector.
 * @param a1 The second element of the a vector.
 * @param a2 The third element of the a vector.
 * @param a3 The fourth element of the a vector.
 * @param b0 The first element of the b vector.
 * @param b1 The second element of the b vector.
 * @param b2 The third element of the b vector.
 * @param b3 The fourth element of the b vector.
 * @return The dot product of vectors and b.
 */
KAPI f32 vec4_dot_f32(f32 a0, f32 a1, f32 a2, f32 a3, f32 b0, f32 b1, f32 b2, f32 b3);

/**
 * @brief Compares all elements of vector_0 and vector_1 and ensures the
 * difference is less than tolerance.
 *
 * @param vector_0 The first vector.
 * @param vector_1 The second vector.
 * @param tolerance The difference tolerance. Typically K_FLOAT_EPSILON or
 * similar.
 * @return True if within tolerance; otherwise false.
 */
KAPI b8 vec4_compare(vec4 vector_0, vec4 vector_1, f32 tolerance);

/**
 * @brief Clamps the provided vector in-place to the given min/max values.
 *
 * @param vector A pointer to the vector to be clamped.
 * @param min The minimum value.
 * @param max The maximum value.
 */
KAPI void vec4_clamp(vec4* vector, f32 min, f32 max);

/**
 * @brief Returns a clamped copy of the provided vector.
 *
 * @param vector The vector to clamp.
 * @param min The minimum value.
 * @param max The maximum value.
 * @return A clamped copy of the provided vector.
 */
KAPI vec4 vec4_clamped(vec4 vector, f32 min, f32 max);

// ------------------------------------------
// Mat4 (4x4 matrix)
// ------------------------------------------

/**
 * @brief Creates and returns an identity matrix:
 *
 * {
 *   {1, 0, 0, 0},
 *   {0, 1, 0, 0},
 *   {0, 0, 1, 0},
 *   {0, 0, 0, 1}
 * }
 *
 * @return A new identity matrix
 */
KAPI mat4 mat4_identity(void);

/**
 * @brief Returns the result of multiplying matrix_0 and matrix_1.
 *
 * @param matrix_0 The first matrix to be multiplied.
 * @param matrix_1 The second matrix to be multiplied.
 * @return The result of the matrix multiplication.
 */
KAPI mat4 mat4_mul(mat4 matrix_0, mat4 matrix_1);

/**
 * @brief Creates and returns an orthographic projection matrix. Typically used
 * to render flat or 2D scenes.
 *
 * @param left The left side of the view frustum.
 * @param right The right side of the view frustum.
 * @param bottom The bottom side of the view frustum.
 * @param top The top side of the view frustum.
 * @param near_clip The near clipping plane distance.
 * @param far_clip The far clipping plane distance.
 * @return A new orthographic projection matrix.
 */
KAPI mat4 mat4_orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near_clip, f32 far_clip);

/**
 * @brief Creates and returns a perspective matrix. Typically used to render 3d
 * scenes.
 *
 * @param fov_radians The field of view in radians.
 * @param aspect_ratio The aspect ratio.
 * @param near_clip The near clipping plane distance.
 * @param far_clip The far clipping plane distance.
 * @return A new perspective matrix.
 */
KAPI mat4 mat4_perspective(f32 fov_radians, f32 aspect_ratio, f32 near_clip, f32 far_clip);

/**
 * @brief Creates and returns a look-at matrix, or a matrix looking
 * at target from the perspective of position.
 *
 * @param position The position of the matrix.
 * @param target The position to "look at".
 * @param up The up vector.
 * @return A matrix looking at target from the perspective of position.
 */
KAPI mat4 mat4_look_at(vec3 position, vec3 target, vec3 up);

/**
 * @brief Returns a transposed copy of the provided matrix (rows->colums)
 *
 * @param matrix The matrix to be transposed.
 * @return A transposed copy of of the provided matrix.
 */
KAPI mat4 mat4_transposed(mat4 matrix);

/**
 * @brief Calculates the determinant of the given matrix.
 *
 * @param matrix The matrix to calculate the determinant of.
 * @return The determinant of the given matrix.
 */
KAPI f32 mat4_determinant(mat4 matrix);

/**
 * @brief Creates and returns an inverse of the provided matrix.
 *
 * @param matrix The matrix to be inverted.
 * @return A inverted copy of the provided matrix.
 */
KAPI mat4 mat4_inverse(mat4 matrix);

/**
 * @brief Creates and returns a translation matrix from the given position.
 *
 * @param position The position to be used to create the matrix.
 * @return A newly created translation matrix.
 */
KAPI mat4 mat4_translation(vec3 position);

/**
 * @brief Returns a scale matrix using the provided scale.
 *
 * @param scale The 3-component scale.
 * @return A scale matrix.
 */
KAPI mat4 mat4_scale(vec3 scale);

/**
 * @brief Returns a matrix created from the provided translation, rotation and scale (TRS).
 *
 * @param position The position to be used to create the matrix.
 * @param rotation The quaternion rotation to be used to create the matrix.
 * @param scale The 3-component scale to be used to create the matrix.
 * @return A matrix created in TRS order.
 */
KAPI mat4 mat4_from_translation_rotation_scale(vec3 t, quat r, vec3 s);

/**
 * @brief Creates a rotation matrix from the provided x angle.
 *
 * @param angle_radians The x angle in radians.
 * @return A rotation matrix.
 */
KAPI mat4 mat4_euler_x(f32 angle_radians);

/**
 * @brief Creates a rotation matrix from the provided y angle.
 *
 * @param angle_radians The y angle in radians.
 * @return A rotation matrix.
 */
KAPI mat4 mat4_euler_y(f32 angle_radians);

/**
 * @brief Creates a rotation matrix from the provided z angle.
 *
 * @param angle_radians The z angle in radians.
 * @return A rotation matrix.
 */
KAPI mat4 mat4_euler_z(f32 angle_radians);

/**
 * @brief Creates a rotation matrix from the provided x, y and z axis rotations.
 *
 * @param x_radians The x rotation.
 * @param y_radians The y rotation.
 * @param z_radians The z rotation.
 * @return A rotation matrix.
 */
KAPI mat4 mat4_euler_xyz(f32 x_radians, f32 y_radians, f32 z_radians);

/**
 * @brief Returns a forward vector relative to the provided matrix.
 *
 * @param matrix The matrix from which to base the vector.
 * @return A 3-component directional vector.
 */
KAPI vec3 mat4_forward(mat4 matrix);

/**
 * @brief Returns a backward vector relative to the provided matrix.
 *
 * @param matrix The matrix from which to base the vector.
 * @return A 3-component directional vector.
 */
KAPI vec3 mat4_backward(mat4 matrix);

/**
 * @brief Returns a upward vector relative to the provided matrix.
 *
 * @param matrix The matrix from which to base the vector.
 * @return A 3-component directional vector.
 */
KAPI vec3 mat4_up(mat4 matrix);

/**
 * @brief Returns a downward vector relative to the provided matrix.
 *
 * @param matrix The matrix from which to base the vector.
 * @return A 3-component directional vector.
 */
KAPI vec3 mat4_down(mat4 matrix);

/**
 * @brief Returns a left vector relative to the provided matrix.
 *
 * @param matrix The matrix from which to base the vector.
 * @return A 3-component directional vector.
 */
KAPI vec3 mat4_left(mat4 matrix);

/**
 * @brief Returns a right vector relative to the provided matrix.
 *
 * @param matrix The matrix from which to base the vector.
 * @return A 3-component directional vector.
 */
KAPI vec3 mat4_right(mat4 matrix);

/**
 * @brief Returns the position relative to the provided matrix.
 *
 * @param matrix A pointer to the matrix from which to base the vector.
 * @return A 3-component positional vector.
 */
KAPI vec3 mat4_position_get(const mat4* matrix);

/**
 * @brief Returns the quaternion relative to the provided matrix.
 *
 * @param matrix A pointer to the matrix from which to base the quaternion.
 * @return A quaternion.
 */
KAPI quat mat4_rotation_get(const mat4* matrix);

/**
 * @brief Returns the scale relative to the provided matrix.
 *
 * @param matrix A pointer to the matrix from which to base the vector.
 * @return A 3-component scale vector.
 */
KAPI vec3 mat4_scale_get(const mat4* matrix);

/**
 * @brief Performs m * v
 *
 * @param m The matrix to be multiplied.
 * @param v The vector to multiply by.
 * @return The transformed vector.
 */
KAPI vec3 mat4_mul_vec3(mat4 m, vec3 v);

/**
 * @brief Performs v * m
 *
 * @param v The vector to bemultiplied.
 * @param m The matrix to be multiply by.
 * @return The transformed vector.
 */
KAPI vec3 vec3_mul_mat4(vec3 v, mat4 m);

/**
 * @brief Performs m * v
 *
 * @param m The matrix to be multiplied.
 * @param v The vector to multiply by.
 * @return The transformed vector.
 */
KAPI vec4 mat4_mul_vec4(mat4 m, vec4 v);

/**
 * @brief Performs v * m
 *
 * @param v The vector to bemultiplied.
 * @param m The matrix to be multiply by.
 * @return The transformed vector.
 */
KAPI vec4 vec4_mul_mat4(vec4 v, mat4 m);

// ------------------------------------------
// Quaternion
// ------------------------------------------

/**
 * @brief Creates an identity quaternion.
 *
 * @return An identity quaternion.
 */
KAPI quat quat_identity(void);

KAPI quat quat_from_surface_normal(vec3 normal, vec3 reference_up);

/**
 * @brief Returns the normal of the provided quaternion.
 *
 * @param q The quaternion.
 * @return The normal of the provided quaternion.
 */
KAPI f32 quat_normal(quat q);

/**
 * @brief Returns a normalized copy of the provided quaternion.
 *
 * @param q The quaternion to normalize.
 * @return A normalized copy of the provided quaternion.
 */
KAPI quat quat_normalize(quat q);

/**
 * @brief Returns the conjugate of the provided quaternion. That is,
 * The x, y and z elements are negated, but the w element is untouched.
 *
 * @param q The quaternion to obtain a conjugate of.
 * @return The conjugate quaternion.
 */
KAPI quat quat_conjugate(quat q);

/**
 * @brief Returns an inverse copy of the provided quaternion.
 *
 * @param q The quaternion to invert.
 * @return An inverse copy of the provided quaternion.
 */
KAPI quat quat_inverse(quat q);

/**
 * @brief Multiplies the provided quaternions.
 *
 * @param q_0 The first quaternion.
 * @param q_1 The second quaternion.
 * @return The multiplied quaternion.
 */
KAPI quat quat_mul(quat q_0, quat q_1);

/**
 * @brief Calculates the dot product of the provided quaternions.
 *
 * @param q_0 The first quaternion.
 * @param q_1 The second quaternion.
 * @return The dot product of the provided quaternions.
 */
KAPI f32 quat_dot(quat q_0, quat q_1);

/**
 * @brief Creates a rotation matrix from the given quaternion.
 *
 * @param q The quaternion to be used.
 * @return A rotation matrix.
 */
KAPI mat4 quat_to_mat4(quat q);

/**
 * @brief Calculates a rotation matrix based on the quaternion and the passed in
 * center point.
 *
 * @param q The quaternion.
 * @param center The center point.
 * @return A rotation matrix.
 */
KAPI mat4 quat_to_rotation_matrix(quat q, vec3 center);

/**
 * @brief Creates a quaternion from the given axis and angle.
 *
 * @param axis The axis of rotation.
 * @param angle The angle of rotation.
 * @param normalize Indicates if the quaternion should be normalized.
 * @return A new quaternion.
 */
KAPI quat quat_from_axis_angle(vec3 axis, f32 angle, b8 normalize);

KAPI quat quat_from_euler_radians(vec3 euler_rotation_radians);

KAPI vec3 quat_to_euler_radians(quat q);

KAPI vec3 quat_to_euler(quat q);

KAPI quat quat_from_direction(vec3 direction);

KAPI quat quat_lookat(vec3 from, vec3 to);

/**
 * @brief Calculates spherical linear interpolation of a given percentage
 * between two quaternions.
 *
 * @param q_0 The first quaternion.
 * @param q_1 The second quaternion.
 * @param percentage The percentage of interpolation, typically a value from
 * 0.0f-1.0f.
 * @return An interpolated quaternion.
 */
KAPI quat quat_slerp(quat q_0, quat q_1, f32 percentage);

// ------------------------------------------
// Plane3D
// ------------------------------------------

/**
 * @brief Creates a three-dimensional plane based on the provided position and normal.
 *
 * @param position
 * @param normal
 * @return A three-dimensional plane.
 */
KAPI plane_3d plane_3d_create(vec3 position, vec3 normal);

/**
 * @brief Obtains the signed distance between the plane p and the provided
 * postion.
 *
 * @param p A constant pointer to a plane.
 * @param position A constant pointer to a position.
 * @return The signed distance from the point to the plane.
 */
KAPI f32 plane_signed_distance(const plane_3d* p, const vec3* position);

/**
 * @brief Indicates if plane p intersects a sphere constructed via center and
 * radius.
 *
 * @param p A constant pointer to a plane.
 * @param center A constant pointer to a position representing the center of a
 * sphere.
 * @param radius The radius of the sphere.
 * @return True if the sphere intersects the plane; otherwise false.
 */
KAPI b8 plane_intersects_sphere(const plane_3d* p, const vec3* center, f32 radius);

/**
 * @brief Indicates if plane p intersects an axis-aligned bounding box
 * constructed via center and extents.
 *
 * @param p A constant pointer to a plane.
 * @param center A constant pointer to a position representing the center of an
 * axis-aligned bounding box.
 * @param extents The half-extents of an axis-aligned bounding box.
 * @return True if the axis-aligned bounding box intersects the plane; otherwise
 * false.
 */
KAPI b8 plane_intersects_aabb(const plane_3d* p, const vec3* center, const vec3* extents);

// ------------------------------------------
// Frustum
// ------------------------------------------

/**
 * @brief Creates and returns a frustum based on the provided position,
 * direction vectors, aspect, field of view, and near/far clipping planes
 * (typically obtained from a camera). This is typically used for frustum
 * culling.
 *
 * @param position A constant pointer to the position to be used.
 * @param target A constant pointer to the target vector to be used.
 * @param up A constant pointer to the up vector to be used.
 * @param aspect The aspect ratio.
 * @param fov The vertical field of view.
 * @param near The near clipping plane distance.
 * @param far The far clipping plane distance.
 * @return A shiny new frustum.
 */
KAPI frustum frustum_create(const vec3* position, const vec3* target, const vec3* up, f32 aspect, f32 fov, f32 near, f32 far);

KAPI frustum frustum_from_view_projection(mat4 view_projection);

/**
 * @brief Indicates if the frustum intersects (or contains) a sphere constructed
 * via center and radius.
 *
 * @param f A constant pointer to a frustum.
 * @param center A constant pointer to a position representing the center of a
 * sphere.
 * @param radius The radius of the sphere.
 * @return True if the sphere is intersected by or contained within the frustum
 * f; otherwise false.
 */
KAPI b8 frustum_intersects_sphere(const frustum* f, const vec3* center, f32 radius);

/**
 * @brief Indicates if frustum f intersects an axis-aligned bounding box
 * constructed via center and extents.
 *
 * @param f A constant pointer to a frustum.
 * @param center A constant pointer to a position representing the center of an
 * axis-aligned bounding box.
 * @param extents The half-extents of an axis-aligned bounding box.
 * @return True if the axis-aligned bounding box is intersected by or contained
 * within the frustum f; otherwise false.
 */
KAPI b8 frustum_intersects_aabb(const frustum* f, const vec3* center, const vec3* extents);

/**
 * Calculate the corner points of the provided frustum in world space, using
 * the given projection and view matrices.
 *
 * @param projection_view The combined projection/view matrix from the active camera.
 * @param corners An array of 8 vec4s to hold the caluclated points.
 */
KAPI void frustum_corner_points_world_space(mat4 projection_view, vec4* corners);

// ------------------------------------------
// Oriented Bounding Box (OBB)
// ------------------------------------------

KAPI f32 oriented_bounding_box_project(const oriented_bounding_box* obb, vec3 axis);

KINLINE b8 rect_2d_contains_point(rect_2d rect, vec2 point) {
    return (point.x >= rect.x && point.x <= rect.x + rect.width) && (point.y >= rect.y && point.y <= rect.y + rect.height);
}

KINLINE vec3 extents_2d_half(extents_2d extents) {
    return (vec3){
        (extents.min.x + extents.max.x) * 0.5f,
        (extents.min.y + extents.max.y) * 0.5f,
    };
}

KINLINE vec3 extents_3d_half(extents_3d extents) {
    return (vec3){
        (extents.min.x + extents.max.x) * 0.5f,
        (extents.min.y + extents.max.y) * 0.5f,
        (extents.min.z + extents.max.z) * 0.5f,
    };
}

KINLINE vec2 vec2_mid(vec2 v_0, vec2 v_1) {
    return (vec2){
        (v_0.x - v_1.x) * 0.5f,
        (v_0.y - v_1.y) * 0.5f};
}

KINLINE vec3 edge_3d_get_closest_point(vec3 point, vec3 edge_start, vec3 edge_end) {
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

KINLINE vec3 triangle_3d_get_normal(const triangle_3d* tri) {
    vec3 edge1 = vec3_sub(tri->verts[1], tri->verts[0]);
    vec3 edge2 = vec3_sub(tri->verts[2], tri->verts[0]);

    vec3 normal = vec3_cross(edge1, edge2);
    return vec3_normalized(normal);
}

KINLINE vec3 triangle_3d_get_closest_point(vec3 point, const triangle_3d* tri) {
    vec3 p0 = tri->verts[0];
    vec3 p1 = tri->verts[1];
    vec3 p2 = tri->verts[2];

    vec3 closest_0_1 = edge_3d_get_closest_point(point, p0, p1);
    vec3 closest_1_2 = edge_3d_get_closest_point(point, p1, p2);
    vec3 closest_2_0 = edge_3d_get_closest_point(point, p2, p0);

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
