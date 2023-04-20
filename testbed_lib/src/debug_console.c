#include "debug_console.h"
#include "resources/ui_text.h"

#include <core/console.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <containers/darray.h>

#include <core/event.h>
#include <core/input.h>

b8 debug_console_consumer_write(void* inst, log_level level, const char* message) {
    debug_console_state* state = (debug_console_state*)inst;
    if (state) {
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
            darray_push(state->lines, split_message[i]);
        }

        // DO clean up the temporary array itself though (just
        // not its content in this case).
        darray_destroy(split_message);
        state->dirty = true;
    }
    return true;
}

static b8 debug_console_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    debug_console_state* state = (debug_console_state*)listener_inst;
    if (!state->visible) {
        return false;
    }
    if (code == EVENT_CODE_KEY_PRESSED) {
        u16 key_code = context.data.u16[0];
        b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT);

        if (key_code == KEY_ENTER) {
            u32 len = string_length(state->entry_control.text);
            if (len > 0) {
                // Keep the command in the history list.
                command_history_entry entry;
                entry.command = string_duplicate(state->entry_control.text);
                darray_push(state->history, entry);

                // Execute the command and clear the text.
                if (!console_command_execute(state->entry_control.text)) {
                    // TODO: handle error?
                }
                // Clear the text.
                ui_text_text_set(&state->entry_control, "");
            }
        } else if (key_code == KEY_BACKSPACE) {
            u32 len = string_length(state->entry_control.text);
            if (len > 0) {
                char* str = string_duplicate(state->entry_control.text);
                str[len - 1] = 0;
                ui_text_text_set(&state->entry_control, str);
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
                u32 len = string_length(state->entry_control.text);
                char* new_text = kallocate(len + 2, MEMORY_TAG_STRING);
                string_format(new_text, "%s%c", state->entry_control.text, char_code);
                ui_text_text_set(&state->entry_control, new_text);
                kfree(new_text, len + 1, MEMORY_TAG_STRING);
            }
        }

        // TODO: keep command history, up/down to navigate it.
    }

    return false;
}

void debug_console_create(debug_console_state* out_console_state) {
    if (out_console_state) {
        out_console_state->line_display_count = 10;
        out_console_state->line_offset = 0;
        out_console_state->lines = darray_create(char*);
        out_console_state->visible = false;
        out_console_state->history = darray_create(command_history_entry);
        out_console_state->history_offset = 0;

        // NOTE: update the text based on number of lines to display and
        // the number of lines offset from the bottom. A UI Text object is
        // used for display for now. Can worry about colour in a separate pass.
        // Not going to consider word wrap.
        // NOTE: also should consider clipping rectangles and newlines.

        // Register as a console consumer.
        console_consumer_register(out_console_state, debug_console_consumer_write, &out_console_state->console_consumer_id);

        // Register for key events.
        event_register(EVENT_CODE_KEY_PRESSED, out_console_state, debug_console_on_key);
        event_register(EVENT_CODE_KEY_RELEASED, out_console_state, debug_console_on_key);
    }
}

b8 debug_console_load(debug_console_state* state) {
    if (!state) {
        KFATAL("debug_console_load() called before console was initialized!");
        return false;
    }

    // Create a ui text control for rendering.
    if (!ui_text_create(UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "", &state->text_control)) {
        KFATAL("Unable to create text control for debug console.");
        return false;
    }

    ui_text_position_set(&state->text_control, (vec3){3.0f, 30.0f, 0.0f});

    // Create another ui text control for rendering typed text.
    if (!ui_text_create(UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "", &state->entry_control)) {
        KFATAL("Unable to create entry text control for debug console.");
        return false;
    }

    ui_text_position_set(&state->entry_control, (vec3){3.0f, 30.0f + (31.0f * state->line_display_count), 0.0f});

    return true;
}

void debug_console_unload(debug_console_state* state) {
    if (state) {
        ui_text_destroy(&state->text_control);
        ui_text_destroy(&state->entry_control);
    }
}

void debug_console_update(debug_console_state* state) {
    if (state && state->dirty) {
        u32 line_count = darray_length(state->lines);
        u32 max_lines = KMIN(state->line_display_count, KMAX(line_count, state->line_display_count));

        // Calculate the min line first, taking into account the line offset as well.
        u32 min_line = KMAX(line_count - max_lines - state->line_offset, 0);
        u32 max_line = min_line + max_lines - 1;

        // Hopefully big enough to handle most things.
        char buffer[16384];
        kzero_memory(buffer, sizeof(char) * 16384);
        u32 buffer_pos = 0;
        for (u32 i = min_line; i <= max_line; ++i) {
            // TODO: insert colour codes for the message type.

            const char* line = state->lines[i];
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
        ui_text_text_set(&state->text_control, buffer);

        state->dirty = false;
    }
}

void debug_console_on_lib_load(debug_console_state* state, b8 update_consumer) {
    if (update_consumer) {
        event_register(EVENT_CODE_KEY_PRESSED, state, debug_console_on_key);
        event_register(EVENT_CODE_KEY_RELEASED, state, debug_console_on_key);
        console_consumer_update(state->console_consumer_id, state, debug_console_consumer_write);
    }
}

void debug_console_on_lib_unload(debug_console_state* state) {
    event_unregister(EVENT_CODE_KEY_PRESSED, state, debug_console_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, state, debug_console_on_key);
    console_consumer_update(state->console_consumer_id, 0, 0);
}

ui_text* debug_console_get_text(debug_console_state* state) {
    if (state) {
        return &state->text_control;
    }
    return 0;
}

ui_text* debug_console_get_entry_text(debug_console_state* state) {
    if (state) {
        return &state->entry_control;
    }
    return 0;
}

b8 debug_console_visible(debug_console_state* state) {
    if (!state) {
        return false;
    }

    return state->visible;
}

void debug_console_visible_set(debug_console_state* state, b8 visible) {
    if (state) {
        state->visible = visible;
    }
}

void debug_console_move_up(debug_console_state* state) {
    if (state) {
        state->dirty = true;
        u32 line_count = darray_length(state->lines);
        // Don't bother with trying an offset, just reset and boot out.
        if (line_count <= state->line_display_count) {
            state->line_offset = 0;
            return;
        }
        state->line_offset++;
        state->line_offset = KMIN(state->line_offset, line_count - state->line_display_count);
    }
}

void debug_console_move_down(debug_console_state* state) {
    if (state) {
        if(state->line_offset == 0) {
            return;
        }
        state->dirty = true;
        u32 line_count = darray_length(state->lines);
        // Don't bother with trying an offset, just reset and boot out.
        if (line_count <= state->line_display_count) {
            state->line_offset = 0;
            return;
        }

        state->line_offset--;
        state->line_offset = KMAX(state->line_offset, 0);
    }
}

void debug_console_move_to_top(debug_console_state* state) {
    if (state) {
        state->dirty = true;
        u32 line_count = darray_length(state->lines);
        // Don't bother with trying an offset, just reset and boot out.
        if (line_count <= state->line_display_count) {
            state->line_offset = 0;
            return;
        }

        state->line_offset = line_count - state->line_display_count;
    }
}

void debug_console_move_to_bottom(debug_console_state* state) {
    if (state) {
        state->dirty = true;
        state->line_offset = 0;
    }
}

void debug_console_history_back(debug_console_state* state) {
    if (state) {
        u32 length = darray_length(state->history);
        if (length > 0) {
            state->history_offset = KMIN(state->history_offset++, length - 1);
            ui_text_text_set(&state->entry_control, state->history[length - state->history_offset - 1].command);
        }
    }
}

void debug_console_history_forward(debug_console_state* state) {
    if (state) {
        u32 length = darray_length(state->history);
        if (length > 0) {
            state->history_offset = KMAX(state->history_offset--, 0);
            ui_text_text_set(&state->entry_control, state->history[length - state->history_offset - 1].command);
        }
    }
}
