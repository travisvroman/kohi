/**
 * @file math_types.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Contains various math types required for the engine.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"

/**
 * @brief A 2-element vector.
 */
typedef union vec2_u {
    /** @brief An array of x, y */
    f32 elements[2];
    struct {
        union {
            /** @brief The first element. */
            f32 x,
                /** @brief The first element. */
                r,
                /** @brief The first element. */
                s,
                /** @brief The first element. */
                u;
        };
        union {
            /** @brief The second element. */
            f32 y,
                /** @brief The second element. */
                g,
                /** @brief The second element. */
                t,
                /** @brief The second element. */
                v;
        };
    };
} vec2;

/**
 * @brief A 3-element vector.
 */
typedef union vec3_u {
    /** @brief An array of x, y, z */
    f32 elements[3];
    struct {
        union {
            /** @brief The first element. */
            f32 x,
                /** @brief The first element. */
                r,
                /** @brief The first element. */
                s,
                /** @brief The first element. */
                u;
        };
        union {
            /** @brief The second element. */
            f32 y,
                /** @brief The second element. */
                g,
                /** @brief The second element. */
                t,
                /** @brief The second element. */
                v;
        };
        union {
            /** @brief The third element. */
            f32 z,
                /** @brief The third element. */
                b,
                /** @brief The third element. */
                p,
                /** @brief The third element. */
                w;
        };
    };
} vec3;

/**
 * @brief A 4-element vector.
 */
typedef union vec4_u {
    /** @brief An array of x, y, z, w */
    f32 elements[4];
    union {
        struct {
            union {
                /** @brief The first element. */
                f32 x,
                    /** @brief The first element. */
                    r,
                    /** @brief The first element. */
                    s;
            };
            union {
                /** @brief The second element. */
                f32 y,
                    /** @brief The third element. */
                    g,
                    /** @brief The third element. */
                    t;
            };
            union {
                /** @brief The third element. */
                f32 z,
                    /** @brief The third element. */
                    b,
                    /** @brief The third element. */
                    p,
                    /** @brief The third element. */
                    width;
            };
            union {
                /** @brief The fourth element. */
                f32 w,
                    /** @brief The fourth element. */
                    a,
                    /** @brief The fourth element. */
                    q,
                    /** @brief The fourth element. */
                    height;
            };
        };
    };
} vec4;

/** @brief A quaternion, used to represent rotational orientation. */
typedef vec4 quat;

/** @brief A 2d rectangle. */
typedef vec4 rect_2d;

/**
 * @brief A 4-element vector of unsigned ints.
 */
typedef union uvec4_u {
    /** @brief An array of x, y, z, w */
    u32 elements[4];
    union {
        struct {
            union {
                /** @brief The first element. */
                u32 x,
                    /** @brief The first element. */
                    r,
                    /** @brief The first element. */
                    s;
            };
            union {
                /** @brief The second element. */
                u32 y,
                    /** @brief The third element. */
                    g,
                    /** @brief The third element. */
                    t;
            };
            union {
                /** @brief The third element. */
                u32 z,
                    /** @brief The third element. */
                    b,
                    /** @brief The third element. */
                    p,
                    /** @brief The third element. */
                    width;
            };
            union {
                /** @brief The fourth element. */
                u32 w,
                    /** @brief The fourth element. */
                    a,
                    /** @brief The fourth element. */
                    q,
                    /** @brief The fourth element. */
                    height;
            };
        };
    };
} uvec4;

/** @brief A 3x3 matrix */
typedef union mat3_u {
    /** @brief The matrix elements. */
    f32 data[12];
} mat3;

/** @brief a 4x4 matrix, typically used to represent object transformations. */
typedef union mat4_u {
    /** @brief The matrix elements */
    f32 data[16];
} mat4;

/**
 * @brief Represents the extents of a 2d object.
 */
typedef struct extents_2d {
    /** @brief The minimum extents of the object. */
    vec2 min;
    /** @brief The maximum extents of the object. */
    vec2 max;
} extents_2d;

/**
 * @brief Represents the extents of a 3d object.
 */
typedef struct extents_3d {
    /** @brief The minimum extents of the object. */
    vec3 min;
    /** @brief The maximum extents of the object. */
    vec3 max;
} extents_3d;

/**
 * @brief Represents a single vertex in 3D space.
 */
typedef struct vertex_3d {
    /** @brief The position of the vertex */
    vec3 position;
    /** @brief The normal of the vertex. */
    vec3 normal;
    /** @brief The texture coordinate of the vertex. */
    vec2 texcoord;
    /** @brief The colour of the vertex. */
    vec4 colour;
    /** @brief The tangent of the vertex. */
    vec4 tangent;
} vertex_3d;

/**
 * @brief Represents a single vertex in 2D space.
 */
typedef struct vertex_2d {
    /** @brief The position of the vertex */
    vec2 position;
    /** @brief The texture coordinate of the vertex. */
    vec2 texcoord;
} vertex_2d;

/**
 * @brief Represents a single vertex in 3D space with position and colour data only.
 */
typedef struct colour_vertex_3d {
    /** @brief The position of the vertex. w is ignored. */
    vec4 position;
    /** @brief The colour of the vertex. */
    vec4 colour;
} colour_vertex_3d;

typedef struct plane_3d {
    vec3 normal;
    f32 distance;
} plane_3d;

#define FRUSTUM_SIDE_COUNT 6

typedef enum frustum_side {
    FRUSTUM_SIDE_TOP = 0,
    FRUSTUM_SIDE_BOTTOM = 1,
    FRUSTUM_SIDE_RIGHT = 2,
    FRUSTUM_SIDE_LEFT = 3,
    FRUSTUM_SIDE_FAR = 4,
    FRUSTUM_SIDE_NEAR = 5,
} frustum_side;

typedef struct frustum {
    // Top, bottom, right, left, far, near
    plane_3d sides[FRUSTUM_SIDE_COUNT];
} frustum;

/**
 * @brief A 2-element integer-based vector.
 */
typedef union vec2i_t {
    /** @brief An array of x, y */
    i32 elements[2];
    struct {
        union {
            /** @brief The first element. */
            i32 x,
                /** @brief The first element. */
                r,
                /** @brief The first element. */
                s,
                /** @brief The first element. */
                u;
        };
        union {
            /** @brief The second element. */
            i32 y,
                /** @brief The second element. */
                g,
                /** @brief The second element. */
                t,
                /** @brief The second element. */
                v;
        };
    };
} vec2i;

/**
 * @brief A 4-element integer-based vector.
 */
typedef union vec4i_t {
    /** @brief An array of x, y, z, w */
    i32 elements[4];
    union {
        struct {
            union {
                /** @brief The first element. */
                i32 x,
                    /** @brief The first element. */
                    r,
                    /** @brief The first element. */
                    s;
            };
            union {
                /** @brief The second element. */
                i32 y,
                    /** @brief The third element. */
                    g,
                    /** @brief The third element. */
                    t;
            };
            union {
                /** @brief The third element. */
                i32 z,
                    /** @brief The third element. */
                    b,
                    /** @brief The third element. */
                    p,
                    /** @brief The third element. */
                    width;
            };
            union {
                /** @brief The fourth element. */
                i32 w,
                    /** @brief The fourth element. */
                    a,
                    /** @brief The fourth element. */
                    q,
                    /** @brief The fourth element. */
                    height;
            };
        };
    };
} vec4i;

typedef struct triangle_2d {
    vec2 verts[3];
} triangle_2d;

typedef struct triangle_3d {
    vec3 verts[3];
} triangle_3d;

typedef struct oriented_bounding_box {
    vec3 center;
    vec3 half_extents;
    quat rotation;
} oriented_bounding_box;
