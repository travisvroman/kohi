#ifndef _KOHI_MATH_GEOMETRY_2D_H_
#define _KOHI_MATH_GEOMETRY_2D_H_

#include "defines.h"
#include "kmath.h"
#include "math_types.h"

/**
 * @brief Represents a two-dimensional circle in space.
 */
typedef struct circle_2d {
    /** @brief The center point of the circle. */
    vec2 center;
    /** @brief The radius of the circle. */
    f32 radius;
} circle_2d;

/**
 * @brief Indicates if the provided point is within the given rectangle.
 * @param point The point to check.
 * @param rect The rectangle to check against.
 * @returns True if the point is within the rectangle; otherwise false.
 */
KINLINE b8 point_in_rect_2d(vec2 point, rect_2d rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width && point.y >= rect.y && point.y <= rect.y + rect.height;
}

/**
 * @brief Indicates if the provided integer-based point is within the given integer-based rectangle.
 * @param point The integer-based point to check.
 * @param rect The integer-based rectangle to check against.
 * @returns True if the point is within the rectangle; otherwise false.
 */
KINLINE b8 point_in_rect_2di(vec2i point, rect_2di rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width && point.y >= rect.y && point.y <= rect.y + rect.height;
}

/**
 * @brief Indicates if the provided point is within the given circle.
 * @param point The point to check.
 * @param rect The rectangle to check against.
 * @returns True if the point is within the rectangle; otherwise false.
 */
KINLINE b8 point_in_circle_2d(vec2 point, circle_2d circle) {
    f32 r_squared = circle.radius * circle.radius;
    return vec2_distance_squared(point, circle.center) <= r_squared;
}

#endif
