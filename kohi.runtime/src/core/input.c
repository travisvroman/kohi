/**
 * @file input.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include "core/input.h"

#include "application/application_types.h"
#include "containers/stack.h"
#include "core/event.h"
#include "core/keymap.h"
#include "frame_data.h"
#include "input_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "platform/platform.h"

typedef struct keyboard_state {
	b8 keys[256];
} keyboard_state;

typedef struct mouse_state {
	i16 x;
	i16 y;
	b8 buttons[MOUSE_BUTTON_MAX];
	b8 dragging[MOUSE_BUTTON_MAX];
} mouse_state;

typedef struct input_state {
	keyboard_state keyboard_current;
	keyboard_state keyboard_previous;
	mouse_state mouse_current;
	mouse_state mouse_previous;

	stack keymap_stack;
	// keymap active_keymap;
	b8 allow_key_repeats;

	struct application *app;
} input_state;

// Internal input state pointer
// FIXME: Get rid of this static rubbish
static input_state *state_ptr;

static b8 check_modifiers (keymap_modifier modifiers);

b8 input_system_initialize (u64 *memory_requirement, void *state, void *config, struct application *app) {
	*memory_requirement = sizeof(input_state);
	if (state == 0) {
		return true;
	}
	kzero_memory(state, sizeof(input_state));
	state_ptr = state;

	// Create the keymap stack and an active keymap to apply to.
	stack_create(&state_ptr->keymap_stack, sizeof(keymap));
	// state_ptr->active_keymap = keymap_create();

	state_ptr->allow_key_repeats = false;
	state_ptr->app = app;

	if (!platform_initialize_input()) {
		KERROR("Failed to initialize platform input. See logs for details.");
		return false;
	}

	KINFO("Input subsystem initialized.");

	return true;
}

void input_system_shutdown (void *state) {
	// TODO: Add shutdown routines when needed.
	state_ptr = 0;
}

void input_update (const struct frame_data *p_frame_data) {
	if (!state_ptr) {
		return;
	}

	// Handle hold bindings.
	for (u32 k = 0; k < KEYS_MAX_KEYS; ++k) {
		keys key = (keys)k;
		if (input_is_key_down(key) && input_was_key_down(key)) {
			u32 map_count = state_ptr->keymap_stack.element_count;
			keymap *maps = (keymap *)state_ptr->keymap_stack.memory;
			for (i32 m = map_count - 1; m >= 0; --m) {
				keymap *map = &maps[m];
				keymap_binding *binding = &map->entries[key].bindings[0];
				b8 unset = false;
				while (binding) {
					// If an unset is detected, stop processing.
					if (binding->type == KEYMAP_BIND_TYPE_UNSET) {
						unset = true;
						break;
					} else if (binding->type == KEYMAP_BIND_TYPE_HOLD) {
						if (binding->code && check_modifiers(binding->modifiers)) {
							state_ptr->app->on_action(state_ptr->app, binding->code);
						}
					}

					binding = binding->next;
				}
				// If an unset is detected or the map is marked to override all, stop processing.
				if (unset || map->overrides_all) {
					break;
				}
			}
		}
	}

	// Copy current states to previous states.
	kcopy_memory(&state_ptr->keyboard_previous, &state_ptr->keyboard_current, sizeof(keyboard_state));
	kcopy_memory(&state_ptr->mouse_previous, &state_ptr->mouse_current, sizeof(mouse_state));
}

static b8 check_modifiers (keymap_modifier modifiers) {
	if (modifiers & KEYMAP_MODIFIER_SHIFT_BIT) {
		if (!input_is_key_down(K_KEY_SHIFT) && !input_is_key_down(K_KEY_LSHIFT) && !input_is_key_down(K_KEY_RSHIFT)) {
			return false;
		}
	} else {
		if (input_is_key_down(K_KEY_SHIFT) || input_is_key_down(K_KEY_LSHIFT) || input_is_key_down(K_KEY_RSHIFT)) {
			return false;
		}
	}
	if (modifiers & KEYMAP_MODIFIER_CONTROL_BIT) {
		if (!input_is_key_down(K_KEY_CONTROL) && !input_is_key_down(K_KEY_LCONTROL) && !input_is_key_down(K_KEY_RCONTROL)) {
			return false;
		}
	} else {
		if (input_is_key_down(K_KEY_CONTROL) || input_is_key_down(K_KEY_LCONTROL) || input_is_key_down(K_KEY_RCONTROL)) {
			return false;
		}
	}

	if (modifiers & KEYMAP_MODIFIER_ALT_BIT) {
		if (!input_is_key_down(K_KEY_LALT) && !input_is_key_down(K_KEY_RALT)) {
			return false;
		}
	} else {
		if (input_is_key_down(K_KEY_LALT) || input_is_key_down(K_KEY_RALT)) {
			return false;
		}
	}

	return true;
}

void input_process_key (keys key, b8 pressed, b8 is_repeat) {
	if (!state_ptr) {
		return;
	}
	KINFO("Key pressed: '%c'.", (char)key);
	// keymap_entry* map_entry = &state_ptr->active_keymap.entries[key];

	// Only handle this if the state actually changed, or if repeats are allowed.
	b8 is_repeat_state = is_repeat || (pressed && state_ptr->keyboard_current.keys[key]);
	b8 changed = state_ptr->keyboard_current.keys[key] != pressed;
	if (state_ptr->allow_key_repeats || changed) {
		// Update internal state.
		state_ptr->keyboard_current.keys[key] = pressed;

		// if (key == K_KEY_LALT) {
		//     KINFO("Left alt %s.", pressed ? "pressed" : "released");
		// } else if (key == K_KEY_RALT) {
		//     KINFO("Right alt %s.", pressed ? "pressed" : "released");
		// }

		// if (key == K_KEY_LCONTROL) {
		//     KINFO("Left ctrl %s.", pressed ? "pressed" : "released");
		// } else if (key == K_KEY_RCONTROL) {
		//     KINFO("Right ctrl %s.", pressed ? "pressed" : "released");
		// }

		// if (key == K_KEY_LSHIFT) {
		//     KINFO("Left shift %s.", pressed ? "pressed" : "released");
		// } else if (key == K_KEY_RSHIFT) {
		//     KINFO("Right shift %s.", pressed ? "pressed" : "released");
		// }

		// Check for key bindings
		// Iterate keymaps top-down on the stack.
		u32 map_count = state_ptr->keymap_stack.element_count;
		keymap *maps = (keymap *)state_ptr->keymap_stack.memory;
		for (i32 m = map_count - 1; m >= 0; --m) {
			keymap *map = &maps[m];
			keymap_binding *binding = &map->entries[key].bindings[0];
			b8 unset = false;
			while (binding) {
				// If an unset is detected, stop processing.
				if (binding->type == KEYMAP_BIND_TYPE_UNSET) {
					unset = true;
					break;
				}

				if (pressed && binding->type & KEYMAP_BIND_TYPE_PRESS) {
					if (binding->code && check_modifiers(binding->modifiers)) {
						state_ptr->app->on_action(state_ptr->app, binding->code);
					}
				}
				if (!pressed && binding->type & KEYMAP_BIND_TYPE_RELEASE && !is_repeat_state) {
					if (binding->code && check_modifiers(binding->modifiers)) {
						state_ptr->app->on_action(state_ptr->app, binding->code);
					}
				}

				binding = binding->next;
			}
			// If an unset is detected or the map is marked to override all, stop processing.
			if (unset || map->overrides_all) {
				break;
			}
		}

		// Fire off an event for immediate processing.
		event_context context;
		context.data.u16[0] = key;
		context.data.u16[1] = is_repeat ? 1 : 0;
		event_fire(pressed ? EVENT_CODE_KEY_PRESSED : EVENT_CODE_KEY_RELEASED, 0, context);
	} else {
		KINFO("changed=%u, repeats_allowed=%u", changed, state_ptr->allow_key_repeats);
		if (key == K_KEY_LSHIFT) {
			KINFO("Left shift %s. (1)", pressed ? "pressed" : "released");
		} else if (key == K_KEY_RSHIFT) {
			KINFO("Right shift %s. (1)", pressed ? "pressed" : "released");
		}
	}
}

void input_process_button (mouse_buttons button, b8 pressed) {
	// If the state changed, fire an event.
	if (state_ptr->mouse_current.buttons[button] != pressed) {
		state_ptr->mouse_current.buttons[button] = pressed;

		// Fire the event.
		event_context context;
		context.data.i16[0] = state_ptr->mouse_current.x;
		context.data.i16[1] = state_ptr->mouse_current.y;
		context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
		context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
		context.data.u16[4] = button;
		event_fire(pressed ? EVENT_CODE_BUTTON_PRESSED : EVENT_CODE_BUTTON_RELEASED, 0, context);
	}

	// Check for drag releases.
	if (!pressed) {
		if (state_ptr->mouse_current.dragging[button]) {
			// Issue a drag end event.

			state_ptr->mouse_current.dragging[button] = false;
			// KTRACE("mouse drag ended at: x:%hi, y:%hi, button: %hu", state_ptr->mouse_current.x, state_ptr->mouse_current.y, button);

			event_context context;
			context.data.i16[0] = state_ptr->mouse_current.x;
			context.data.i16[1] = state_ptr->mouse_current.y;
			context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
			context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
			context.data.u16[4] = button;
			event_fire(EVENT_CODE_MOUSE_DRAG_END, 0, context);
		} else {
			// If not a drag release, then it is a click.

			// Fire the event.
			event_context context;
			context.data.i16[0] = state_ptr->mouse_current.x;
			context.data.i16[1] = state_ptr->mouse_current.y;
			context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
			context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
			context.data.u16[4] = button;
			event_fire(EVENT_CODE_BUTTON_CLICKED, 0, context);
		}
	}
}

void input_process_mouse_move (i16 x, i16 y) {
	// Only process if actually different
	if (state_ptr->mouse_current.x != x || state_ptr->mouse_current.y != y) {
		// NOTE: Enable this if debugging.
		// KDEBUG("Mouse pos: %i, %i!", x, y);

		// Update internal state_ptr->
		state_ptr->mouse_current.x = x;
		state_ptr->mouse_current.y = y;

		// Fire the event.
		event_context context;
		context.data.i16[0] = x;
		context.data.i16[1] = y;
		context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
		context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
		event_fire(EVENT_CODE_MOUSE_MOVED, 0, context);

		for (u16 i = 0; i < MOUSE_BUTTON_MAX; ++i) {
			// Check if the button is down first.
			if (state_ptr->mouse_current.buttons[i]) {
				if (!state_ptr->mouse_previous.dragging[i] && !state_ptr->mouse_current.dragging[i]) {
					// Start a drag for this button.

					state_ptr->mouse_current.dragging[i] = true;

					event_context drag_context;
					drag_context.data.i16[0] = state_ptr->mouse_current.x;
					drag_context.data.i16[1] = state_ptr->mouse_current.y;
					drag_context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
					drag_context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
					drag_context.data.u16[4] = i;
					event_fire(EVENT_CODE_MOUSE_DRAG_BEGIN, 0, drag_context);
					// KTRACE("mouse drag began at: x:%hi, y:%hi, button: %hu", state_ptr->mouse_current.x, state_ptr->mouse_current.y, i);
				} else if (state_ptr->mouse_current.dragging[i]) {
					// Issue a continuance of the drag operation.
					event_context drag_context;
					drag_context.data.i16[0] = state_ptr->mouse_current.x;
					drag_context.data.i16[1] = state_ptr->mouse_current.y;
					drag_context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
					drag_context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
					drag_context.data.u16[4] = i;
					event_fire(EVENT_CODE_MOUSE_DRAGGED, 0, drag_context);
					// KTRACE("mouse drag continued at: x:%hi, y:%hi, button: %hu", state_ptr->mouse_current.x, state_ptr->mouse_current.y, i);
				}
			}
		}
	}
}

void input_process_mouse_wheel (i8 z_delta) {
	// NOTE: no internal state to update.

	// Fire the event.
	event_context context;
	context.data.i16[0] = state_ptr->mouse_current.x;
	context.data.i16[1] = state_ptr->mouse_current.y;
	context.data.i16[2] = state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
	context.data.i16[3] = state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
	context.data.i8[8] = z_delta;
	event_fire(EVENT_CODE_MOUSE_WHEEL, 0, context);
}

void input_key_repeats_enable (b8 enable) {
	if (state_ptr) {
		state_ptr->allow_key_repeats = enable;
	}
}

b8 input_is_key_down (keys key) {
	if (!state_ptr) {
		return false;
	}
	return state_ptr->keyboard_current.keys[key] == true;
}

b8 input_is_key_up (keys key) {
	if (!state_ptr) {
		return true;
	}
	return state_ptr->keyboard_current.keys[key] == false;
}

b8 input_was_key_down (keys key) {
	if (!state_ptr) {
		return false;
	}
	return state_ptr->keyboard_previous.keys[key] == true;
}

b8 input_was_key_up (keys key) {
	if (!state_ptr) {
		return true;
	}
	return state_ptr->keyboard_previous.keys[key] == false;
}

// mouse input
b8 input_is_button_down (mouse_buttons button) {
	if (!state_ptr) {
		return false;
	}
	return state_ptr->mouse_current.buttons[button] == true;
}

b8 input_is_button_up (mouse_buttons button) {
	if (!state_ptr) {
		return true;
	}
	return state_ptr->mouse_current.buttons[button] == false;
}

b8 input_was_button_down (mouse_buttons button) {
	if (!state_ptr) {
		return false;
	}
	return state_ptr->mouse_previous.buttons[button] == true;
}

b8 input_was_button_up (mouse_buttons button) {
	if (!state_ptr) {
		return true;
	}
	return state_ptr->mouse_previous.buttons[button] == false;
}

b8 input_is_button_dragging (mouse_buttons button) {
	if (!state_ptr) {
		return false;
	}

	return state_ptr->mouse_current.dragging[button];
}

void input_get_mouse_position (i32 *x, i32 *y) {
	if (!state_ptr) {
		*x = 0;
		*y = 0;
		return;
	}
	*x = state_ptr->mouse_current.x;
	*y = state_ptr->mouse_current.y;
}

void input_get_previous_mouse_position (i32 *x, i32 *y) {
	if (!state_ptr) {
		*x = 0;
		*y = 0;
		return;
	}
	*x = state_ptr->mouse_previous.x;
	*y = state_ptr->mouse_previous.y;
}

const char *input_keycode_str (keys key) {
	switch (key) {
	case K_KEY_BACKSPACE:
		return "backspace";
	case K_KEY_ENTER:
		return "enter";
	case K_KEY_TAB:
		return "tab";
	case K_KEY_SHIFT:
		return "shift";
	case K_KEY_CONTROL:
		return "ctrl";
	case K_KEY_PAUSE:
		return "pause";
	case K_KEY_CAPITAL:
		return "capslock";
	case K_KEY_ESCAPE:
		return "esc";

	case K_KEY_CONVERT:
		return "ime_convert";
	case K_KEY_NONCONVERT:
		return "ime_noconvert";
	case K_KEY_ACCEPT:
		return "ime_accept";
	case K_KEY_MODECHANGE:
		return "ime_modechange";

	case K_KEY_SPACE:
		return "space";
	case K_KEY_PAGEUP:
		return "pageup";
	case K_KEY_PAGEDOWN:
		return "pagedown";
	case K_KEY_END:
		return "end";
	case K_KEY_HOME:
		return "home";
	case K_KEY_LEFT:
		return "left";
	case K_KEY_UP:
		return "up";
	case K_KEY_RIGHT:
		return "right";
	case K_KEY_DOWN:
		return "down";
	case K_KEY_SELECT:
		return "select";
	case K_KEY_PRINT:
		return "print";
	case K_KEY_EXECUTE:
		return "execute";
	case K_KEY_PRINTSCREEN:
		return "printscreen";
	case K_KEY_INSERT:
		return "insert";
	case K_KEY_DELETE:
		return "delete";
	case K_KEY_HELP:
		return "help";

	case K_KEY_0:
		return "0";
	case K_KEY_1:
		return "1";
	case K_KEY_2:
		return "2";
	case K_KEY_3:
		return "3";
	case K_KEY_4:
		return "4";
	case K_KEY_5:
		return "5";
	case K_KEY_6:
		return "6";
	case K_KEY_7:
		return "7";
	case K_KEY_8:
		return "8";
	case K_KEY_9:
		return "9";

	case K_KEY_A:
		return "a";
	case K_KEY_B:
		return "b";
	case K_KEY_C:
		return "c";
	case K_KEY_D:
		return "d";
	case K_KEY_E:
		return "e";
	case K_KEY_F:
		return "f";
	case K_KEY_G:
		return "g";
	case K_KEY_H:
		return "h";
	case K_KEY_I:
		return "i";
	case K_KEY_J:
		return "j";
	case K_KEY_K:
		return "k";
	case K_KEY_L:
		return "l";
	case K_KEY_M:
		return "m";
	case K_KEY_N:
		return "n";
	case K_KEY_O:
		return "o";
	case K_KEY_P:
		return "p";
	case K_KEY_Q:
		return "q";
	case K_KEY_R:
		return "r";
	case K_KEY_S:
		return "s";
	case K_KEY_T:
		return "t";
	case K_KEY_U:
		return "u";
	case K_KEY_V:
		return "v";
	case K_KEY_W:
		return "w";
	case K_KEY_X:
		return "x";
	case K_KEY_Y:
		return "y";
	case K_KEY_Z:
		return "z";

	case K_KEY_LSUPER:
		return "l_super";
	case K_KEY_RSUPER:
		return "r_super";
	case K_KEY_APPS:
		return "apps";

	case K_KEY_SLEEP:
		return "sleep";

		// Numberpad keys
	case K_KEY_NUMPAD0:
		return "numpad_0";
	case K_KEY_NUMPAD1:
		return "numpad_1";
	case K_KEY_NUMPAD2:
		return "numpad_2";
	case K_KEY_NUMPAD3:
		return "numpad_3";
	case K_KEY_NUMPAD4:
		return "numpad_4";
	case K_KEY_NUMPAD5:
		return "numpad_5";
	case K_KEY_NUMPAD6:
		return "numpad_6";
	case K_KEY_NUMPAD7:
		return "numpad_7";
	case K_KEY_NUMPAD8:
		return "numpad_8";
	case K_KEY_NUMPAD9:
		return "numpad_9";
	case K_KEY_MULTIPLY:
		return "numpad_mult";
	case K_KEY_ADD:
		return "numpad_add";
	case K_KEY_SEPARATOR:
		return "numpad_sep";
	case K_KEY_SUBTRACT:
		return "numpad_sub";
	case K_KEY_DECIMAL:
		return "numpad_decimal";
	case K_KEY_DIVIDE:
		return "numpad_div";

	case K_KEY_F1:
		return "f1";
	case K_KEY_F2:
		return "f2";
	case K_KEY_F3:
		return "f3";
	case K_KEY_F4:
		return "f4";
	case K_KEY_F5:
		return "f5";
	case K_KEY_F6:
		return "f6";
	case K_KEY_F7:
		return "f7";
	case K_KEY_F8:
		return "f8";
	case K_KEY_F9:
		return "f9";
	case K_KEY_F10:
		return "f10";
	case K_KEY_F11:
		return "f11";
	case K_KEY_F12:
		return "f12";
	case K_KEY_F13:
		return "f13";
	case K_KEY_F14:
		return "f14";
	case K_KEY_F15:
		return "f15";
	case K_KEY_F16:
		return "f16";
	case K_KEY_F17:
		return "f17";
	case K_KEY_F18:
		return "f18";
	case K_KEY_F19:
		return "f19";
	case K_KEY_F20:
		return "f20";
	case K_KEY_F21:
		return "f21";
	case K_KEY_F22:
		return "f22";
	case K_KEY_F23:
		return "f23";
	case K_KEY_F24:
		return "f24";

	case K_KEY_NUMLOCK:
		return "num_lock";
	case K_KEY_SCROLL:
		return "scroll_lock";
	case K_KEY_NUMPAD_EQUAL:
		return "numpad_equal";

	case K_KEY_LSHIFT:
		return "l_shift";
	case K_KEY_RSHIFT:
		return "r_shift";
	case K_KEY_LCONTROL:
		return "l_ctrl";
	case K_KEY_RCONTROL:
		return "r_ctrl";
	case K_KEY_LALT:
		return "l_alt";
	case K_KEY_RALT:
		return "r_alt";

	case K_KEY_SEMICOLON:
		return ";";

	case K_KEY_APOSTROPHE:
		return "'";
	case K_KEY_EQUAL:
		return "=";
	case K_KEY_COMMA:
		return ",";
	case K_KEY_MINUS:
		return "-";
	case K_KEY_PERIOD:
		return ".";
	case K_KEY_SLASH:
		return "/";

	case K_KEY_GRAVE:
		return "`";

	case K_KEY_LBRACKET:
		return "[";
	case K_KEY_PIPE:
		return "\\";
	case K_KEY_RBRACKET:
		return "]";

	default:
		return "undefined";
	}
}

void input_keymap_push (const keymap *map) {
	if (state_ptr && map) {
		// Add the keymap to the stack, then apply it.
		if (!stack_push(&state_ptr->keymap_stack, (void *)map)) {
			KERROR("Failed to push keymap!");
			return;
		}
	}
}

b8 input_keymap_pop (void) {
	if (state_ptr) {
		// Pop the keymap from the stack, then re-apply the stack.
		keymap popped;
		return stack_pop(&state_ptr->keymap_stack, &popped);
	}

	return false;
}
