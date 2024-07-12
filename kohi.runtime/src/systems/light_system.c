#include "light_system.h"

#include "core/engine.h"
#include "logger.h"

#define MAX_POINT_LIGHTS 10

typedef struct light_system_state {
    directional_light* dir_light;
    point_light* p_lights[MAX_POINT_LIGHTS];
} light_system_state;

b8 light_system_initialize(u64* memory_requirement, void* memory, void* config) {
    *memory_requirement = sizeof(light_system_state);
    if (!memory) {
        return true;
    }

    // NOTE: perform config/init here.
    return true;
}

void light_system_shutdown(void* state) {
    if (state) {
        // NOTE: perform teardown here.
    }
}

b8 light_system_directional_add(directional_light* light) {
    if (!light) {
        return false;
    }
    light_system_state* state = engine_systems_get()->light_system;
    state->dir_light = light;
    return true;
}

b8 light_system_point_add(point_light* light) {
    if (!light) {
        return false;
    }
    light_system_state* state = engine_systems_get()->light_system;
    for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (!state->p_lights[i]) {
            state->p_lights[i] = light;
            return true;
        }
    }

    // We're full, so fail.
    KERROR("light_system_add_point already has the max of %u lights. Light not added.", MAX_POINT_LIGHTS);
    return false;
}

b8 light_system_directional_remove(directional_light* light) {
    if (!light) {
        return false;
    }
    light_system_state* state = engine_systems_get()->light_system;
    if (state->dir_light == light) {
        state->dir_light = 0;
        return true;
    }

    return false;
}

b8 light_system_point_remove(point_light* light) {
    if (!light) {
        return false;
    }
    light_system_state* state = engine_systems_get()->light_system;
    for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (state->p_lights[i] == light) {
            state->p_lights[i] = 0;
            return true;
        }
    }

    KERROR("light_system_remove_point does not have a light that matches the one passed, thus it cannot be removed.");
    return false;
}

directional_light* light_system_directional_light_get(void) {
    light_system_state* state = engine_systems_get()->light_system;
    return state->dir_light;
}

u32 light_system_point_light_count(void) {
    light_system_state* state = engine_systems_get()->light_system;
    i32 count = 0;
    for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (state->p_lights[i]) {
            count++;
        }
    }

    return count;
}

b8 light_system_point_lights_get(point_light* p_lights) {
    if (!p_lights) {
        return false;
    }
    light_system_state* state = engine_systems_get()->light_system;
    for (u32 i = 0, j = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (state->p_lights[i]) {
            p_lights[j] = *(state->p_lights[i]);
            j++;
        }
    }

    return true;
}
