#include "game_keybinds.h"

#include <core/event.h>
#include <core/logger.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <renderer/renderer_frontend.h>
#include "debug_console.h"

void game_on_escape_callback(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    KDEBUG("game_on_escape_callback");
    event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
}

void game_on_yaw(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;

    f32 f = 0.0f;
    if (key == KEY_LEFT || key == KEY_A) {
        f = 1.0f;
    } else if (key == KEY_RIGHT || key == KEY_D) {
        f = -1.0f;
    }
    camera_yaw(state->world_camera, f * state->delta_time);
}

void game_on_pitch(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;

    f32 f = 0.0f;
    if (key == KEY_UP) {
        f = 1.0f;
    } else if (key == KEY_DOWN) {
        f = -1.0f;
    }
    camera_pitch(state->world_camera, f * state->delta_time);
}

void game_on_move_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;
    static const f32 temp_move_speed = 50.0f;
    camera_move_forward(state->world_camera, temp_move_speed * state->delta_time);
}

void game_on_move_backward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;
    static const f32 temp_move_speed = 50.0f;
    camera_move_backward(state->world_camera, temp_move_speed * state->delta_time);
}

void game_on_move_left(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;
    static const f32 temp_move_speed = 50.0f;
    camera_move_left(state->world_camera, temp_move_speed * state->delta_time);
}

void game_on_move_right(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;
    static const f32 temp_move_speed = 50.0f;
    camera_move_right(state->world_camera, temp_move_speed * state->delta_time);
}

void game_on_move_up(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;
    static const f32 temp_move_speed = 50.0f;
    camera_move_up(state->world_camera, temp_move_speed * state->delta_time);
}

void game_on_move_down(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;
    static const f32 temp_move_speed = 50.0f;
    camera_move_down(state->world_camera, temp_move_speed * state->delta_time);
}

void game_on_console_change_visibility(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;

    b8 console_visible = debug_console_visible();
    console_visible = !console_visible;

    debug_console_visible_set(console_visible);
    if (console_visible) {
        input_keymap_push(&state->console_keymap);
    } else {
        input_keymap_pop();
    }
}

void game_on_set_render_mode_default(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_DEFAULT;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (game*)user_data, data);
}

void game_on_set_render_mode_lighting(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_LIGHTING;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (game*)user_data, data);
}

void game_on_set_render_mode_normals(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_context data = {};
    data.data.i32[0] = RENDERER_VIEW_MODE_NORMALS;
    event_fire(EVENT_CODE_SET_RENDER_MODE, (game*)user_data, data);
}

void game_on_load_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    event_fire(EVENT_CODE_DEBUG1, (game*)user_data, (event_context){});
}

void game_on_console_scroll(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    if (key == KEY_UP) {
        debug_console_move_up();
    } else if (key == KEY_DOWN) {
        debug_console_move_down();
    }
}

void game_on_console_scroll_hold(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;

    static f32 accumulated_time = 0.0f;
    accumulated_time += state->delta_time;
    if (accumulated_time >= 0.1f) {
        if (key == KEY_UP) {
            debug_console_move_up();
        } else if (key == KEY_DOWN) {
            debug_console_move_down();
        }
        accumulated_time = 0.0f;
    }
}

void game_on_debug_texture_swap(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    KDEBUG("Swapping texture!");
    event_context context = {};
    event_fire(EVENT_CODE_DEBUG0, user_data, context);
}

void game_on_debug_cam_position(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;

    KDEBUG(
        "Pos:[%.2f, %.2f, %.2f",
        state->world_camera->position.x,
        state->world_camera->position.y,
        state->world_camera->position.z);
}

void game_on_debug_vsync_toggle(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    b8 vsync_enabled = renderer_flag_enabled(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT);
    vsync_enabled = !vsync_enabled;
    renderer_flag_set_enabled(RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT, vsync_enabled);
}

void game_print_memory_metrics(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
    game* game_inst = (game*)user_data;
    game_state* state = (game_state*)game_inst->state;

    char* usage = get_memory_usage_str();
    KINFO(usage);
    string_free(usage);
    KDEBUG("Allocations: %llu (%llu this frame)", state->alloc_count, state->alloc_count - state->prev_alloc_count);
}

void game_setup_keymaps(game* game_inst) {
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

    keymap_binding_add(&testbed_keymap, KEY_0, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_render_mode_default);
    keymap_binding_add(&testbed_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_render_mode_lighting);
    keymap_binding_add(&testbed_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_set_render_mode_normals);

    keymap_binding_add(&testbed_keymap, KEY_L, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_load_scene);

    keymap_binding_add(&testbed_keymap, KEY_T, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_debug_texture_swap);
    keymap_binding_add(&testbed_keymap, KEY_P, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_debug_cam_position);
    keymap_binding_add(&testbed_keymap, KEY_V, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_debug_vsync_toggle);
    keymap_binding_add(&testbed_keymap, KEY_M, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_print_memory_metrics);

    input_keymap_push(&testbed_keymap);

    // A console-specific keymap. Is not pushed by default.
    game_state* state = ((game_state*)game_inst->state);
    state->console_keymap = keymap_create();
    state->console_keymap.overrides_all = true;
    keymap_binding_add(&state->console_keymap, KEY_GRAVE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_change_visibility);
    keymap_binding_add(&state->console_keymap, KEY_ESCAPE, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_change_visibility);

    keymap_binding_add(&state->console_keymap, KEY_UP, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll);
    keymap_binding_add(&state->console_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll);
    keymap_binding_add(&state->console_keymap, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll_hold);
    keymap_binding_add(&state->console_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, game_inst, game_on_console_scroll_hold);
}
