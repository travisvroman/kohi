#include "debug_console.h"

#include <core/console.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <containers/darray.h>
#include <resources/ui_text.h>
#include <core/event.h>
#include <core/input.h>

typedef struct command_history_entry {
    const char* command;
} command_history_entry;

// TODO(travis): statically-defined state for now.
typedef struct debug_console_state {
    // Number of lines displayed at once.
    i32 line_display_count;
    // Number of lines offset from bottom of list.
    i32 line_offset;
    // darray
    char** lines;
    // darray
    command_history_entry* history;
    i32 history_offset;

    b8 dirty;
    b8 visible;

    ui_text text_control;
    ui_text entry_control;

} debug_console_state;

static debug_console_state* state_ptr;

b8 debug_console_consumer_write(void* inst, log_level level, const char* message) {
    if (state_ptr) {
        // Create a new copy of the string, and try splitting it
        // by newlines to make each one count as a new line.
        // NOTE: The lack of cleanup on the strings is intentional
        // here because the strings need to live on so that they can
        // be accessed by this debug console. Ordinarily a cleanup
        // via string_cleanup_split_array would be warranted.
        char** split_message = darray_create(char*);
        u32 count = string_split(message, '\n', &split_message, true, false);
        // Push each to the array as a new line.
        for (u32 i = 0; i < count; ++i) {
            darray_push(state_ptr->lines, split_message[i]);
        }

        // DO clean up the temporary array itself though (just
        // not its content in this case).
        darray_destroy(split_message);
        state_ptr->dirty = true;
    }
    return true;
}

static b8 debug_console_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    if (!state_ptr->visible) {
        return false;
    }
    if (code == EVENT_CODE_KEY_PRESSED) {
        u16 key_code = context.data.u16[0];
        b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT);

        if (key_code == KEY_ENTER) {
            u32 len = string_length(state_ptr->entry_control.text);
            if (len > 0) {
                // Keep the command in the history list.
                command_history_entry entry;
                entry.command = string_duplicate(state_ptr->entry_control.text);
                darray_push(state_ptr->history, entry);

                // Execute the command and clear the text.
                if (!console_execute_command(state_ptr->entry_control.text)) {
                    // TODO: handle error?
                }
                // Clear the text.
                ui_text_set_text(&state_ptr->entry_control, "");
            }
        } else if (key_code == KEY_BACKSPACE) {
            u32 len = string_length(state_ptr->entry_control.text);
            if (len > 0) {
                char* str = string_duplicate(state_ptr->entry_control.text);
                str[len - 1] = 0;
                ui_text_set_text(&state_ptr->entry_control, str);
                kfree(str, len + 1, MEMORY_TAG_STRING);
            }
        } else {
            // Use A-Z and 0-9 as-is.
            char char_code = key_code;
            if ((key_code >= KEY_A && key_code <= KEY_Z)) {
                // TODO: check caps lock.
                if (!shift_held) {
                    char_code = key_code + 32;
                }
            } else if ((key_code >= KEY_0 && key_code <= KEY_9)) {
                if (shift_held) {
                    // NOTE: this handles US standard keyboard layouts.
                    // Will need to handle other layouts as well.
                    switch (key_code) {
                        case KEY_0:
                            char_code = ')';
                            break;
                        case KEY_1:
                            char_code = '!';
                            break;
                        case KEY_2:
                            char_code = '@';
                            break;
                        case KEY_3:
                            char_code = '#';
                            break;
                        case KEY_4:
                            char_code = '$';
                            break;
                        case KEY_5:
                            char_code = '%';
                            break;
                        case KEY_6:
                            char_code = '^';
                            break;
                        case KEY_7:
                            char_code = '&';
                            break;
                        case KEY_8:
                            char_code = '*';
                            break;
                        case KEY_9:
                            char_code = '(';
                            break;
                    }
                }
            } else {
                switch (key_code) {
                    case KEY_SPACE:
                        char_code = key_code;
                        break;
                    case KEY_MINUS:
                        char_code = shift_held ? '_' : '-';
                        break;
                    case KEY_EQUAL:
                        char_code = shift_held ? '+' : '=';
                        break;
                    default:
                        // Not valid for entry, use 0
                        char_code = 0;
                        break;
                }
            }

            if (char_code != 0) {
                u32 len = string_length(state_ptr->entry_control.text);
                char* new_text = kallocate(len + 2, MEMORY_TAG_STRING);
                string_format(new_text, "%s%c", state_ptr->entry_control.text, char_code);
                ui_text_set_text(&state_ptr->entry_control, new_text);
                kfree(new_text, len + 1, MEMORY_TAG_STRING);
            }
        }

        // TODO: keep command history, up/down to navigate it.
    }

    return false;
}

void debug_console_create() {
    if (!state_ptr) {
        state_ptr = kallocate(sizeof(debug_console_state), MEMORY_TAG_GAME);
        state_ptr->line_display_count = 10;
        state_ptr->line_offset = 0;
        state_ptr->lines = darray_create(char*);
        state_ptr->visible = false;
        state_ptr->history = darray_create(command_history_entry);
        state_ptr->history_offset = 0;

        // NOTE: update the text based on number of lines to display and
        // the number of lines offset from the bottom. A UI Text object is
        // used for display for now. Can worry about colour in a separate pass.
        // Not going to consider word wrap.
        // NOTE: also should consider clipping rectangles and newlines.

        // Register as a console consumer.
        console_register_consumer(0, debug_console_consumer_write);
    }
}

b8 debug_console_load() {
    if (!state_ptr) {
        KFATAL("debug_console_load() called before console was initialized!");
        return false;
    }

    // Create a ui text control for rendering.
    if (!ui_text_create(UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "", &state_ptr->text_control)) {
        KFATAL("Unable to create text control for debug console.");
        return false;
    }

    ui_text_set_position(&state_ptr->text_control, (vec3){3.0f, 30.0f, 0.0f});

    // Create another ui text control for rendering typed text.
    if (!ui_text_create(UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "", &state_ptr->entry_control)) {
        KFATAL("Unable to create entry text control for debug console.");
        return false;
    }

    ui_text_set_position(&state_ptr->entry_control, (vec3){3.0f, 30.0f + (31.0f * state_ptr->line_display_count), 0.0f});

    event_register(EVENT_CODE_KEY_PRESSED, 0, debug_console_on_key);
    event_register(EVENT_CODE_KEY_RELEASED, 0, debug_console_on_key);

    return true;
}

void debug_console_unload() {
    if (state_ptr) {
        ui_text_destroy(&state_ptr->text_control);
        ui_text_destroy(&state_ptr->entry_control);
    }
}

void debug_console_update() {
    if (state_ptr && state_ptr->dirty) {
        u32 line_count = darray_length(state_ptr->lines);
        u32 max_lines = KMIN(state_ptr->line_display_count, KMAX(line_count, state_ptr->line_display_count));

        // Calculate the min line first, taking into account the line offset as well.
        u32 min_line = KMAX(line_count - max_lines - state_ptr->line_offset, 0);
        u32 max_line = min_line + max_lines - 1;

        // Hopefully big enough to handle most things.
        char buffer[16384];
        kzero_memory(buffer, sizeof(char) * 16384);
        u32 buffer_pos = 0;
        for (u32 i = min_line; i <= max_line; ++i) {
            // TODO: insert colour codes for the message type.

            const char* line = state_ptr->lines[i];
            u32 line_length = string_length(line);
            for (u32 c = 0; c < line_length; c++, buffer_pos++) {
                buffer[buffer_pos] = line[c];
            }
            // Append a newline
            buffer[buffer_pos] = '\n';
            buffer_pos++;
        }

        // Make sure the string is null-terminated
        buffer[buffer_pos] = '\0';

        // Once the string is built, set the text.
        ui_text_set_text(&state_ptr->text_control, buffer);

        state_ptr->dirty = false;
    }
}

ui_text* debug_console_get_text() {
    if (state_ptr) {
        return &state_ptr->text_control;
    }
    return 0;
}

ui_text* debug_console_get_entry_text() {
    if (state_ptr) {
        return &state_ptr->entry_control;
    }
    return 0;
}

b8 debug_console_visible() {
    if (!state_ptr) {
        return false;
    }

    return state_ptr->visible;
}

void debug_console_visible_set(b8 visible) {
    if (state_ptr) {
        state_ptr->visible = visible;
    }
}

void debug_console_move_up() {
    if (state_ptr) {
        state_ptr->dirty = true;
        u32 line_count = darray_length(state_ptr->lines);
        // Don't bother with trying an offset, just reset and boot out.
        if (line_count <= state_ptr->line_display_count) {
            state_ptr->line_offset = 0;
            return;
        }
        state_ptr->line_offset++;
        state_ptr->line_offset = KMIN(state_ptr->line_offset, line_count - state_ptr->line_display_count);
    }
}

void debug_console_move_down() {
    if (state_ptr) {
        state_ptr->dirty = true;
        u32 line_count = darray_length(state_ptr->lines);
        // Don't bother with trying an offset, just reset and boot out.
        if (line_count <= state_ptr->line_display_count) {
            state_ptr->line_offset = 0;
            return;
        }

        state_ptr->line_offset--;
        state_ptr->line_offset = KMAX(state_ptr->line_offset, 0);
    }
}

void debug_console_move_to_top() {
    if (state_ptr) {
        state_ptr->dirty = true;
        u32 line_count = darray_length(state_ptr->lines);
        // Don't bother with trying an offset, just reset and boot out.
        if (line_count <= state_ptr->line_display_count) {
            state_ptr->line_offset = 0;
            return;
        }

        state_ptr->line_offset = line_count - state_ptr->line_display_count;
    }
}

void debug_console_move_to_bottom() {
    if (state_ptr) {
        state_ptr->dirty = true;
        state_ptr->line_offset = 0;
    }
}

void debug_console_history_back() {
    if (state_ptr) {
        u32 length = darray_length(state_ptr->history);
        if (length > 0) {
            state_ptr->history_offset = KMIN(state_ptr->history_offset++, length - 1);
            ui_text_set_text(&state_ptr->entry_control, state_ptr->history[length - state_ptr->history_offset - 1].command);
        }
    }
}

void debug_console_history_forward() {
    if (state_ptr) {
        u32 length = darray_length(state_ptr->history);
        if (length > 0) {
            state_ptr->history_offset = KMAX(state_ptr->history_offset--, 0);
            ui_text_set_text(&state_ptr->entry_control, state_ptr->history[length - state_ptr->history_offset - 1].command);
        }
    }
}