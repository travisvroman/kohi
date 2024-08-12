#include "kname.h"

#include "containers/u64_bst.h"
#include "logger.h"
#include "strings/kstring.h"
#include "utils/crc64.h"

// Global lookup table for saved names.
static bt_node* string_lookup = 0;

kname kname_create(const char* str) {

    // Take a copy of the string to hash.
    char* copy = string_duplicate(str);
    // Convert it to lowercase _before_ hashing.
    string_to_lower(copy);

    // Hash the lowercase string.
    kname name = crc64(0, (const u8*)copy, string_length(copy));

    // Dispose of the lowercase string.
    string_free(copy);

    // Register in a global lookup table if not already there.
    const bt_node* entry = u64_bst_find(string_lookup, name);
    if (!entry) {
        // Take a copy in case it was dynamically allocated and might
        // later be freed. Storing a copy of the *original* string for reference,
        // even though this is _not_ what is used for lookup.
        bt_node_value value;
        value.str = string_duplicate(str);
        if (!u64_bst_insert(string_lookup, name, value)) {
            KERROR("Failed to save kname string '%s' to global lookup table.");
        }
    }
    return name;
}

const char* kname_string_get(kname name) {

    const bt_node* entry = u64_bst_find(string_lookup, name);
    if (entry) {
        // NOTE: For now, just return the existing pointer to the string.
        // If this ever becomes a problem, return a copy instead.
        return entry->value.str;
    }

    return 0;
}
