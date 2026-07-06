#include "hashtable.h"

#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

#define MAX_OCCUPANCY 0.75f
#define GROW_FACTOR 2

typedef u8 ht_control;

typedef u64 ht_h1; // First part of the hash, offset into the start position.
typedef u8 ht_h2;  // Second part of the hash, check-sum for faster iteration.

// h1 is stored in last 7 bytes of the 8-byte hash,
// h2 is stored in the first byte.
#define GET_H1(_hash) (((_hash) & ~0xFFULL) >> 8)
#define GET_H2(_hash) ((_hash) & 0xFF)

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV_PRIME 0x00000100000001b3
#define FNV_OFFSET_BASIS 0xcbf29ce484222325
static u64 fnv_1a(const char* str) {
	u64 hash = FNV_OFFSET_BASIS;
	for (; *str; str++) {
		hash ^= *str;
		hash *= FNV_PRIME;
	}
	return hash;
}

static void grow_and_rehash(hashtable* table);

static ht_control* get_control_block(hashtable* table);
static kstring* get_key_block(hashtable* table);
static void* get_data_block(hashtable* table);

void hashtable_create(u64 element_size, u64 initial_capacity, b8 is_pointer_type, hashtable* out_hashtable) {
	if (!out_hashtable) {
		KERROR("hashtable_create failed! Pointer to out_hashtable is required.");
		return;
	}
	if (!initial_capacity || !element_size) {
		KERROR("element_size and initial_capacity must be a positive non-zero value.");
		return;
	}

	out_hashtable->capacity = initial_capacity;
	out_hashtable->element_size = element_size;
	out_hashtable->filled = 0;
	out_hashtable->is_pointer_type = is_pointer_type;

	u64 allocation_size = (sizeof(ht_control) + sizeof(kstring) + element_size) * initial_capacity;
	out_hashtable->memory = kallocate(allocation_size, MEMORY_TAG_HASHTABLE);
	kzero_memory(out_hashtable->memory, allocation_size);
}

void hashtable_destroy(hashtable* table) {
	if (!table) {
		KERROR("hashtable_destroy failed! Pointer to table is required.");
		return;
	}

	u64 allocation_size = (sizeof(ht_control) + sizeof(kstring) + table->element_size) * table->capacity;

	if (table->filled > 0) {
		ht_control* control_block = get_control_block(table);
		kstring* key_block = get_key_block(table);
		for (u64 i = 0; i < table->capacity; i++) {
			if (control_block[i] != 0) {
				kstring_destroy(&key_block[i]);
			}
		}
	}

	kfree(table->memory, allocation_size, MEMORY_TAG_HASHTABLE);
	kzero_memory(table, sizeof(hashtable));
}

b8 hashtable_set(hashtable* table, const char* name, const void* value) {
	if (!table || !name) {
		KERROR("hashtable_set requires table and name to exist.");
		return false;
	}

	if (!value && !table->is_pointer_type) {
		KERROR("hashtable_set requires valid pointer to value when called on non-pointer type hashtable.");
		return false;
	}

	u64 hash = fnv_1a(name);
	ht_h1 h1 = GET_H1(hash);
	ht_h2 h2 = GET_H2(hash);

	u64 pos = h1 % table->capacity;

	ht_control* control_block = get_control_block(table);
	kstring* key_block = get_key_block(table);
	void* data_block = get_data_block(table);

	while (1) {
		if (control_block[pos] == 0) {
			// cells is empty, set H2 check-sum, key and value.
			control_block[pos] = h2;
			kstring_from_cstring(name, &key_block[pos]);
			void* slot = data_block + (table->element_size * pos);
			if (table->is_pointer_type)
				*(void**)slot = (void*)value;
			else
				kcopy_memory(slot, value, table->element_size);

			table->filled += 1;

			if ((f32)table->filled / (f32)table->capacity >= MAX_OCCUPANCY) {
				grow_and_rehash(table);
			}

			break;
		} else if (control_block[pos] == h2 && strings_equal(name, key_block[pos].data)) {
			// cell with identical H2 check-sum and key, replace its content.
			void* slot = data_block + (table->element_size * pos);
			if (table->is_pointer_type)
				*(void**)slot = (void*)value;
			else
				kcopy_memory(slot, value, table->element_size);

			break;
		}

		// Cycle until we find a suitable cell.
		pos = (pos + 1) % table->capacity;
	}

	return true;
}

b8 hashtable_get(hashtable* table, const char* name, void* out_value) {
	if (!table || !name || !out_value) {
		KWARN("hashtable_get requires table, name and out_value to exist.");
		return false;
	}

	u64 hash = fnv_1a(name);
	ht_h1 h1 = GET_H1(hash);
	ht_h2 h2 = GET_H2(hash);

	u64 pos = h1 % table->capacity;

	ht_control* control_block = get_control_block(table);
	kstring* key_block = get_key_block(table);
	void* data_block = get_data_block(table);

	while (1) {
		if (control_block[pos] == 0) // Cell is empty, break from the loop.
			break;
		else if (control_block[pos] == h2 && strings_equal(name, key_block[pos].data)) {
			// Cell found, set the out_value and return true.
			void* slot = data_block + (table->element_size * pos);
			if (table->is_pointer_type)
				*(void**)out_value = *(void**)slot;
			else
				kcopy_memory(out_value, slot, table->element_size);

			return true;
		}

		// Cycle until we find a required cell (or empty).
		pos = (pos + 1) % table->capacity;
	}

	// Value is not presented in the hashtable.
	return false;
}

void hashtable_foreach(hashtable* table, hashtable_foreach_fn foreach_fn, void* user_data) {
	if (!table || !foreach_fn) {
		KWARN("hashtable_foreach requires table and a foreach_fn");
		return;
	}

	ht_control* control_block = get_control_block(table);
	kstring* key_block = get_key_block(table);
	void* data_block = get_data_block(table);

	for (u64 i = 0; i < table->capacity; i++) {
		if (control_block[i] != 0) {
			foreach_fn(i, &key_block[i], data_block + (table->element_size * i), user_data);
		}
	}
}

static void grow_and_rehash(hashtable* table) {
	u64 old_capacity = table->capacity;
	u64 old_allocation_size = (sizeof(ht_control) + sizeof(kstring) + table->element_size) * old_capacity;
	void* old_memory = table->memory;
	ht_control* old_control_block = get_control_block(table);
	kstring* old_key_block = get_key_block(table);
	void* old_data_block = get_data_block(table);

	u64 new_capacity = old_capacity * GROW_FACTOR;
	u64 new_allocation_size = (sizeof(ht_control) + sizeof(kstring) + table->element_size) * new_capacity;
	table->capacity = new_capacity;
	table->memory = kallocate(new_allocation_size, MEMORY_TAG_HASHTABLE);
	ht_control* new_control_block = get_control_block(table);
	kstring* new_key_block = get_key_block(table);
	void* new_data_block = get_data_block(table);

	for (u64 i = 0; i < old_capacity; i++) {
		if (old_control_block[i] == 0)
			continue;

		kstring* key = &old_key_block[i];
		u64 hash = fnv_1a(key->data);
		ht_h1 h1 = GET_H1(hash);
		ht_h2 h2 = GET_H2(hash);

		u64 pos = h1 % table->capacity;

		while (1) {
			// We assume that FNV-1a is good enough to avoid H2 collisions, so only check for empty cells.
			if (new_control_block[pos] == 0) {
				new_control_block[pos] = h2;
				kcopy_memory(&new_key_block[pos], key, sizeof(kstring));

				void* slot_dst = new_data_block + (table->element_size * pos);
				void* slot_src = old_data_block + (table->element_size * i);
				if (table->is_pointer_type)
					*(void**)slot_dst = *(void**)slot_src;
				else
					kcopy_memory(slot_dst, slot_src, table->element_size);

				break;
			}

			pos = (pos + 1) % table->capacity;
		}
	}

	kfree(old_memory, old_allocation_size, MEMORY_TAG_HASHTABLE);
}

static ht_control* get_control_block(hashtable* table) {
	return table->memory + 0;
}

static kstring* get_key_block(hashtable* table) {
	return table->memory + sizeof(ht_control) * table->capacity;
}

static void* get_data_block(hashtable* table) {
	return table->memory + (sizeof(ht_control) + sizeof(kstring)) * table->capacity;
}
