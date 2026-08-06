#include "kstring_id.h"

#include "containers/u64_bst.h"
#include "debug/kassert.h"
#include "kstring.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "utils/crc64.h"

// Global lookup table for saved strings.
static bt_node *kstring_id_lookup = 0;

kstring_id kstring_id_create (const char *str) {
	if (!str || string_length(str) == 0) {
		KERROR("kstring_id_create requires a valid pointer to a string and the string must have a nonzero length.");
		return INVALID_KSTRING_ID;
	}

	// Hash the string.
	kstring_id new_string_id = crc64(0, (const u8 *)str, string_length(str));
	// NOTE: A hash of 0 is never allowed.
	KASSERT_MSG(new_string_id != 0, string_format("kstring_id_create - provided string '%s' hashed to 0, an invalid value. Please change the string to something else to avoid this.", str));

	// Register in a global lookup table if not already there.
	const bt_node *entry = u64_bst_find(kstring_id_lookup, new_string_id);
	if (!entry) {
		bt_node_value value;
		value.str = string_duplicate(str);
		bt_node *inserted = u64_bst_insert(kstring_id_lookup, new_string_id, value);
		if (!inserted) {
			KERROR("Failed to save kstring_id string '%s' to global lookup table.");
		} else if (!kstring_id_lookup) {
			kstring_id_lookup = inserted;
		}
	}
	return new_string_id;
}

const char *kstring_id_string_get (kstring_id stringid) {
	const bt_node *entry = u64_bst_find(kstring_id_lookup, stringid);
	if (entry) {
		// NOTE: For now, just return the existing pointer to the string.
		// If this ever becomes a problem, return a copy instead.
		return entry->value.str;
	}

	return 0;
}

char *kstring_id_join (const kstring_id *strings, u32 count, char delimiter) {
	if (!strings || !count) {
		return 0;
	}
	if (delimiter == 0) {
		KERROR("string_join cannot be used with a null terminator character as the delimiter.");
		return 0;
	}

	u32 total_length = 0;
	u32 *lengths = KALLOC_TYPE_CARRAY(u32, count);
	for (u32 i = 0; i < count; ++i) {
		const char *str = kstring_id_string_get(strings[i]);
		lengths[i] = string_length(str);
		total_length += lengths[i];
	}

	// Space for delimiters
	total_length += count;

	char *out_str = kallocate(sizeof(char) * total_length, MEMORY_TAG_STRING);
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		ksprintf(out_str + offset, "%s%c", kstring_id_string_get(strings[i]), delimiter);
		offset += lengths[i] + 1;
	}

	// Null-terminate the string
	out_str[total_length - 1] = 0;

	kfree(lengths);

	return out_str;
}

void kstring_id_shutdown (void) {
	u64_bst_cleanup_with_strings(kstring_id_lookup);
}
