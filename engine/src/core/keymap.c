#include "keymap.h"
#include "core/kmemory.h"

keymap keymap_create() {
    keymap map;
    kzero_memory(&map, sizeof(keymap));

    map.overrides_all = false;

    for (u32 i = 0; i < KEYS_MAX_KEYS; ++i) {
        map.entries[i].bindings = 0;
        map.entries[i].key = i;
    }

    return map;
}

void keymap_binding_add(keymap* map, keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data, PFN_keybind_callback callback) {
    if (map) {
        keymap_entry* entry = &map->entries[key];
        keymap_binding* node = entry->bindings;
        keymap_binding* previous = entry->bindings;
        while (node) {
            previous = node;
            node = node->next;
        }

        keymap_binding* new_entry = kallocate(sizeof(keymap_binding), MEMORY_TAG_UNKNOWN);
        new_entry->callback = callback;
        new_entry->modifiers = modifiers;
        new_entry->type = type;
        new_entry->next = 0;
        new_entry->user_data = user_data;

        if (previous) {
            previous->next = new_entry;
        } else {
            entry->bindings = new_entry;
        }
    }
}

void keymap_binding_remove(keymap* map, keys key, keymap_entry_bind_type type, keymap_modifier modifiers, PFN_keybind_callback callback) {
    if (map) {
        keymap_entry* entry = &map->entries[key];
        keymap_binding* node = entry->bindings;
        keymap_binding* previous = entry->bindings;
        while (node) {
            if (node->callback == callback && node->modifiers == modifiers && node->type == type) {
                // Remove it
                previous->next = node->next;
                kfree(node, sizeof(keymap_binding), MEMORY_TAG_UNKNOWN);
                return;
            }
            previous = node;
            node = node->next;
        }
    }
}