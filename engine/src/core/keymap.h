#pragma once

#include "defines.h"
#include "input.h"

typedef enum keymap_modifier_bits {
    KEYMAP_MODIFIER_NONE_BIT = 0x0,
    KEYMAP_MODIFIER_SHIFT_BIT = 0x1,
    KEYMAP_MODIFIER_CONTROL_BIT = 0x2,
    KEYMAP_MODIFIER_ALT_BIT = 0x4,
} keymap_modifier_bits;

typedef u32 keymap_modifier;

typedef enum keymap_entry_bind_type {
    /** @brief An undefined mapping that can be overridden. */
    KEYMAP_BIND_TYPE_UNDEFINED = 0x0,
    /** @brief Callback is made when key is initially pressed. */
    KEYMAP_BIND_TYPE_PRESS = 0x1,
    /** @brief Callback is made when key is released. */
    KEYMAP_BIND_TYPE_RELEASE = 0x2,
    /** @brief Callback is made continuously while key is held. */
    KEYMAP_BIND_TYPE_HOLD = 0x4,
    /** @brief Used to disable a key binding on a lower-level map. */
    KEYMAP_BIND_TYPE_UNSET = 0x8
} keymap_entry_bind_type;

typedef void (*PFN_keybind_callback)(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);

typedef struct keymap_binding {
    keymap_entry_bind_type type;
    keymap_modifier modifiers;
    PFN_keybind_callback callback;
    void* user_data;
    struct keymap_binding* next;
} keymap_binding;

typedef struct keymap_entry {
    keys key;
    // Linked list of bindings.
    keymap_binding* bindings;
} keymap_entry;

typedef struct keymap {
    b8 overrides_all;
    keymap_entry entries[KEYS_MAX_KEYS];
} keymap;

KAPI keymap keymap_create();

KAPI void keymap_binding_add(keymap* map, keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data, PFN_keybind_callback callback);
KAPI void keymap_binding_remove(keymap* map, keys key, keymap_entry_bind_type type, keymap_modifier modifiers, PFN_keybind_callback callback);

// TODO(travis): Keymaps will replace the existing
// checks for key states in that they will instead
// call callback functions instead.
// Maps will be added onto a stack, where bindings are
// replaced along the way. For example, if the base keymap
// defines the "escape" key as an application quit, then
// another keymap added on re-defines the key to nothing while
// adding a new binding for "a", then "a"'s binding will work,
// and "escape" will do nothing. If "escape" were left undefined
// in the second keymap, the original mapping is left unchanged.
// Maps are pushed/popped as expected on a stack.