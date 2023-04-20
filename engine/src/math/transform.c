#include "transform.h"

#include "kmath.h"

transform transform_create(void) {
    transform t;
    transform_position_rotation_scale_set(&t, vec3_zero(), quat_identity(), vec3_one());
    t.local = mat4_identity();
    t.parent = 0;
    return t;
}

transform transform_from_position(vec3 position) {
    transform t;
    transform_position_rotation_scale_set(&t, position, quat_identity(), vec3_one());
    t.local = mat4_identity();
    t.parent = 0;
    return t;
}

transform transform_from_rotation(quat rotation) {
    transform t;
    transform_position_rotation_scale_set(&t, vec3_zero(), rotation, vec3_one());
    t.local = mat4_identity();
    t.parent = 0;
    return t;
}

transform transform_from_position_rotation(vec3 position, quat rotation) {
    transform t;
    transform_position_rotation_scale_set(&t, position, rotation, vec3_one());
    t.local = mat4_identity();
    t.parent = 0;
    return t;
}

transform transform_from_position_rotation_scale(vec3 position, quat rotation, vec3 scale) {
    transform t;
    transform_position_rotation_scale_set(&t, position, rotation, scale);
    t.local = mat4_identity();
    t.parent = 0;
    return t;
}

transform* transform_parent_get(transform* t) {
    if (!t) {
        return 0;
    }
    return t->parent;
}

void transform_parent_set(transform* t, transform* parent) {
    if (t) {
        t->parent = parent;
    }
}

vec3 transform_position_get(const transform* t) {
    return t->position;
}

void transform_position_set(transform* t, vec3 position) {
    t->position = position;
    t->is_dirty = true;
}

void transform_translate(transform* t, vec3 translation) {
    t->position = vec3_add(t->position, translation);
    t->is_dirty = true;
}

quat transform_rotation_get(const transform* t) {
    return t->rotation;
}

void transform_rotation_set(transform* t, quat rotation) {
    t->rotation = rotation;
    t->is_dirty = true;
}

void transform_rotate(transform* t, quat rotation) {
    t->rotation = quat_mul(t->rotation, rotation);
    t->is_dirty = true;
}

vec3 transform_scale_get(const transform* t) {
    return t->scale;
}

void transform_scale_set(transform* t, vec3 scale) {
    t->scale = scale;
    t->is_dirty = true;
}

void transform_scale(transform* t, vec3 scale) {
    t->scale = vec3_mul(t->scale, scale);
    t->is_dirty = true;
}

void transform_position_rotation_set(transform* t, vec3 position, quat rotation) {
    t->position = position;
    t->rotation = rotation;
    t->is_dirty = true;
}

void transform_position_rotation_scale_set(transform* t, vec3 position, quat rotation, vec3 scale) {
    t->position = position;
    t->rotation = rotation;
    t->scale = scale;
    t->is_dirty = true;
}

void transform_translate_rotate(transform* t, vec3 translation, quat rotation) {
    t->position = vec3_add(t->position, translation);
    t->rotation = quat_mul(t->rotation, rotation);
    t->is_dirty = true;
}

mat4 transform_local_get(transform* t) {
    if (t) {
        if (t->is_dirty) {
            mat4 tr = mat4_mul(quat_to_mat4(t->rotation), mat4_translation(t->position));
            tr = mat4_mul(mat4_scale(t->scale), tr);
            t->local = tr;
            t->is_dirty = false;
        }

        return t->local;
    }
    return mat4_identity();
}

mat4 transform_world_get(transform* t) {
    if (t) {
        mat4 l = transform_local_get(t);
        if (t->parent) {
            mat4 p = transform_world_get(t->parent);
            return mat4_mul(l, p);
        }
        return l;
    }
    return mat4_identity();
}
