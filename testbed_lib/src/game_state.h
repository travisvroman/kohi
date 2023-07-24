#pragma once

#include <application_types.h>
#include <defines.h>
#include <math/math_types.h>
#include <systems/camera_system.h>

#include "editor/editor_gizmo.h"
#include "resources/simple_scene.h"

// TODO: temp
#include <core/clock.h>
#include <core/keymap.h>
#include <resources/skybox.h>
#include <resources/ui_text.h>
#include <systems/light_system.h>

#include "debug_console.h"

typedef struct testbed_game_state {
    b8 running;
    camera* world_camera;

    u16 width, height;

    frustum camera_frustum;

    clock update_clock;
    clock render_clock;
    f64 last_update_elapsed;

    // TODO: temp
    simple_scene main_scene;
    b8 main_scene_unload_triggered;

    mesh meshes[10];

    point_light* p_light_1;

    mesh ui_meshes[10];
    ui_text test_text;
    ui_text test_sys_text;

    debug_console_state debug_console;

    // The unique identifier of the currently hovered-over object.
    u32 hovered_object_id;

    keymap console_keymap;

    u64 alloc_count;
    u64 prev_alloc_count;

    f32 forward_move_speed;
    f32 backward_move_speed;

    editor_gizmo gizmo;
    // TODO: end temp
} testbed_game_state;

typedef struct testbed_application_frame_data {
    i32 dummy;
} testbed_application_frame_data;
