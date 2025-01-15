#include "game_keybinds.h"

#include <application/application_types.h>
#include <core/console.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core/input.h>
#include <core/keymap.h>
#include <core/kvar.h>
#include <core/systems_manager.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/timeline_system.h>

#if KOHI_DEBUG
#    include "debug_console.h"
#endif
#include "game_state.h"
#include "identifiers/khandle.h"
#include "renderer/renderer_types.h"

static f32 get_engine_delta_time(void) {
    khandle engine = timeline_system_get_engine();
    return timeline_system_delta_get(engine);
}

void game_on_escape_callback(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    KDEBUG("game_on_escape_callback");
    event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
}

void game_on_yaw(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    f32 f = 0.0f;
    if (key == KEY_LEFT || key == KEY_A) {
        f = 1.0f;
    } else if (key == KEY_RIGHT || key == KEY_D) {
        f = -1.0f;
    }

    camera_yaw(state->world_camera, f * get_engine_delta_time());
}

void game_on_pitch(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    f32 f = 0.0f;
    if (key == KEY_UP) {
        f = 1.0f;
    } else if (key == KEY_DOWN) {
        f = -1.0f;
    }

    camera_pitch(state->world_camera, f * get_engine_delta_time());
}

void game_on_move_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    camera_move_forward(state->world_camera, state->forward_move_speed * get_engine_delta_time());
}

void game_on_move_backward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    camera_move_backward(state->world_camera, state->backward_move_speed * get_engine_delta_time());
}

void game_on_move_left(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    camera_move_left(state->world_camera, state->forward_move_speed * get_engine_delta_time());
}

void game_on_move_right(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    camera_move_right(state->world_camera, state->forward_move_speed * get_engine_delta_time());
}

void game_on_move_up(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    camera_move_up(state->world_camera, state->forward_move_speed * get_engine_delta_time());
}

void game_on_move_down(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    camera_move_down(state->world_camera, state->forward_move_speed * get_engine_delta_time());
}

void game_on_console_change_visibility(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    // No-op unless a debug build
#if KOHI_DEBUG
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    b8 console_visible = debug_console_visible(&state->debug_console);
    console_visible = !console_visible;

    debug_console_visible_set(&state->debug_console, console_visible);
    if (console_visible) {
        input_keymap_push(&state->console_keymap);
    } else {
        input_keymap_pop();
    }
#endif
}

void game_on_set_render_mode_default(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_DEFAULT;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (application*)user_data, data);
}

void game_on_set_render_mode_lighting(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_LIGHTING;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (application*)user_data, data);
}

void game_on_set_render_mode_normals(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_NORMALS;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (application*)user_data, data);
}

void game_on_set_render_mode_cascades(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_CASCADES;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (application*)user_data, data);
}

void game_on_set_render_mode_wireframe(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_WIREFRAME;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (application*)user_data, data);
}

void game_on_set_gizmo_mode(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    editor_gizmo_mode mode;
    switch (key) {
    case KEY_1:
    default:
        mode = EDITOR_GIZMO_MODE_NONE;
        break;
    case KEY_2:
        mode = EDITOR_GIZMO_MODE_MOVE;
        break;
    case KEY_3:
        mode = EDITOR_GIZMO_MODE_ROTATE;
        break;
    case KEY_4:
        mode = EDITOR_GIZMO_MODE_SCALE;
        break;
    }
    editor_gizmo_mode_set(&state->gizmo, mode);
}

void game_on_gizmo_orientation_set(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    editor_gizmo_orientation orientation = editor_gizmo_orientation_get(&state->gizmo);
    orientation++;
    if (orientation > EDITOR_GIZMO_ORIENTATION_MAX) {
        orientation = 0;
    }
    editor_gizmo_orientation_set(&state->gizmo, orientation);
}

void game_on_load_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_fire(EVENT_CODE_DEBUG1, (application*)user_data, (event_context){});
}

void game_on_save_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_fire(EVENT_CODE_DEBUG5, (application*)user_data, (event_context){});
}

void game_on_unload_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_fire(EVENT_CODE_DEBUG2, (application*)user_data, (event_context){});
}

void game_on_play_sound(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_fire(EVENT_CODE_DEBUG3, (application*)user_data, (event_context){});
}
void game_on_toggle_sound(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_fire(EVENT_CODE_DEBUG4, (application*)user_data, (event_context){});
}

void game_on_console_scroll(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
// No-op unless a debug build.
#if KOHI_DEBUG
    application* app = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)app->state;
    debug_console_state* console_state = &state->debug_console;
    if (key == KEY_PAGEUP) {
        debug_console_move_up(console_state);
    } else if (key == KEY_PAGEDOWN) {
        debug_console_move_down(console_state);
    }
#endif
}

void game_on_console_scroll_hold(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    // No-op unless a debug build.
#if KOHI_DEBUG
    application* app = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)app->state;
    debug_console_state* console_state = &state->debug_console;

    static f32 accumulated_time = 0.0f;
    accumulated_time += get_engine_delta_time();

    if (accumulated_time >= 0.1f) {
        if (key == KEY_PAGEUP) {
            debug_console_move_up(console_state);
        } else if (key == KEY_PAGEDOWN) {
            debug_console_move_down(console_state);
        }
        accumulated_time = 0.0f;
    }
#endif
}

void game_on_console_history_back(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
// No-op unless a debug build.
#if KOHI_DEBUG
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    debug_console_history_back(&state->debug_console);
#endif
}

void game_on_console_history_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    // No-op unless a debug build.
#if KOHI_DEBUG
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;
    debug_console_history_forward(&state->debug_console);
#endif
}

void game_on_debug_texture_swap(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    KDEBUG("Swapping texture!");
    event_context context = {};
    event_fire(EVENT_CODE_DEBUG0, user_data, context);
}

void game_on_debug_cam_position(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    KINFO(
        "Pos:[%.2f, %.2f, %.2f",
        state->world_camera->position.x,
        state->world_camera->position.y,
        state->world_camera->position.z);
}

void game_on_debug_vsync_toggle(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    char cmd[30];
    string_ncopy(cmd, "kvar_set_int vsync 0", 29);
    b8 vsync_enabled = renderer_flag_enabled_get(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT);
    u32 length = string_length(cmd);
    cmd[length - 1] = vsync_enabled ? '1' : '0';
    console_command_execute(cmd);
}

void game_print_memory_metrics(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    application* game_inst = (application*)user_data;
    testbed_game_state* state = (testbed_game_state*)game_inst->state;

    char* usage = get_memory_usage_str();
    KINFO(usage);
    string_free(usage);
    KINFO("Allocations: %llu (%llu this frame)", state->alloc_count, state->alloc_count - state->prev_alloc_count);
}

void game_setup_keymaps(application* game_inst) {
    // Global keymap
    keymap global_keymap = keymap_create();
    keymap_binding_add(&global_keymap, KEY_ESCAPE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_escape_callback);

    input_keymap_push(&global_keymap);

    // Testbed keymap
    keymap testbed_keymap = keymap_create();
    keymap_binding_add(&testbed_keymap, KEY_A, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_yaw);
    keymap_binding_add(&testbed_keymap, KEY_LEFT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_yaw);

    keymap_binding_add(&testbed_keymap, KEY_D, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_yaw);
    keymap_binding_add(&testbed_keymap, KEY_RIGHT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_yaw);

    keymap_binding_add(&testbed_keymap, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_pitch);
    keymap_binding_add(&testbed_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_pitch);

    keymap_binding_add(&testbed_keymap, KEY_GRAVE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_change_visibility);

    keymap_binding_add(&testbed_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_move_forward);
    keymap_binding_add(&testbed_keymap, KEY_S, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_move_backward);
    keymap_binding_add(&testbed_keymap, KEY_Q, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_move_left);
    keymap_binding_add(&testbed_keymap, KEY_E, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_move_right);
    keymap_binding_add(&testbed_keymap, KEY_SPACE, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_move_up);
    keymap_binding_add(&testbed_keymap, KEY_X, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_move_down);

    keymap_binding_add(&testbed_keymap, KEY_0, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, game_inst, game_on_set_render_mode_default);
    keymap_binding_add(&testbed_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, game_inst, game_on_set_render_mode_lighting);
    keymap_binding_add(&testbed_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, game_inst, game_on_set_render_mode_normals);
    keymap_binding_add(&testbed_keymap, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, game_inst, game_on_set_render_mode_cascades);
    keymap_binding_add(&testbed_keymap, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, game_inst, game_on_set_render_mode_wireframe);

    keymap_binding_add(&testbed_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_gizmo_mode);
    keymap_binding_add(&testbed_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_gizmo_mode);
    keymap_binding_add(&testbed_keymap, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_gizmo_mode);
    keymap_binding_add(&testbed_keymap, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_gizmo_mode);
    keymap_binding_add(&testbed_keymap, KEY_G, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_gizmo_orientation_set);

    keymap_binding_add(&testbed_keymap, KEY_L, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_load_scene);
    keymap_binding_add(&testbed_keymap, KEY_U, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_unload_scene);
    // ctrl s
    keymap_binding_add(&testbed_keymap, KEY_S, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, game_inst, game_on_save_scene);

    keymap_binding_add(&testbed_keymap, KEY_F, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_play_sound);
    keymap_binding_add(&testbed_keymap, KEY_R, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_toggle_sound);

    keymap_binding_add(&testbed_keymap, KEY_T, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_debug_texture_swap);
    keymap_binding_add(&testbed_keymap, KEY_P, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_debug_cam_position);
    keymap_binding_add(&testbed_keymap, KEY_V, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_debug_vsync_toggle);
    keymap_binding_add(&testbed_keymap, KEY_M, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_print_memory_metrics);

    input_keymap_push(&testbed_keymap);

    // A console-specific keymap. Is not pushed by default.
    testbed_game_state* state = ((testbed_game_state*)game_inst->state);
    state->console_keymap = keymap_create();
    state->console_keymap.overrides_all = true;
    keymap_binding_add(&state->console_keymap, KEY_GRAVE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_change_visibility);
    keymap_binding_add(&state->console_keymap, KEY_ESCAPE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_change_visibility);

    keymap_binding_add(&state->console_keymap, KEY_PAGEUP, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll);
    keymap_binding_add(&state->console_keymap, KEY_PAGEDOWN, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll);
    keymap_binding_add(&state->console_keymap, KEY_PAGEUP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll_hold);
    keymap_binding_add(&state->console_keymap, KEY_PAGEDOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll_hold);

    keymap_binding_add(&state->console_keymap, KEY_UP, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_history_back);
    keymap_binding_add(&state->console_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_history_forward);

// If this was done with the console open, push its keymap.
#if KOHI_DEBUG
    b8 console_visible = debug_console_visible(&state->debug_console);
    if (console_visible) {
        input_keymap_push(&state->console_keymap);
    }
#endif
}

void game_remove_keymaps(struct application* game_inst) {
    // Pop all keymaps
    while (input_keymap_pop()) {
    }

    testbed_game_state* state = ((testbed_game_state*)game_inst->state);

    // Remove all bindings for the console keymap, since that's the only one we hold onto.
    keymap_clear(&state->console_keymap);
}
