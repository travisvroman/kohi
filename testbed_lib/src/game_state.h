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
#include <systems/light_system.h>
#include <resources/simple_scene.h>

#include "debug_console.h"

typedef struct testbed_game_state {
    camera* world_camera;

    u16 width, height;

    frustum camera_frustum;

    clock update_clock;
    clock render_clock;
    f64 last_update_elapsed;

    // TODO: temp
    simple_scene main_scene;
    b8 main_scene_unload_triggered;
    skybox sb;

    mesh meshes[10];
    b8 models_loaded;

    directional_light dir_light;
    point_light p_lights[3];

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
} testbed_game_state;

typedef struct testbed_application_frame_data {
    i32 dummy;
} testbed_application_frame_data;