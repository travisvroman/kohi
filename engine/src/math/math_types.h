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
    vec3 tangent;
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

/**
 * @brief Represents the transform of an object in the world.
 * Transforms can have a parent whose own transform is then
 * taken into account. NOTE: The properties of this should not
 * be edited directly, but done via the functions in transform.h
 * to ensure proper matrix generation.
 */
typedef struct transform {
    /** @brief The position in the world. */
    vec3 position;
    /** @brief The rotation in the world. */
    quat rotation;
    /** @brief The scale in the world. */
    vec3 scale;
    /**
     * @brief Indicates if the position, rotation or scale have changed,
     * indicating that the local matrix needs to be recalculated.
     */
    b8 is_dirty;
    /**
     * @brief The local transformation matrix, updated whenever
     * the position, rotation or scale have changed.
     */
    mat4 local;

    f32 determinant;

    /** @brief A pointer to a parent transform if one is assigned. Can also be null. */
    struct transform* parent;
} transform;

typedef struct plane_3d {
    vec3 normal;
    f32 distance;
} plane_3d;

typedef struct frustum {
    // Top, bottom, right, left, far, near
    plane_3d sides[6];
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
