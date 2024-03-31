#pragma once

#include "defines.h"
#include "standard_ui_system.h"

typedef struct command_history_entry {
    const char* command;
} command_history_entry;

// TODO(travis): statically-defined state for now.
typedef struct debug_console_state {
    b8 loaded;
    u8 console_consumer_id;
    // Number of lines displayed at once.
    u32 line_display_count;
    // Number of lines offset from bottom of list.
    u32 line_offset;
    // darray
    char** lines;
    // darray
    command_history_entry* history;
    i32 history_offset;

    b8 dirty;
    b8 visible;

    sui_control bg_panel;
    sui_control text_control;
    sui_control entry_textbox;

} debug_console_state;

KAPI void debug_console_create(debug_console_state* out_console_state);

KAPI b8 debug_console_load(debug_console_state* state);
KAPI void debug_console_unload(debug_console_state* state);
KAPI void debug_console_update(debug_console_state* state);

KAPI void debug_console_on_lib_load(debug_console_state* state, b8 update_consumer);
KAPI void debug_console_on_lib_unload(debug_console_state* state);

KAPI struct sui_control* debug_console_get_text(debug_console_state* state);
KAPI struct sui_control* debug_console_get_entry_text(debug_console_state* state);

KAPI b8 debug_console_visible(debug_console_state* state);
KAPI void debug_console_visible_set(debug_console_state* state, b8 visible);

KAPI void debug_console_move_up(debug_console_state* state);
KAPI void debug_console_move_down(debug_console_state* state);
KAPI void debug_console_move_to_top(debug_console_state* state);
KAPI void debug_console_move_to_bottom(debug_console_state* state);

KAPI void debug_console_history_back(debug_console_state* state);
KAPI void debug_console_history_forward(debug_console_state* state);
