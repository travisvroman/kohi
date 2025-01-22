#include "debug_console.h"

#include <containers/darray.h>
#include <core/console.h>
#include <core/event.h>
#include <core/input.h>
#include <memory/kmemory.h>
#include <resources/resource_types.h>
#include <strings/kstring.h>

#include "controls/sui_label.h"
#include "controls/sui_panel.h"
#include "controls/sui_textbox.h"
#include "standard_ui_system.h"

static void debug_console_entry_box_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt);

b8 debug_console_consumer_write(void* inst, log_level level, const char* message) {
    debug_console_state* state = (debug_console_state*)inst;
    if (state) {
        // Not necessarily a failure, but move on if not loaded.
        if (!state->loaded) {
            return true;
        }
        // For high-priority error/fatal messages, don't bother with splitting,
        // just output them because something truly terrible could prevent this
        // split from happening.
        if (level <= LOG_LEVEL_ERROR) {
            // NOTE: Trim the string to get rid of the newline appended at the console level.
            darray_push(state->lines, string_trim(string_duplicate(message)));
            state->dirty = true;
            return true;
        }
        // Create a new copy of the string, and try splitting it
        // by newlines to make each one count as a new line.
        // NOTE: The lack of cleanup on the strings is intentional
        // here because the strings need to live on so that they can
        // be accessed by this debug console. Ordinarily a cleanup
        // via string_cleanup_split_darray would be warranted.
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

static b8 debug_console_on_resize(u16 code, void* sender, void* listener_inst, event_context context) {
    u16 width = context.data.u16[0];
    /* u16 height = context.data.u16[1]; */

    debug_console_state* state = listener_inst;
    vec2 size = sui_panel_size(state->sui_state, &state->bg_panel);
    sui_panel_control_resize(state->sui_state, &state->bg_panel, (vec2){width, size.y});

    return false;
}

b8 debug_console_create(standard_ui_state* sui_state, debug_console_state* out_console_state) {
    if (!sui_state || !out_console_state) {
        return false;
    }

    out_console_state->line_display_count = 10;
    out_console_state->line_offset = 0;
    out_console_state->lines = darray_create(char*);
    out_console_state->visible = false;
    out_console_state->history = darray_create(command_history_entry);
    out_console_state->history_offset = -1;
    out_console_state->loaded = false;
    out_console_state->sui_state = sui_state;

    // NOTE: update the text based on number of lines to display and
    // the number of lines offset from the bottom. A UI Text object is
    // used for display for now. Can worry about colour in a separate pass.
    // Not going to consider word wrap.
    // NOTE: also should consider clipping rectangles and newlines.

    // Register as a console consumer.
    console_consumer_register(out_console_state, debug_console_consumer_write, &out_console_state->console_consumer_id);

    // Register for key events.
    event_register(EVENT_CODE_WINDOW_RESIZED, out_console_state, debug_console_on_resize);

    u16 font_size = 31;
    f32 height = 50.0f + (font_size * out_console_state->line_display_count + 1); // Account for padding and textbox at the bottom

    // Create controls.

    // Background panel.
    {
        if (!sui_panel_control_create(sui_state, "debug_console_bg_panel", (vec2){1280.0f, height}, (vec4){0.0f, 0.0f, 0.0f, 0.75f}, &out_console_state->bg_panel)) {
            KERROR("Failed to create background panel.");
            return false;
        }
        if (!standard_ui_system_register_control(sui_state, &out_console_state->bg_panel)) {
            KERROR("Unable to register control.");
            return false;
        }
        /* transform_translate(&state->bg_panel.xform, (vec3){500, 100}); */
        if (!standard_ui_system_control_add_child(sui_state, 0, &out_console_state->bg_panel)) {
            KERROR("Failed to parent background panel.");
            return false;
        }
    }

    // Label to render console text.
    {
        if (!sui_label_control_create(sui_state, "debug_console_log_text", FONT_TYPE_SYSTEM, kname_create("Noto Sans CJK JP"), font_size, "", &out_console_state->text_control)) {
            KFATAL("Unable to create text control for debug console.");
            return false;
        }
        if (!standard_ui_system_register_control(sui_state, &out_console_state->text_control)) {
            KERROR("Unable to register console text label control.");
            return false;
        }
        if (!standard_ui_system_control_add_child(sui_state, &out_console_state->bg_panel, &out_console_state->text_control)) {
            KERROR("Failed to add background console text label as a child of the panel.");
            return false;
        }

        sui_control_position_set(sui_state, &out_console_state->text_control, (vec3){3.0f, font_size, 0.0f});
    }

    // Textbox for command entry.
    {
        if (!sui_textbox_control_create(sui_state, "debug_console_entry_textbox", FONT_TYPE_SYSTEM, kname_create("Noto Sans CJK JP"), font_size, "", &out_console_state->entry_textbox)) {
            KFATAL("Unable to create entry textbox control for debug console.");
            return false;
        }

        out_console_state->entry_textbox.user_data = out_console_state;
        out_console_state->entry_textbox.user_data_size = sizeof(debug_console_state*);
        out_console_state->entry_textbox.on_key = debug_console_entry_box_on_key;
        if (!standard_ui_system_register_control(out_console_state->sui_state, &out_console_state->entry_textbox)) {
            KERROR("Unable to register control.");
            return false;
        }
        if (!standard_ui_system_control_add_child(sui_state, &out_console_state->bg_panel, &out_console_state->entry_textbox)) {
            KERROR("Failed to parent textbox control to background panel of debug console.");
            return false;
        }

        // HACK: This is definitely not the best way to figure out the height of the above text control.
        sui_control_position_set(sui_state, &out_console_state->entry_textbox, (vec3){3.0f, 10.0f + (font_size * out_console_state->line_display_count), 0.0f});
    }

    return true;
}

b8 debug_console_load(debug_console_state* state) {
    if (!state) {
        KFATAL("debug_console_load() called before console was initialized!");
        return false;
    }

    // Load controls and activate them.

    // Background panel.
    {
        if (!state->bg_panel.load(state->sui_state, &state->bg_panel)) {
            KERROR("Failed to load background panel.");
            return false;
        }
        state->bg_panel.is_active = true;
        state->bg_panel.is_visible = false;
        if (!standard_ui_system_update_active(state->sui_state, &state->bg_panel)) {
            KERROR("Unable to update active state.");
        }
    }

    // Label to render console text.
    {
        if (!state->text_control.load(state->sui_state, &state->text_control)) {
            KERROR("Failed to load text control.");
        }
        state->text_control.is_active = true;
        if (!standard_ui_system_update_active(state->sui_state, &state->text_control)) {
            KERROR("Unable to update active state.");
        }
    }

    // Textbox for command entry.
    {
        if (!state->entry_textbox.load(state->sui_state, &state->entry_textbox)) {
            KERROR("Failed to load entry textbox for debug console.");
        }
        state->entry_textbox.is_active = true;
        if (!standard_ui_system_update_active(state->sui_state, &state->entry_textbox)) {
            KERROR("Unable to update active state.");
        }
    }

    state->loaded = true;

    return true;
}

void debug_console_unload(debug_console_state* state) {
    if (state) {
        state->loaded = false;
    }
}

#define DEBUG_CONSOLE_BUFFER_LENGTH 32768

void debug_console_update(debug_console_state* state) {
    if (state && state->loaded && state->dirty) {
        // Build one string out of several lines of console text to display in the console window.
        // This has a limit of DEBUG_CONSOLE_BUFFER_LENGTH, which should be more than enough anyway,
        // but is clamped to avoid a buffer overflow.
        u32 line_count = darray_length(state->lines);
        u32 max_lines = KMIN(state->line_display_count, KMAX(line_count, state->line_display_count));

        // Calculate the min line first, taking into account the line offset as well.
        u32 min_line = KMAX(line_count - max_lines - state->line_offset, 0);
        u32 max_line = min_line + max_lines - 1;

        // Hopefully big enough to handle most things.
        char buffer[DEBUG_CONSOLE_BUFFER_LENGTH];
        kzero_memory(buffer, sizeof(char) * DEBUG_CONSOLE_BUFFER_LENGTH);
        // Leave enough space at the end of the buffer for a \n and a null terminator.
        const u32 max_buf_pos = DEBUG_CONSOLE_BUFFER_LENGTH - 2;
        u32 buffer_pos = 0;
        for (u32 i = min_line; i <= max_line && buffer_pos < max_buf_pos; ++i) {
            // TODO: insert colour codes for the message type.

            const char* line = state->lines[i];
            u32 line_length = string_length(line);
            for (u32 c = 0; c < line_length && buffer_pos < max_buf_pos; c++, buffer_pos++) {
                buffer[buffer_pos] = line[c];
            }
            // Append a newline
            buffer[buffer_pos] = '\n';
            buffer_pos++;
        }

        // Make sure the string is null-terminated
        buffer[buffer_pos] = '\0';

        // Once the string is built, set the text.
        sui_label_text_set(state->sui_state, &state->text_control, buffer);

        state->dirty = false;
    }
}

static void debug_console_entry_box_on_key(standard_ui_state* state, sui_control* self, sui_keyboard_event evt) {
    if (evt.type == SUI_KEYBOARD_EVENT_TYPE_PRESS) {
        u16 key_code = evt.key;
        /* b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT); */

        if (key_code == KEY_ENTER) {
            debug_console_state* state = self->internal_data;
            const char* entry_control_text = sui_textbox_text_get(state->sui_state, self);
            u32 len = string_length(entry_control_text);
            if (len > 0) {
                // Keep the command in the history list.
                command_history_entry entry = {0};
                entry.command = string_duplicate(entry_control_text);
                if (entry.command) {
                    darray_push(((debug_console_state*)self->user_data)->history, entry);

                    // Execute the command and clear the text.
                    if (!console_command_execute(entry_control_text)) {
                        // TODO: handle error?
                    }
                }
                // Clear the text.
                sui_textbox_text_set(state->sui_state, self, "");
            }
        }
    }
}

void debug_console_on_lib_load(debug_console_state* state, b8 update_consumer) {
    if (update_consumer) {
        state->entry_textbox.on_key = debug_console_entry_box_on_key;
        event_register(EVENT_CODE_WINDOW_RESIZED, state, debug_console_on_resize);
        console_consumer_update(state->console_consumer_id, state, debug_console_consumer_write);
    }
}

void debug_console_on_lib_unload(debug_console_state* state) {
    state->entry_textbox.on_key = 0;
    event_unregister(EVENT_CODE_WINDOW_RESIZED, state, debug_console_on_resize);
    console_consumer_update(state->console_consumer_id, 0, 0);
}

sui_control* debug_console_get_text(debug_console_state* state) {
    if (state) {
        return &state->text_control;
    }
    return 0;
}

sui_control* debug_console_get_entry_text(debug_console_state* state) {
    if (state) {
        return &state->entry_textbox;
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
        state->bg_panel.is_visible = visible;
        standard_ui_system_focus_control(state->sui_state, visible ? &state->entry_textbox : 0);
        input_key_repeats_enable(visible);
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
        if (state->line_offset == 0) {
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
        i32 length = darray_length(state->history);
        if (length > 0) {
            state->history_offset = KMIN(state->history_offset + 1, length - 1);
            i32 idx = length - state->history_offset - 1;
            sui_textbox_text_set(state->sui_state, &state->entry_textbox, state->history[idx].command);
        }
    }
}

void debug_console_history_forward(debug_console_state* state) {
    if (state) {
        i32 length = darray_length(state->history);
        if (length > 0) {
            state->history_offset = KMAX(state->history_offset - 1, -1);
            i32 idx = length - state->history_offset - 1;
            sui_textbox_text_set(
                state->sui_state,
                &state->entry_textbox,
                state->history_offset == -1 ? "" : state->history[idx].command);
        }
    }
}
