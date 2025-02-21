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
#include "systems/zone_system.h"
#include "time/kclock.h"

#define PACKAGE_NAME_SOI "SOI"

typedef enum game_mode {
    GAME_MODE_WORLD,
    GAME_MODE_EDITOR,
    MAIN_MENU,
    PAUSED_MENU
} game_mode;

// User-defined codes to be used with the event system.
typedef enum game_event_code {
    // Start of the User-defined code range. Not an actual used code.
    GAME_EVENT_CODE_START = 0x00FF,
    /**
     * @brief An event fired when zone has been loaded. A pointer to the
     * zone is included as the sender.
     *
     * Context usage:
     * u8 spawn_point_id = context.data.u8[0]
     */
    GAME_EVENT_CODE_ZONE_LOADED = 0x0100
} game_event_code;

typedef struct game_state {
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

    u16 width, height;

    struct kaudio_system_state* audio_system;
    struct kruntime_plugin* sui_plugin;
    struct standard_ui_plugin_state* sui_plugin_state;
    struct standard_ui_state* sui_state;
    zone_system_state zone_state;

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

    // HACK: Gameplay stuff
    khandle player_xform;
    khandle player_mesh_xform;
} game_state;

typedef struct game_frame_data {
    i32 dummy;
} game_frame_data;
