#pragma once

// Core
#include <defines.h>
#include <identifiers/khandle.h>
#include <math/math_types.h>
#include <time/kclock.h>

// Runtime
#include <application/application_types.h>
#include <core/keymap.h>
#include <renderer/rendergraph.h>
#include <renderer/viewport.h>
#include <resources/debug/debug_box3d.h>
#include <resources/scene.h>
#include <resources/skybox.h>
#include <systems/camera_system.h>
#include <systems/light_system.h>

// Standard UI plugin
#include <debug_console.h>
#include <standard_ui_system.h>

// Utils plugin
#include <editor/editor_gizmo.h>

#include "audio/audio_frontend.h"
#include "core/engine.h"

struct debug_line3d;
struct debug_box3d;
struct kaudio_system_state;

typedef struct selected_object {
    khandle ktransform_handle;
    khandle node_handle;
    khandle ktransform_parent_handle;
} selected_object;

typedef struct application_state {
    b8 running;
    camera* world_camera;
    struct kaudio_system_state* audio_system;

    // TODO: temp
    camera* world_camera_2;

    u16 width, height;

    frustum camera_frustum;

    kclock update_clock;
    kclock prepare_clock;
    kclock render_clock;
    f64 last_update_elapsed;

    // TODO: temp
    rendergraph forward_graph;
    scene main_scene;
    b8 main_scene_unload_triggered;

    point_light* p_light_1;

    sui_control test_text;
    sui_control test_text_black;
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

    selected_object selection;
    b8 using_gizmo;

    u32 render_mode;

    struct kruntime_plugin* sui_plugin;
    struct standard_ui_plugin_state* sui_plugin_state;
    struct standard_ui_state* sui_state;

    struct sui_control test_panel;
    struct sui_control test_button;

    kaudio_instance test_sound;
    kaudio_instance test_music;

    u32 proj_box_index;
    u32 cam_proj_line_indices[24];

    // TODO: end temp
} application_state;

typedef struct testbed_application_frame_data {
    i32 dummy;
} testbed_application_frame_data;
