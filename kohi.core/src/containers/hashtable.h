/**
 * @file hashtable.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the hashtable implementation.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"
#include "strings/kstring.h"

/**
 * @brief Represents a simple hashtable. Members of this structure
 * should not be modified outside the functions associated with it.
 */
typedef struct hashtable {
	u64 capacity;
	u64 element_size;
	u64 filled;
	void* memory;
	b8 is_pointer_type;
} hashtable;

/**
 * @brief Creates a hashtable and stores it in out_hashtable.
 *
 * @param element_size The size of each element in bytes.
 * @param initial_capacity Initial capacity of the hashtable.
 * @param is_pointer_type Indicates if this hashtable will hold pointer types.
 * @param out_hashtable A pointer to a hashtable in which to hold relevant data.
 */
KAPI void hashtable_create(u64 element_size, u64 initial_capacity, b8 is_pointer_type, hashtable* out_hashtable);

/**
 * @brief Destroys the provided hashtable. Does not release memory for pointer types.
 *
 * @param table A pointer to the table to be destroyed.
 */
KAPI void hashtable_destroy(hashtable* table);

/**
 * @brief Stores a copy of the data in value in the provided hashtable.
 * Only use for tables which were *NOT* created with is_pointer_type = true.
 *
 * @param table A pointer to the table to get from. Required.
 * @param name The name of the entry to set. Required.
 * @param value The value to be set. Required.
 * @return True, or false if a null pointer is passed.
 */
KAPI b8 hashtable_set(hashtable* table, const char* name, const void* value);

/**
 * @brief Obtains a copy of data present in the hashtable.
 * Only use for tables which were *NOT* created with is_pointer_type = true.
 *
 * @param table A pointer to the table to retrieved from. Required.
 * @param name The name of the entry to retrieved. Required.
 * @param value A pointer to store the retrieved value. Required.
 * @return True; or false if a null pointer is passed.
 */
KAPI b8 hashtable_get(hashtable* table, const char* name, void* out_value);

/**
 * @brief Pointer to a function that will be called on every object stored inside hashtable
 * when passed to the `hashtable_foreach`.
 */
typedef void (*hashtable_foreach_fn)(u64 index, const kstring* key, void* data, void* user_data);

/**
 * @brief Calls a foreach_fn on every object stored inside hashtable.
 *
 * @param table A pointer to the table to iterate on. Required.
 * @param foreach_fn Foreach function that will be called on every object. Required.
 * @param user_data User data that is passed to the foreach_fn.
 */
KAPI void hashtable_foreach(hashtable* table, hashtable_foreach_fn foreach_fn, void* user_data);
