/**
 * @file binary_string_table.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of a binary string table.
 *
 * @details A binary string table is used to hold strings in a contiguous block of memory
 * in a way that is easily serialized and referenced. Strings are referenced by an index that
 * is returned when an entry is added. This allows data structures that are to be serialized (for example)
 * to simply store that index into this table, which itself can also be serialized into a binary block
 * within the same file and can be referenced later during deserialization.
 * @note: Any additions to a binary string table causes reallocations to occur by design. It's designed for
 * (de)serialization, not runtime performance, and should only be used in non-performance-critical code.
 *
 * @version 1.0
 * @date 2025-10-21
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2025
 *
 */
#pragma once

#include "defines.h"

// A single entry in a binary string table.
typedef struct binary_string_table_entry {
	u32 offset;
	u32 length;
} binary_string_table_entry;

// Header for the binary string table.
typedef struct binary_string_table_header {
	u32 entry_count;
	u64 data_block_size;
} binary_string_table_header;

// The runtime representation of a binary string table.
typedef struct binary_string_table {
	binary_string_table_header header;
	// Entry lookup
	binary_string_table_entry *lookup;
	// The data block holding all string data. Strings are NOT terminated since
	// thier offset and length is stored in the header entries' lookup.
	char *data;
} binary_string_table;

/**
 * Creates a binary string table.
 *
 * @return The newly-created binary string table.
 */
KAPI binary_string_table binary_string_table_create (void);

/**
 * Creates a binary string table from the given block of memory. This should have been created by
 * the binary_string_table_serialized() function for this to work correctly. Typically used when
 * reading from a file.
 *
 * @return The newly-created binary string table.
 */
KAPI binary_string_table binary_string_table_from_block (void *block);

/**
 * @brief Destroys the provided binary string table.
 *
 * @param table A pointer to the table to be destroyted.
 */
KAPI void binary_string_table_destroy (binary_string_table *table);

/**
 * @brief Adds the given string to the provided table. String MUST be null-terminated.
 *
 * @param table A pointer to the table to add to.
 * @param The null-terminated string to be added.
 * @return The index of the added string.
 */
KAPI u32 binary_string_table_add (binary_string_table *table, const char *string);

/**
 * @brief Returns a null-terminated copy of the string from the table. Dynamically allocated and must be freed by the caller.
 *
 * @param table A constant pointer to the table to search.
 * @param index The index of the string to look up.
 * @return A null-terminated copy of the string at the given index.
 */
KAPI const char *binary_string_table_get (const binary_string_table *table, u32 index);

/**
 * @brief Returns the length of the string, NOT accounting for null terminator.
 *
 * @param table A constant pointer to the table to search.
 * @param index The index of the string to look up.
 * @return The length of the given entry, NOT counting the null terminator.
 */
KAPI u32 binary_string_table_length_get (const binary_string_table *table, u32 index);
/**
 * @brief Copies string into already-existing buffer. Use binary_string_table_length_get to obtain the length of an entry's string.
 *
 * @param table A constant pointer to the table to search.
 * @param index The index of the string to look up.
 * @param buffer Allocated memory to hold the string data. Ideally should be length + 1 to account for a null terminator (although one is not added by this function)
 */
KAPI void binary_string_table_get_buffered (const binary_string_table *table, u32 index, char *buffer);

/**
 * @brief Serialize table to a single block of memory, tagged with MEMORY_TAG_BINARY_DATA. Should be freed by the caller.
 *
 * @param table A constant pointer to the table to serialize.
 * @param out_size A pointer to hold the total serialized block size.
 * @return A block of memory containing the serialized data.
 */
KAPI void *binary_string_table_serialized (const binary_string_table *table, u64 *out_size);
