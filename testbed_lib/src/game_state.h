#pragma once

#include <defines.h>
#include <application_types.h>
#include <math/math_types.h>
#include <systems/camera_system.h>

// TODO: temp
#include <resources/skybox.h>
#include <resources/ui_text.h>
#include <core/clock.h>
#include <core/keymap.h>

#include "debug_console.h"

typedef struct game_state {
    f32 delta_time;
    camera* world_camera;

    u16 width, height;

    frustum camera_frustum;

    clock update_clock;
    clock render_clock;
    f64 last_update_elapsed;

    // TODO: temp
    skybox sb;

    mesh meshes[10];
    mesh* car_mesh;
    mesh* sponza_mesh;
    b8 models_loaded;

    mesh ui_meshes[10];
    ui_text test_text;
    ui_text test_sys_text;

    debug_console_state debug_console;

    // The unique identifier of the currently hovered-over object.
    u32 hovered_object_id;

    keymap console_keymap;

    u64 alloc_count;
    u64 prev_alloc_count;
    // TODO: end temp
} game_state;

