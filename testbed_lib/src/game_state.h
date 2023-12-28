#pragma once

#include <application_types.h>
#include <defines.h>
#include <math/math_types.h>
#include <renderer/rendergraph.h>
#include <systems/camera_system.h>

#include "audio/audio_types.h"
#include "editor/editor_gizmo.h"
#include "renderer/viewport.h"
#include "resources/simple_scene.h"

// TODO: temp
#include <core/kclock.h>
#include <core/keymap.h>
#include <resources/debug/debug_box3d.h>
#include <resources/skybox.h>
#include <standard_ui_system.h>
#include <systems/light_system.h>

#include "debug_console.h"
struct debug_line3d;
struct debug_box3d;
struct transform;

#define MAX_SHADOW_CASCADE_COUNT 4

typedef struct selected_object {
    u32 unique_id;
    struct transform* xform;
} selected_object;

typedef struct testbed_game_state {
    b8 running;
    camera* world_camera;

    // TODO: temp
    camera* world_camera_2;

    u16 width, height;

    frustum camera_frustum;

    kclock update_clock;
    kclock prepare_clock;
    kclock render_clock;
    kclock present_clock;
    f64 last_update_elapsed;

    // TODO: temp
    simple_scene main_scene;
    b8 main_scene_unload_triggered;

    mesh meshes[10];

    point_light* p_light_1;

    mesh ui_meshes[10];
    sui_control test_text;
    sui_control test_sys_text;

    debug_console_state debug_console;

    // The unique identifier of the currently hovered-over object.
    u32 hovered_object_id;

    keymap console_keymap;

    u64 alloc_count;
    u64 prev_alloc_count;

    f32 forward_move_speed;
    f32 backward_move_speed;

    editor_gizmo gizmo;

    // Used for visualization of our casts/collisions.
    struct debug_line3d* test_lines;
    struct debug_box3d* test_boxes;

    viewport world_viewport;
    viewport ui_viewport;

    viewport world_viewport2;

    rendergraph frame_graph;
    rendergraph_pass skybox_pass;
    rendergraph_pass shadowmap_pass;
    rendergraph_pass scene_pass;
    rendergraph_pass editor_pass;
    rendergraph_pass ui_pass;

    u16 shadowmap_resolution;

    selected_object selection;
    b8 using_gizmo;

    u32 render_mode;

    struct sui_control test_panel;
    struct sui_control test_button;

    struct audio_file* test_audio_file;
    struct audio_file* test_loop_audio_file;
    struct audio_file* test_music;
    audio_emitter test_emitter;

    u32 proj_box_index;
    u32 cam_proj_line_indices[24];

    // TODO: end temp
} testbed_game_state;

typedef struct testbed_application_frame_data {
    i32 dummy;
} testbed_application_frame_data;
