#pragma once

#include "core/keymap.h"
#include "debug_console.h"
#include "defines.h"
#include "editor/editor_gizmo.h"
#include "renderer/camera.h"
#include "renderer/rendergraph.h"
#include "renderer/viewport.h"
#include "resources/scene.h"
#include "standard_ui_system.h"
#include "time/kclock.h"

typedef enum game_mode {
    GAME_MODE_WORLD,
    GAME_MODE_EDITOR,
    MAIN_MENU,
    PAUSED_MENU
} game_mode;

typedef struct overdrive2069_game_state {
    b8 running;
    camera* vehicle_camera;
    camera* cutscene_camera;
    camera* editor_camera;

    keymap global_keymap;
    keymap world_keymap;
    keymap editor_keymap;
    keymap console_keymap;

    camera* current_camera;
    // The current mode of the game, which controls input, etc.
    game_mode mode;

    scene track_scene;

    u16 width, height;

    struct kaudio_system_state* audio_system;
    struct kruntime_plugin* sui_plugin;
    struct standard_ui_plugin_state* sui_plugin_state;
    struct standard_ui_state* sui_state;

    kclock update_clock;
    kclock prepare_clock;
    kclock render_clock;
    f64 last_update_elapsed;

    rendergraph forward_graph;

    viewport world_viewport;
    viewport ui_viewport;

    u32 render_mode;

    // HACK: Debug stuff to eventually be excluded on release builds.
    sui_control debug_text;
    sui_control debug_text_shadow;
    debug_console_state debug_console;
    editor_gizmo gizmo;
    f32 editor_camera_forward_move_speed;
    f32 editor_camera_backward_move_speed;
} overdrive2069_game_state;

typedef struct overdrive_2069_frame_data {
    i32 dummy;
} overdrive_2069_frame_data;
