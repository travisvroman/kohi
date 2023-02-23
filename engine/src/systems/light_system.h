#pragma once

#include "defines.h"

#include "math/math_types.h"

typedef struct directional_light {
    vec4 colour;
    vec4 direction;  // ignore w
} directional_light;

typedef struct point_light {
    vec4 colour;
    vec4 position;  // ignore w
    // Usually 1, make sure denominator never gets smaller than 1
    f32 constant_f;
    // Reduces light intensity linearly
    f32 linear;
    // Makes the light fall off slower at longer distances.
    f32 quadratic;
    f32 padding;
} point_light;

b8 light_system_initialize(u64* memory_requirement, void* memory, void* config);
void light_system_shutdown(void* state);

KAPI b8 light_system_add_directional(directional_light* light);
KAPI b8 light_system_add_point(point_light* light);

KAPI b8 light_system_remove_directional(directional_light* light);
KAPI b8 light_system_remove_point(point_light* light);

KAPI directional_light* light_system_directional_light_get();

KAPI i32 light_system_point_light_count();
KAPI b8 light_system_point_lights_get(point_light* p_lights);
