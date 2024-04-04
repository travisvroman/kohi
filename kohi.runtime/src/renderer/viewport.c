#include "viewport.h"

#include "kmemory.h"
#include "logger.h"
#include "math/kmath.h"
#include "renderer/renderer_types.h"

static void regenerate_projection_matrix(viewport* v) {
    if (v) {
        if (v->projection_matrix_type == RENDERER_PROJECTION_MATRIX_TYPE_PERSPECTIVE) {
            v->projection = mat4_perspective(v->fov, v->rect.width / v->rect.height, v->near_clip, v->far_clip);
        } else if (v->projection_matrix_type == RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC) {
            // NOTE: may need to reverse y/w
            v->projection = mat4_orthographic(v->rect.x, v->rect.width, v->rect.height, v->rect.y, v->near_clip, v->far_clip);
        } else if (v->projection_matrix_type == RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC_CENTERED) {
            f32 mod = v->fov;
            v->projection = mat4_orthographic(-v->rect.width * mod, v->rect.width * mod, -v->rect.height * mod, v->rect.height * mod, v->near_clip, v->far_clip);
        } else {
            KERROR("Regenerating default perspect projection matrix, as an invalid type was specified.");
            v->projection = mat4_perspective(v->fov, v->rect.z / v->rect.w, v->near_clip, v->far_clip);
        }
    }
}

b8 viewport_create(vec4 rect, f32 fov, f32 near_clip, f32 far_clip, renderer_projection_matrix_type projection_matrix_type, viewport* out_viewport) {
    if (!out_viewport) {
        return false;
    }

    out_viewport->rect = rect;
    out_viewport->fov = fov;
    out_viewport->near_clip = near_clip;
    out_viewport->far_clip = far_clip;
    out_viewport->projection_matrix_type = projection_matrix_type;
    if (projection_matrix_type == RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC_CENTERED && fov == 0) {
        KWARN("viewport_create is using a centered orthographic type with a fov of 0. FOV should be non-zero.");
    }
    regenerate_projection_matrix(out_viewport);

    return true;
}
void viewport_destroy(viewport* v) {
    if (v) {
        kzero_memory(v, sizeof(viewport));
    }
}

void viewport_resize(viewport* v, vec4 rect) {
    if (v) {
        v->rect = rect;
        regenerate_projection_matrix(v);
    }
}
