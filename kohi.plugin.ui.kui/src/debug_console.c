#include "debug_console.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kui_types.h"

#include <containers/darray.h>
#include <controls/kui_label.h>
#include <controls/kui_panel.h>
#include <controls/kui_textbox.h>
#include <core/console.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/input.h>
#include <kui_system.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <strings/kstring.h>

static void debug_console_entry_box_on_key (kui_state *state, kui_control self, kui_keyboard_event evt);

b8 debug_console_consumer_write (void *inst, log_level level, const char *message) {
	debug_console_state *state = (debug_console_state *)inst;
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
		char **split_message = darray_create(char *);
		u32 count = string_split(message, '\n', &split_message, true, false, false);
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

static b8 debug_console_on_resize (u16 code, void *sender, void *listener_inst, event_context context) {
	u16 width = context.data.u16[0];
	/* u16 height = context.data.u16[1]; */

	debug_console_state *state = listener_inst;
	vec2 size = kui_panel_size(state->kui_state, state->bg_panel);
	kui_panel_control_resize(state->kui_state, state->bg_panel, (vec2){width, size.y});

	kui_textbox_control_width_set(state->kui_state, state->entry_textbox, width - 4);

	return false;
}

b8 debug_console_create (kui_state *kui_state, debug_console_state *out_console_state) {
	if (!kui_state || !out_console_state) {
		return false;
	}

	out_console_state->line_display_count = 10;
	out_console_state->line_offset = 0;
	out_console_state->lines = darray_create(char *);
	out_console_state->visible = false;
	out_console_state->history = darray_create(command_history_entry);
	out_console_state->history_offset = -1;
	out_console_state->loaded = false;
	out_console_state->kui_state = kui_state;

	// NOTE: update the text based on number of lines to display and
	// the number of lines offset from the bottom. A UI Text object is
	// used for display for now. Can worry about colour in a separate pass.
	// Not going to consider word wrap.
	// NOTE: also should consider clipping rectangles and newlines.

	// Register as a console consumer.
	console_consumer_register(out_console_state, debug_console_consumer_write, &out_console_state->console_consumer_id);

	return true;
}

b8 debug_console_load (debug_console_state *state) {
	if (!state) {
		KFATAL("debug_console_load() called before console was initialized!");
		return false;
	}

	// Register for key events.
	event_register(EVENT_CODE_WINDOW_RESIZED, state, debug_console_on_resize);

	u16 font_size = 31;
	f32 height = 50.0f + (font_size * state->line_display_count + 1); // Account for padding and textbox at the bottom
	f32 width = engine_active_window_get()->width;

	// Create controls.
	kui_state *kui_state = state->kui_state;

	// Background panel.
	{
		state->bg_panel = kui_panel_control_create(kui_state, "debug_console_bg_panel", (vec2){width, height}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, INVALID_KUI_CONTROL, state->bg_panel));
		kui_control_set_is_visible(kui_state, state->bg_panel, false); // Not visible by default.
	}

	// Label to render console text.
	{
		state->text_control = kui_label_control_create(kui_state, "debug_console_log_text", FONT_TYPE_SYSTEM, kname_create("Noto Sans Mono CJK JP"), font_size, "");
		KASSERT(kui_system_control_add_child(kui_state, state->bg_panel, state->text_control));
		kui_control_position_set(kui_state, state->text_control, (vec3){3.0f, 0.0f, 0.0f});
	}

	// Textbox for command entry.
	{
		state->entry_textbox = kui_textbox_control_create(kui_state, "debug_console_entry_textbox", FONT_TYPE_SYSTEM, kname_create("Noto Sans Mono CJK JP"), font_size, "", KUI_TEXTBOX_TYPE_STRING);
		kui_control_set_user_data(kui_state, state->entry_textbox, sizeof(debug_console_state), state, false, MEMORY_TAG_UI);
		kui_control_set_on_key(kui_state, state->entry_textbox, debug_console_entry_box_on_key);
		KASSERT(kui_system_control_add_child(kui_state, state->bg_panel, state->entry_textbox));
		kui_textbox_control_width_set(state->kui_state, state->entry_textbox, width - 4);

		// HACK: This is definitely not the best way to figure out the height of the above text control.
		kui_control_position_set(kui_state, state->entry_textbox, (vec3){3.0f, 10.0f + (font_size * state->line_display_count), 0.0f});
	}

	state->loaded = true;

	return true;
}

void debug_console_unload (debug_console_state *state) {
	if (state) {
		state->loaded = false;

		console_consumer_unregister(state->console_consumer_id);
		state->console_consumer_id = INVALID_ID_U8;

		u32 len = darray_length(state->lines);
		for (u32 i = 0; i < len; ++i) {
			string_free(state->lines[i]);
		}
		darray_destroy(state->lines);
		state->lines = KNULL;

		darray_destroy(state->history);
		state->history = KNULL;
	}
}

#define DEBUG_CONSOLE_BUFFER_LENGTH 32768

void debug_console_update (debug_console_state *state) {
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

			const char *line = state->lines[i];
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
		kui_label_text_set(state->kui_state, state->text_control, buffer);

		state->dirty = false;
	}
}

static void debug_console_entry_box_on_key (kui_state *state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;
		/* b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT); */

		if (key_code == KEY_ENTER) {
			const char *entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				// Keep the command in the history list.
				command_history_entry entry = {0};
				entry.command = string_duplicate(entry_control_text);
				if (entry.command) {
					debug_console_state *user_data = kui_control_get_user_data(state, self);
					darray_push(user_data->history, entry);

					// Execute the command and clear the text.
					if (!console_command_execute(entry_control_text)) {
						// TODO: handle error?
					}
				}
				// Clear the text.
				kui_textbox_text_set(state, self, "");
			}
		}
	}
}

void debug_console_on_lib_load (debug_console_state *state, b8 update_consumer) {
	if (update_consumer) {
		kui_control_set_on_key(state->kui_state, state->entry_textbox, debug_console_entry_box_on_key);
		event_register(EVENT_CODE_WINDOW_RESIZED, state, debug_console_on_resize);
		console_consumer_update(state->console_consumer_id, state, debug_console_consumer_write);
	}
}

void debug_console_on_lib_unload (debug_console_state *state) {
	kui_control_set_on_key(state->kui_state, state->entry_textbox, KNULL);
	event_unregister(EVENT_CODE_WINDOW_RESIZED, state, debug_console_on_resize);
	console_consumer_update(state->console_consumer_id, 0, 0);
}

kui_control debug_console_get_text (debug_console_state *state) {
	if (state) {
		return state->text_control;
	}
	return INVALID_KUI_CONTROL;
}

kui_control debug_console_get_entry_text (debug_console_state *state) {
	if (state) {
		return state->entry_textbox;
	}
	return INVALID_KUI_CONTROL;
}

b8 debug_console_visible (debug_console_state *state) {
	if (!state) {
		return false;
	}

	return state->visible;
}

void debug_console_visible_set (debug_console_state *state, b8 visible) {
	if (state) {
		state->visible = visible;
		kui_control_set_is_visible(state->kui_state, state->bg_panel, visible);
		kui_system_focus_control(state->kui_state, visible ? state->entry_textbox : INVALID_KUI_CONTROL);
		input_key_repeats_enable(visible);
	}
}

void debug_console_move_up (debug_console_state *state) {
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

void debug_console_move_down (debug_console_state *state) {
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

void debug_console_move_to_top (debug_console_state *state) {
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

void debug_console_move_to_bottom (debug_console_state *state) {
	if (state) {
		state->dirty = true;
		state->line_offset = 0;
	}
}

void debug_console_history_back (debug_console_state *state) {
	if (state) {
		i32 length = darray_length(state->history);
		if (length > 0) {
			state->history_offset = KMIN(state->history_offset + 1, length - 1);
			i32 idx = length - state->history_offset - 1;
			kui_textbox_text_set(state->kui_state, state->entry_textbox, state->history[idx].command);
		}
	}
}

void debug_console_history_forward (debug_console_state *state) {
	if (state) {
		i32 length = darray_length(state->history);
		if (length > 0) {
			state->history_offset = KMAX(state->history_offset - 1, -1);
			i32 idx = length - state->history_offset - 1;
			kui_textbox_text_set(
				state->kui_state,
				state->entry_textbox,
				state->history_offset == -1 ? "" : state->history[idx].command);
		}
	}
}
