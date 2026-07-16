#include "keymap.h"
#include "memory/kmemory.h"

keymap keymap_create (void) {
	keymap map;
	kzero_memory(&map, sizeof(keymap));

	map.overrides_all = false;

	for (u32 i = 0; i < KEYS_MAX_KEYS; ++i) {
		map.entries[i].bindings = 0;
		map.entries[i].key = i;
	}

	return map;
}

void keymap_binding_add (keymap *map, keys key, keymap_entry_bind_type type, keymap_modifier modifiers, keymap_action_code code) {
	if (map) {
		keymap_entry *entry = &map->entries[key];
		keymap_binding *node = entry->bindings;
		keymap_binding *previous = entry->bindings;
		while (node) {
			previous = node;
			node = node->next;
		}

		keymap_binding *new_entry = kallocate(sizeof(keymap_binding), MEMORY_TAG_KEYMAP);
		new_entry->code = code;
		new_entry->modifiers = modifiers;
		new_entry->type = type;
		new_entry->next = 0;

		if (previous) {
			previous->next = new_entry;
		} else {
			entry->bindings = new_entry;
		}
	}
}

void keymap_binding_remove (keymap *map, keys key, keymap_entry_bind_type type, keymap_modifier modifiers, keymap_action_code code) {
	if (map) {
		keymap_entry *entry = &map->entries[key];
		keymap_binding *node = entry->bindings;
		keymap_binding *previous = entry->bindings;
		while (node) {
			if (node->code == code && node->modifiers == modifiers && node->type == type) {
				// Remove it
				previous->next = node->next;
				kfree(node);
				return;
			}
			previous = node;
			node = node->next;
		}
	}
}

void keymap_clear (keymap *map) {
	if (map) {
		for (u32 i = 0; i < KEYS_MAX_KEYS; ++i) {
			keymap_entry *entry = &map->entries[i];
			keymap_binding *node = entry->bindings;
			while (node) {
				// Remove all nodes
				keymap_binding *next = node->next;
				kfree(node);
				node = next;
			}
		}
	}
}
