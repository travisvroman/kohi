#pragma once

#include "math/math_types.h"
#include "renderer/renderer_types.inl"

typedef struct viewport {
    /** @brief the dimensions of this viewport, x/y position, z/w are width/height.*/
    rect_2d rect;
    f32 fov;
    f32 near_clip;
    f32 far_clip;
    renderer_projection_matrix_type projection_matrix_type;
    mat4 projection;
} viewport;

KAPI b8 viewport_create(vec4 rect, f32 fov, f32 near_clip, f32 far_clip, renderer_projection_matrix_type projection_matrix_type, viewport* out_viewport);
KAPI void viewport_destroy(viewport* v);

KAPI void viewport_resize(viewport* v, vec4 rect);
