#include "camera.h"

#include "math/kmath.h"

camera camera_create(void) {
    camera c;
    camera_reset(&c);
    return c;
}

camera camera_copy(camera source) {
    camera c;
    c.euler_rotation = source.euler_rotation;
    c.position = source.position;
    c.is_dirty = source.is_dirty;
    c.view_matrix = source.view_matrix;
    return c;
}

void camera_reset(camera* c) {
    if (c) {
        c->euler_rotation = vec3_zero();
        c->position = vec3_zero();
        c->is_dirty = false;
        c->view_matrix = mat4_identity();
    }
}

vec3 camera_position_get(const camera* c) {
    if (c) {
        return c->position;
    }
    return vec3_zero();
}

void camera_position_set(camera* c, vec3 position) {
    if (c) {
        c->position = position;
        c->is_dirty = true;
    }
}

vec3 camera_rotation_euler_get(const camera* c) {
    if (c) {
        return c->euler_rotation;
    }
    return vec3_zero();
}

void camera_rotation_euler_set_radians(camera* c, vec3 rotation_radians) {
    if (c) {
        // Convert number passed in to radians.
        c->euler_rotation.x = rotation_radians.x;
        c->euler_rotation.y = rotation_radians.y;
        c->euler_rotation.z = rotation_radians.z;
        c->is_dirty = true;
    }
}

void camera_rotation_euler_set(camera* c, vec3 rotation) {
    if (c) {
        // Convert number passed in to radians.
        c->euler_rotation.x = deg_to_rad(rotation.x);
        c->euler_rotation.y = deg_to_rad(rotation.y);
        c->euler_rotation.z = deg_to_rad(rotation.z);
        c->is_dirty = true;
    }
}

mat4 camera_view_get(camera* c) {
    if (c) {
        if (c->is_dirty) {
            mat4 rotation = mat4_euler_xyz(c->euler_rotation.x, c->euler_rotation.y, c->euler_rotation.z);
            mat4 translation = mat4_translation(c->position);

            c->view_matrix = mat4_mul(rotation, translation);
            c->view_matrix = mat4_inverse(c->view_matrix);

            c->is_dirty = false;
        }
        return c->view_matrix;
    }
    return mat4_identity();
}

vec3 camera_forward(camera* c) {
    if (c) {
        mat4 view = camera_view_get(c);
        return mat4_forward(view);
    }
    return vec3_zero();
}

vec3 camera_backward(camera* c) {
    if (c) {
        mat4 view = camera_view_get(c);
        return mat4_backward(view);
    }
    return vec3_zero();
}

vec3 camera_left(camera* c) {
    if (c) {
        mat4 view = camera_view_get(c);
        return mat4_left(view);
    }
    return vec3_zero();
}

vec3 camera_right(camera* c) {
    if (c) {
        mat4 view = camera_view_get(c);
        return mat4_right(view);
    }
    return vec3_zero();
}

vec3 camera_up(camera* c) {
    if (c) {
        mat4 view = camera_view_get(c);
        return mat4_up(view);
    }
    return vec3_zero();
}

void camera_move_forward(camera* c, f32 amount) {
    if (c) {
        vec3 direction = camera_forward(c);
        direction = vec3_mul_scalar(direction, amount);
        c->position = vec3_add(c->position, direction);
        c->is_dirty = true;
    }
}

void camera_move_backward(camera* c, f32 amount) {
    if (c) {
        vec3 direction = camera_backward(c);
        direction = vec3_mul_scalar(direction, amount);
        c->position = vec3_add(c->position, direction);
        c->is_dirty = true;
    }
}

void camera_move_left(camera* c, f32 amount) {
    if (c) {
        vec3 direction = camera_left(c);
        direction = vec3_mul_scalar(direction, amount);
        c->position = vec3_add(c->position, direction);
        c->is_dirty = true;
    }
}

void camera_move_right(camera* c, f32 amount) {
    if (c) {
        vec3 direction = camera_right(c);
        direction = vec3_mul_scalar(direction, amount);
        c->position = vec3_add(c->position, direction);
        c->is_dirty = true;
    }
}

void camera_move_up(camera* c, f32 amount) {
    if (c) {
        vec3 direction = vec3_up();
        direction = vec3_mul_scalar(direction, amount);
        c->position = vec3_add(c->position, direction);
        c->is_dirty = true;
    }
}

void camera_move_down(camera* c, f32 amount) {
    if (c) {
        vec3 direction = vec3_down();
        direction = vec3_mul_scalar(direction, amount);
        c->position = vec3_add(c->position, direction);
        c->is_dirty = true;
    }
}

void camera_yaw(camera* c, f32 amount) {
    if (c) {
        c->euler_rotation.y += amount;
        c->is_dirty = true;
    }
}

void camera_pitch(camera* c, f32 amount) {
    if (c) {
        c->euler_rotation.x += amount;

        // Clamp to avoid Gimball lock.
        static const f32 limit = 1.55334306f;  // 89 degrees, or equivalent to deg_to_rad(89.0f);
        c->euler_rotation.x = KCLAMP(c->euler_rotation.x, -limit, limit);

        c->is_dirty = true;
    }
}
