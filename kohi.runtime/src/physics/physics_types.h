#pragma once

#include "identifiers/khandle.h"
#include "math/math_types.h"
#include "strings/kname.h"

struct kphysics_body;

typedef struct kphysics_world {
    kname name;
    vec3 gravity;

    // darray of handles to physics bodies
    khandle* bodies;

} kphysics_world;

typedef struct kphysics_system_config {
    u32 steps_per_frame;
} kphysics_system_config;
