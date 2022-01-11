/**
 * @file darray.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of a dynamic array.
 * 
 * @details 
 * Memory layout:
 * - u64 capacity = number elements that can be held.
 * - u64 length = number of elements currently contained
 * - u64 stride = size of each element in bytes
 * - void* elements
 * @version 1.0
 * @date 2022-01-10
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "defines.h"

enum {
    DARRAY_CAPACITY,
    DARRAY_LENGTH,
    DARRAY_STRIDE,
    DARRAY_FIELD_LENGTH
};

/**
 * @brief Creates a new darray of the given length and stride.
 * Note that this performs a dynamic memory allocation. 
 * @note Avoid using this directly; use the darray_create macro instead.
 * @param length The default number of elements in the array.
 * @param stride The size of each array element.
 * @returns A pointer representing the block of memory containing the array.
 */
KAPI void* _darray_create(u64 length, u64 stride);

/**
 * @brief destroys the given array, freeing resources. Frees associated memory.
 * @note Avoid using this function directly. Use the darray_destroy macro instead.
 * @param array The array to be destroyed.
 */
KAPI void _darray_destroy(void* array);

/**
 * @brief Returns the value for the given field.
 * @note This is an internal feature, use public macros instead such as darray_length.
 * @param array The array to retrieve the field for.
 * @param field The index of the field to be retrieved.
 * @returns The value for the given field.
 */
KAPI u64 _darray_field_get(void* array, u64 field);

/**
 * @brief Sets the value for the given field.
 * @note This is an internal feature and should not be called directly.
 * @param array The array to set the field for.
 * @param field The index of the field to be set.
 * @param value The value to be set for the given field.
 */
KAPI void _darray_field_set(void* array, u64 field, u64 value);

/**
 * @brief Resizes the given array using internal resizing amounts.
 * Causes a new allocation.
 * @note This is an internal implementation detail and should not be called directly.
 * @param array The array to be resized.
 * @returns A pointer to the resized array block.
 */
KAPI void* _darray_resize(void* array);

/**
 * @brief Pushes a new entry to the given array. Resizes if necessary.
 * @note Avoid using this directly; call the darray_push macro instead.
 * @param array The array to be pushed to.
 * @param value_ptr A pointer to the value to be pushed. A copy of this value is taken.
 * @returns A pointer to the array block.
 */
KAPI void* _darray_push(void* array, const void* value_ptr);

/**
 * @brief Pops an entry out of the array and places it into dest.
 * @note Avoid using this directly; call the darray_pop macro instead.
 * @param array The array to pop from.
 * @param dest A pointer to hold the popped value.
 */
KAPI void _darray_pop(void* array, void* dest);

/**
 * @brief Pops an entry out of the array at the given index and places it into dest.
 * Brings in all entries after the popped index in by one.
 * @note Avoid using this directly; call the darray_pop_at macro instead.
 * @param array The array to pop from.
 * @param index The index to pop from.
 * @param dest A pointer to hold the popped value.
 * @returns The array block.
 */
KAPI void* _darray_pop_at(void* array, u64 index, void* dest);

/**
 * @brief Inserts a copy of the given value into the supplied array at the given index.
 * Triggers an array resize if required.
 * @note Avoid using this directly; call the darray_insert_at macro instead.
 * @param array The array to insert into.
 * @param index The index to insert at.
 * @param value_ptr A pointer holding the value to be inserted.
 * @returns The array block.
 */
KAPI void* _darray_insert_at(void* array, u64 index, void* value_ptr);

/** @brief The default darray capacity. */
#define DARRAY_DEFAULT_CAPACITY 1

/** @brief The default resize factor (doubles on resize) */
#define DARRAY_RESIZE_FACTOR 2

/**
 * @brief Creates a new darray of the given type with the default capacity. 
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @returns A pointer to the array's memory block.
 */
#define darray_create(type) \
    _darray_create(DARRAY_DEFAULT_CAPACITY, sizeof(type))

/**
 * @brief Creates a new darray of the given type with the provided capacity. 
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @returns A pointer to the array's memory block.
 */
#define darray_reserve(type, capacity) \
    _darray_create(capacity, sizeof(type))

/**
 * @brief Destroys the provided array, freeing any memory allocated by it.
 * @param array The array to be destroyed.
 */
#define darray_destroy(array) _darray_destroy(array);

/**
 * @brief Pushes a new entry to the given array. Resizes if necessary.
 * @param array The array to be pushed to.
 * @param value The value to be pushed. A copy of this value is taken.
 * @returns A pointer to the array block.
 */
#define darray_push(array, value)           \
    {                                       \
        typeof(value) temp = value;         \
        array = _darray_push(array, &temp); \
    }
// NOTE: could use __auto_type for temp above, but intellisense
// for VSCode flags it as an unknown type. typeof() seems to
// work just fine, though. Both are GNU extensions.

/**
 * @brief Pops an entry out of the array and places it into dest.
 * @param array The array to pop from.
 * @param dest A pointer to hold the popped value.
 */
#define darray_pop(array, value_ptr) \
    _darray_pop(array, value_ptr)

/**
 * @brief Inserts a copy of the given value into the supplied array at the given index.
 * Triggers an array resize if required.
 * @param array The array to insert into.
 * @param index The index to insert at.
 * @param value_ptr A pointer holding the value to be inserted.
 * @returns The array block.
 */
#define darray_insert_at(array, index, value)           \
    {                                                   \
        typeof(value) temp = value;                     \
        array = _darray_insert_at(array, index, &temp); \
    }

/**
 * @brief Pops an entry out of the array at the given index and places it into dest.
 * Brings in all entries after the popped index in by one.
 * @param array The array to pop from.
 * @param index The index to pop from.
 * @param dest A pointer to hold the popped value.
 * @returns The array block.
 */
#define darray_pop_at(array, index, value_ptr) \
    _darray_pop_at(array, index, value_ptr)

/**
 * @brief Clears all entries from the array. Does not release any internally-allocated memory.
 * @param array The array to be cleared.
 */
#define darray_clear(array) \
    _darray_field_set(array, DARRAY_LENGTH, 0)

/**
 * @brief Gets the given array's capacity.
 * @param array The array whose capacity to retrieve.
 * @returns The capacity of the given array.
 */
#define darray_capacity(array) \
    _darray_field_get(array, DARRAY_CAPACITY)

/**
 * @brief Gets the length (number of elements) in the given array.
 * @param array The array to obtain the length of.
 * @returns The length of the given array.
 */
#define darray_length(array) \
    _darray_field_get(array, DARRAY_LENGTH)

/**
 * @brief Gets the stride (element size) of the given array.
 * @param array The array to obtain the stride of.
 * @returns The stride of the given array.
 */
#define darray_stride(array) \
    _darray_field_get(array, DARRAY_STRIDE)

/**
 * @brief Sets the length of the given array. This ensures the array has the
 * required capacity to be able to set entries directly, for instance. Can trigger
 * an internal reallocation.
 * @param array The array to set the length of.
 * @param value The length to set the array to.
 */
#define darray_length_set(array, value) \
    _darray_field_set(array, DARRAY_LENGTH, value)
