#include "kname.h"

#include <stdarg.h>

#include "containers/u64_bst.h"
#include "debug/kassert.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "utils/crc64.h"

// Global lookup table for saved names.
static bt_node *string_lookup = 0;

kname kname_create (const char *str) {
	if (!str || string_length(str) == 0) {
		return INVALID_KNAME;
	}

	// Take a copy of the string to hash.
	char *copy = string_duplicate(str);
	// Convert it to lowercase _before_ hashing.
	string_to_lower(copy);

	// Hash the lowercase string.
	kname name = crc64(0, (const u8 *)copy, string_length(copy));
	// NOTE: A hash of 0 is never allowed.
	KASSERT_MSG(name != 0, string_format("kname_create - provided string '%s' hashed to 0, an invalid value. Please change the string to something else to avoid this.", str));

	// Dispose of the lowercase string.
	string_free(copy);

	// Register in a global lookup table if not already there.
	const bt_node *entry = u64_bst_find(string_lookup, name);
	if (!entry) {
		// Take a copy in case it was dynamically allocated and might
		// later be freed. Storing a copy of the *original* string for reference,
		// even though this is _not_ what is used for lookup.
		bt_node_value value;
		value.str = string_duplicate(str);
		bt_node *inserted = u64_bst_insert(string_lookup, name, value);
		if (!inserted) {
			KERROR("Failed to save kname string '%s' to global lookup table.");
		} else if (!string_lookup) {
			string_lookup = inserted;
		}
	}
	return name;
}

kname kname_format (const char *format, ...) {
	if (!format) {
		return 0;
	}

	__builtin_va_list arg_ptr;
	va_start(arg_ptr, format);
	char *result = string_format_v(format, arg_ptr);
	va_end(arg_ptr);

	kname new_kname = kname_create(result);
	string_free(result);

	return new_kname;
}

const char *kname_string_get (kname name) {
	if (name == INVALID_KNAME) {
		return 0;
	}

	const bt_node *entry = u64_bst_find(string_lookup, name);
	if (entry) {
		// NOTE: For now, just return the existing pointer to the string.
		// If this ever becomes a problem, return a copy instead.
		return entry->value.str;
	}

	return 0;
}

char *kname_join (const kname *strings, u32 count, char delimiter) {
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
		const char *str = kname_string_get(strings[i]);
		lengths[i] = string_length(str);
		total_length += lengths[i];
	}

	// Space for delimiters
	total_length += (count - 1);

	char *out_str = KALLOC_TYPE_CARRAY(char, total_length);
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		ksprintf(out_str + offset, "%s%c", kname_string_get(strings[i]), delimiter);
		offset += lengths[i] + 1;
	}

	// Overwrite the final delimiter character with null terminator.
	out_str[total_length - 1] = 0;

	kfree(lengths);

	return out_str;
}

void kname_shutdown (void) {
	u64_bst_cleanup_with_strings(string_lookup);
}
