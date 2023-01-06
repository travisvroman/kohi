#include "core/input.h"
#include "core/event.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "core/keymap.h"
#include "containers/stack.h"

typedef struct keyboard_state {
    b8 keys[256];
} keyboard_state;

typedef struct mouse_state {
    i16 x;
    i16 y;
    b8 buttons[BUTTON_MAX_BUTTONS];
} mouse_state;

typedef struct input_state {
    keyboard_state keyboard_current;
    keyboard_state keyboard_previous;
    mouse_state mouse_current;
    mouse_state mouse_previous;

    stack keymap_stack;
    // keymap active_keymap;
} input_state;

// Internal input state pointer
static input_state* state_ptr;

b8 check_modifiers(keymap_modifier modifiers);

void input_system_initialize(u64* memory_requirement, void* state) {
    *memory_requirement = sizeof(input_state);
    if (state == 0) {
        return;
    }
    kzero_memory(state, sizeof(input_state));
    state_ptr = state;

    // Create the keymap stack and an active keymap to apply to.
    stack_create(&state_ptr->keymap_stack, sizeof(keymap));
    // state_ptr->active_keymap = keymap_create();

    KINFO("Input subsystem initialized.");
}

void input_system_shutdown(void* state) {
    // TODO: Add shutdown routines when needed.
    state_ptr = 0;
}

void input_update(f64 delta_time) {
    if (!state_ptr) {
        return;
    }

    // Handle hold bindings.
    for (u32 k = 0; k < KEYS_MAX_KEYS; ++k) {
        keys key = (keys)k;
        if (input_is_key_down(key) && input_was_key_down(key)) {
            u32 map_count = state_ptr->keymap_stack.element_count;
            keymap* maps = (keymap*)state_ptr->keymap_stack.memory;
            for (i32 m = map_count - 1; m >= 0; --m) {
                keymap* map = &maps[m];
                keymap_binding* binding = &map->entries[key].bindings[0];
                b8 unset = false;
                while (binding) {
                    // If an unset is detected, stop processing.
                    if (binding->type == KEYMAP_BIND_TYPE_UNSET) {
                        unset = true;
                        break;
                    } else if (binding->type == KEYMAP_BIND_TYPE_HOLD) {
                        if (binding->callback && check_modifiers(binding->modifiers)) {
                            binding->callback(key, binding->type, binding->modifiers, binding->user_data);
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

b8 check_modifiers(keymap_modifier modifiers) {
    if (modifiers & KEYMAP_MODIFIER_SHIFT_BIT) {
        if (!input_is_key_down(KEY_SHIFT) && !input_is_key_down(KEY_LSHIFT) && !input_is_key_down(KEY_RSHIFT)) {
            return false;
        }
    }
    if (modifiers & KEYMAP_MODIFIER_CONTROL_BIT) {
        if (!input_is_key_down(KEY_CONTROL) && !input_is_key_down(KEY_LCONTROL) && !input_is_key_down(KEY_RCONTROL)) {
            return false;
        }
    }
    if (modifiers & KEYMAP_MODIFIER_ALT_BIT) {
        if (!input_is_key_down(KEY_LALT) && !input_is_key_down(KEY_RALT)) {
            return false;
        }
    }

    return true;
}

void input_process_key(keys key, b8 pressed) {
    // keymap_entry* map_entry = &state_ptr->active_keymap.entries[key];

    // Only handle this if the state actually changed.
    if (state_ptr && state_ptr->keyboard_current.keys[key] != pressed) {
        // Update internal state_ptr->
        state_ptr->keyboard_current.keys[key] = pressed;

        // if (key == KEY_LALT) {
        //     KINFO("Left alt %s.", pressed ? "pressed" : "released");
        // } else if (key == KEY_RALT) {
        //     KINFO("Right alt %s.", pressed ? "pressed" : "released");
        // }

        // if (key == KEY_LCONTROL) {
        //     KINFO("Left ctrl %s.", pressed ? "pressed" : "released");
        // } else if (key == KEY_RCONTROL) {
        //     KINFO("Right ctrl %s.", pressed ? "pressed" : "released");
        // }

        // if (key == KEY_LSHIFT) {
        //     KINFO("Left shift %s.", pressed ? "pressed" : "released");
        // } else if (key == KEY_RSHIFT) {
        //     KINFO("Right shift %s.", pressed ? "pressed" : "released");
        // }

        // Check for key bindings
        // Iterate keymaps top-down on the stack.
        u32 map_count = state_ptr->keymap_stack.element_count;
        keymap* maps = (keymap*)state_ptr->keymap_stack.memory;
        for (i32 m = map_count - 1; m >= 0; --m) {
            keymap* map = &maps[m];
            keymap_binding* binding = &map->entries[key].bindings[0];
            b8 unset = false;
            while (binding) {
                // If an unset is detected, stop processing.
                if (binding->type == KEYMAP_BIND_TYPE_UNSET) {
                    unset = true;
                    break;
                } else if (pressed && binding->type == KEYMAP_BIND_TYPE_PRESS) {
                    if (binding->callback && check_modifiers(binding->modifiers)) {
                        binding->callback(key, binding->type, binding->modifiers, binding->user_data);
                    }
                } else if (!pressed && binding->type == KEYMAP_BIND_TYPE_RELEASE) {
                    if (binding->callback && check_modifiers(binding->modifiers)) {
                        binding->callback(key, binding->type, binding->modifiers, binding->user_data);
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
        event_fire(pressed ? EVENT_CODE_KEY_PRESSED : EVENT_CODE_KEY_RELEASED, 0, context);
    }
}

void input_process_button(buttons button, b8 pressed) {
    // If the state changed, fire an event.
    if (state_ptr->mouse_current.buttons[button] != pressed) {
        state_ptr->mouse_current.buttons[button] = pressed;

        // Fire the event.
        event_context context;
        context.data.u16[0] = button;
        event_fire(pressed ? EVENT_CODE_BUTTON_PRESSED : EVENT_CODE_BUTTON_RELEASED, 0, context);
    }
}

void input_process_mouse_move(i16 x, i16 y) {
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
        event_fire(EVENT_CODE_MOUSE_MOVED, 0, context);
    }
}

void input_process_mouse_wheel(i8 z_delta) {
    // NOTE: no internal state to update.

    // Fire the event.
    event_context context;
    context.data.i8[0] = z_delta;
    event_fire(EVENT_CODE_MOUSE_WHEEL, 0, context);
}

b8 input_is_key_down(keys key) {
    if (!state_ptr) {
        return false;
    }
    return state_ptr->keyboard_current.keys[key] == true;
}

b8 input_is_key_up(keys key) {
    if (!state_ptr) {
        return true;
    }
    return state_ptr->keyboard_current.keys[key] == false;
}

b8 input_was_key_down(keys key) {
    if (!state_ptr) {
        return false;
    }
    return state_ptr->keyboard_previous.keys[key] == true;
}

b8 input_was_key_up(keys key) {
    if (!state_ptr) {
        return true;
    }
    return state_ptr->keyboard_previous.keys[key] == false;
}

// mouse input
b8 input_is_button_down(buttons button) {
    if (!state_ptr) {
        return false;
    }
    return state_ptr->mouse_current.buttons[button] == true;
}

b8 input_is_button_up(buttons button) {
    if (!state_ptr) {
        return true;
    }
    return state_ptr->mouse_current.buttons[button] == false;
}

b8 input_was_button_down(buttons button) {
    if (!state_ptr) {
        return false;
    }
    return state_ptr->mouse_previous.buttons[button] == true;
}

b8 input_was_button_up(buttons button) {
    if (!state_ptr) {
        return true;
    }
    return state_ptr->mouse_previous.buttons[button] == false;
}

void input_get_mouse_position(i32* x, i32* y) {
    if (!state_ptr) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = state_ptr->mouse_current.x;
    *y = state_ptr->mouse_current.y;
}

void input_get_previous_mouse_position(i32* x, i32* y) {
    if (!state_ptr) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = state_ptr->mouse_previous.x;
    *y = state_ptr->mouse_previous.y;
}

const char* input_keycode_str(keys key) {
    switch (key) {
        case KEY_BACKSPACE:
            return "backspace";
        case KEY_ENTER:
            return "enter";
        case KEY_TAB:
            return "tab";
        case KEY_SHIFT:
            return "shift";
        case KEY_CONTROL:
            return "ctrl";
        case KEY_PAUSE:
            return "pause";
        case KEY_CAPITAL:
            return "capslock";
        case KEY_ESCAPE:
            return "esc";

        case KEY_CONVERT:
            return "ime_convert";
        case KEY_NONCONVERT:
            return "ime_noconvert";
        case KEY_ACCEPT:
            return "ime_accept";
        case KEY_MODECHANGE:
            return "ime_modechange";

        case KEY_SPACE:
            return "space";
        case KEY_PAGEUP:
            return "pageup";
        case KEY_PAGEDOWN:
            return "pagedown";
        case KEY_END:
            return "end";
        case KEY_HOME:
            return "home";
        case KEY_LEFT:
            return "left";
        case KEY_UP:
            return "up";
        case KEY_RIGHT:
            return "right";
        case KEY_DOWN:
            return "down";
        case KEY_SELECT:
            return "select";
        case KEY_PRINT:
            return "print";
        case KEY_EXECUTE:
            return "execute";
        case KEY_PRINTSCREEN:
            return "printscreen";
        case KEY_INSERT:
            return "insert";
        case KEY_DELETE:
            return "delete";
        case KEY_HELP:
            return "help";

        case KEY_0:
            return "0";
        case KEY_1:
            return "1";
        case KEY_2:
            return "2";
        case KEY_3:
            return "3";
        case KEY_4:
            return "4";
        case KEY_5:
            return "5";
        case KEY_6:
            return "6";
        case KEY_7:
            return "7";
        case KEY_8:
            return "8";
        case KEY_9:
            return "9";

        case KEY_A:
            return "a";
        case KEY_B:
            return "b";
        case KEY_C:
            return "c";
        case KEY_D:
            return "d";
        case KEY_E:
            return "e";
        case KEY_F:
            return "f";
        case KEY_G:
            return "g";
        case KEY_H:
            return "h";
        case KEY_I:
            return "i";
        case KEY_J:
            return "j";
        case KEY_K:
            return "k";
        case KEY_L:
            return "l";
        case KEY_M:
            return "m";
        case KEY_N:
            return "n";
        case KEY_O:
            return "o";
        case KEY_P:
            return "p";
        case KEY_Q:
            return "q";
        case KEY_R:
            return "r";
        case KEY_S:
            return "s";
        case KEY_T:
            return "t";
        case KEY_U:
            return "u";
        case KEY_V:
            return "v";
        case KEY_W:
            return "w";
        case KEY_X:
            return "x";
        case KEY_Y:
            return "y";
        case KEY_Z:
            return "z";

        case KEY_LSUPER:
            return "l_super";
        case KEY_RSUPER:
            return "r_super";
        case KEY_APPS:
            return "apps";

        case KEY_SLEEP:
            return "sleep";

            // Numberpad keys
        case KEY_NUMPAD0:
            return "numpad_0";
        case KEY_NUMPAD1:
            return "numpad_1";
        case KEY_NUMPAD2:
            return "numpad_2";
        case KEY_NUMPAD3:
            return "numpad_3";
        case KEY_NUMPAD4:
            return "numpad_4";
        case KEY_NUMPAD5:
            return "numpad_5";
        case KEY_NUMPAD6:
            return "numpad_6";
        case KEY_NUMPAD7:
            return "numpad_7";
        case KEY_NUMPAD8:
            return "numpad_8";
        case KEY_NUMPAD9:
            return "numpad_9";
        case KEY_MULTIPLY:
            return "numpad_mult";
        case KEY_ADD:
            return "numpad_add";
        case KEY_SEPARATOR:
            return "numpad_sep";
        case KEY_SUBTRACT:
            return "numpad_sub";
        case KEY_DECIMAL:
            return "numpad_decimal";
        case KEY_DIVIDE:
            return "numpad_div";

        case KEY_F1:
            return "f1";
        case KEY_F2:
            return "f2";
        case KEY_F3:
            return "f3";
        case KEY_F4:
            return "f4";
        case KEY_F5:
            return "f5";
        case KEY_F6:
            return "f6";
        case KEY_F7:
            return "f7";
        case KEY_F8:
            return "f8";
        case KEY_F9:
            return "f9";
        case KEY_F10:
            return "f10";
        case KEY_F11:
            return "f11";
        case KEY_F12:
            return "f12";
        case KEY_F13:
            return "f13";
        case KEY_F14:
            return "f14";
        case KEY_F15:
            return "f15";
        case KEY_F16:
            return "f16";
        case KEY_F17:
            return "f17";
        case KEY_F18:
            return "f18";
        case KEY_F19:
            return "f19";
        case KEY_F20:
            return "f20";
        case KEY_F21:
            return "f21";
        case KEY_F22:
            return "f22";
        case KEY_F23:
            return "f23";
        case KEY_F24:
            return "f24";

        case KEY_NUMLOCK:
            return "num_lock";
        case KEY_SCROLL:
            return "scroll_lock";
        case KEY_NUMPAD_EQUAL:
            return "numpad_equal";

        case KEY_LSHIFT:
            return "l_shift";
        case KEY_RSHIFT:
            return "r_shift";
        case KEY_LCONTROL:
            return "l_ctrl";
        case KEY_RCONTROL:
            return "r_ctrl";
        case KEY_LALT:
            return "l_alt";
        case KEY_RALT:
            return "r_alt";

        case KEY_SEMICOLON:
            return ";";

        case KEY_APOSTROPHE:
            return "'";
        case KEY_EQUAL:
            return "=";
        case KEY_COMMA:
            return ",";
        case KEY_MINUS:
            return "-";
        case KEY_PERIOD:
            return ".";
        case KEY_SLASH:
            return "/";

        case KEY_GRAVE:
            return "`";

        case KEY_LBRACKET:
            return "[";
        case KEY_PIPE:
            return "\\";
        case KEY_RBRACKET:
            return "]";

        default:
            return "undefined";
    }
}

void input_keymap_push(const keymap* map) {
    if (state_ptr && map) {
        // Add the keymap to the stack, then apply it.
        if (!stack_push(&state_ptr->keymap_stack, (void*)map)) {
            KERROR("Failed to push keymap!");
            return;
        }
    }
}

void input_keymap_pop() {
    if (state_ptr) {
        // Pop the keymap from the stack, then re-apply the stack.
        keymap popped;
        if (!stack_pop(&state_ptr->keymap_stack, &popped)) {
            KERROR("Failed to pop keymap!");
            return;
        }
    }
}
