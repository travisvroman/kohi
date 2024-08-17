/**
 * @file darray.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of a dynamic array.
 *
 * @details
 * Memory layout:
 * - u64 capacity = number elements that can be held. (8 bytes)
 * - u64 length = number of elements currently contained (8 bytes)
 * - u64 stride = size of each element in bytes (8 bytes)
 * - void* allocator (if used, otherwise 0) (8 bytes)
 * - void* elements
 * @version 2.0
 * @date 2023-08-30
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#pragma once

#include "defines.h"

typedef struct darray_header {
    u64 capacity;
    u64 length;
    u64 stride;
    struct frame_allocator_int* allocator;
} darray_header;

/**
 * @brief Creates a new darray of the given length and stride.
 * Note that this performs a dynamic memory allocation.
 * @note Avoid using this directly; use the darray_create macro instead.
 * @param length The default number of elements in the array.
 * @param stride The size of each array element.
 * @returns A pointer representing the block of memory containing the array.
 */
KAPI void* _darray_create(u64 length, u64 stride, struct frame_allocator_int* frame_allocator);

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
    (type*)_darray_create(DARRAY_DEFAULT_CAPACITY, sizeof(type), 0)

/**
 * @brief Creates a new darray of the given type with the default capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param allocator A pointer to a frame allocator.
 * @returns A pointer to the array's memory block.
 */
#define darray_create_with_allocator(type, allocator) \
    _darray_create(DARRAY_DEFAULT_CAPACITY, sizeof(type), allocator)

/**
 * @brief Creates a new darray of the given type with the provided capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @returns A pointer to the array's memory block.
 */
#define darray_reserve(type, capacity) \
    _darray_create(capacity, sizeof(type), 0)

/**
 * @brief Creates a new darray of the given type with the provided capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @param allocator A pointer to a frame allocator.
 * @returns A pointer to the array's memory block.
 */
#define darray_reserve_with_allocator(type, capacity, allocator) \
    _darray_create(capacity, sizeof(type), allocator)

/**
 * @brief Destroys the provided array, freeing any memory allocated by it.
 * @param array The array to be destroyed.
 */
KAPI void darray_destroy(void* array);

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
 * @brief Pops an entry out of the array and places it into dest (if provided).
 * @param array The array to pop from.
 * @param dest A pointer to hold the popped value. Optional.
 */
KAPI void darray_pop(void* array, void* dest);

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
 * @brief Pops an entry out of the array at the given index and places it into dest (if provided).
 * Brings in all entries after the popped index in by one.
 * @param array The array to pop from.
 * @param index The index to pop from.
 * @param dest A pointer to hold the popped value. Optional.
 * @returns The array block.
 */
KAPI void* darray_pop_at(void* array, u64 index, void* dest);

/**
 * @brief Clears all entries from the array. Does not release any internally-allocated memory.
 * @param array The array to be cleared.
 */
KAPI void darray_clear(void* array);

/**
 * @brief Gets the given array's capacity.
 * @param array The array whose capacity to retrieve.
 * @returns The capacity of the given array.
 */
KAPI u64 darray_capacity(void* array);

/**
 * @brief Gets the length (number of elements) in the given array.
 * @param array The array to obtain the length of.
 * @returns The length of the given array.
 */
KAPI u64 darray_length(void* array);

/**
 * @brief Gets the stride (element size) of the given array.
 * @param array The array to obtain the stride of.
 * @returns The stride of the given array.
 */
KAPI u64 darray_stride(void* array);

/**
 * @brief Sets the length of the given array. This ensures the array has the
 * required capacity to be able to set entries directly, for instance. Can trigger
 * an internal reallocation.
 * @param array The array to set the length of.
 * @param value The length to set the array to.
 */
KAPI void darray_length_set(void* array, u64 value);

/**
 * NEW DARRAY
 */

#include "debug/kassert.h"

KAPI void _kdarray_init(u32 length, u32 stride, u32 capacity, struct frame_allocator_int* allocator, u32* out_length, u32* out_stride, u32* out_capacity, void** block, struct frame_allocator_int** out_allocator);
KAPI void _kdarray_free(u32* length, u32* capacity, u32* stride, void** block, struct frame_allocator_int** out_allocator);
KAPI void _kdarray_ensure_size(u32 required_length, u32 stride, u32* out_capacity, struct frame_allocator_int* allocator, void** block);

#define DARRAY_TYPE_NAMED(type, name)                                                                                                                   \
    typedef struct name##_darray {                                                                                                                      \
        u32 length;                                                                                                                                     \
        u32 stride;                                                                                                                                     \
        u32 capacity;                                                                                                                                   \
        type* data;                                                                                                                                     \
        struct frame_allocator_int* allocator;                                                                                                          \
    } name##_darray;                                                                                                                                    \
    typedef struct name##_darray_it {                                                                                                                   \
        name##_darray* arr;                                                                                                                             \
        u32 pos;                                                                                                                                        \
    } name##_darray_it;                                                                                                                                 \
                                                                                                                                                        \
    KINLINE name##_darray name##_darray_reserve_with_allocator(u32 capacity, struct frame_allocator_int* allocator) {                                   \
        name##_darray arr;                                                                                                                              \
        _kdarray_init(capacity, sizeof(type), capacity, allocator, &arr.length, &arr.stride, &arr.capacity, (void**)&arr.data, &arr.allocator);         \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray name##_darray_create_with_allocator(struct frame_allocator_int* allocator) {                                                  \
        name##_darray arr;                                                                                                                              \
        _kdarray_init(0, sizeof(type), DARRAY_DEFAULT_CAPACITY, allocator, &arr.length, &arr.stride, &arr.capacity, (void**)&arr.data, &arr.allocator); \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray name##_darray_reserve(u32 capacity) {                                                                                         \
        name##_darray arr;                                                                                                                              \
        _kdarray_init(0, sizeof(type), capacity, 0, &arr.length, &arr.stride, &arr.capacity, (void**)&arr.data, &arr.allocator);                        \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray name##_darray_create(void) {                                                                                                  \
        name##_darray arr;                                                                                                                              \
        _kdarray_init(0, sizeof(type), DARRAY_DEFAULT_CAPACITY, 0, &arr.length, &arr.stride, &arr.capacity, (void**)&arr.data, &arr.allocator);         \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray* name##_darray_push(name##_darray* arr, type data) {                                                                          \
        _kdarray_ensure_size(arr->length + 1, arr->stride, &arr->capacity, arr->allocator, (void**)&arr->data);                                         \
        arr->data[arr->length] = data;                                                                                                                  \
        arr->length++;                                                                                                                                  \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE type name##_darray_pop(name##_darray* arr) {                                                                                                \
        type retval = arr->data[arr->length - 1];                                                                                                       \
        arr->length--;                                                                                                                                  \
        return retval;                                                                                                                                  \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE type name##_darray_pop_at(name##_darray* arr, u32 index) {                                                                                  \
        KASSERT_MSG(index < arr->length, "Index outside bounds of darray!");                                                                            \
        type retval = arr->data[index];                                                                                                                 \
        for (u32 i = index; i < arr->length; ++i) {                                                                                                     \
            arr->data[i] = arr->data[i + 1];                                                                                                            \
        }                                                                                                                                               \
        arr->length--;                                                                                                                                  \
        return retval;                                                                                                                                  \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray* name##_darray_insert_at(name##_darray* arr, u32 index, type data) {                                                          \
        KASSERT_MSG(index < arr->length, "Index outside bounds of darray!");                                                                            \
        _kdarray_ensure_size(arr->length + 1, arr->stride, &arr->capacity, arr->allocator, (void**)&arr->data);                                         \
        arr->length++;                                                                                                                                  \
        for (u32 i = arr->length; i > index; --i) {                                                                                                     \
            arr->data[i] = arr->data[i - 1];                                                                                                            \
        }                                                                                                                                               \
        arr->data[index] = data;                                                                                                                        \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray* name##_darray_clear(name##_darray* arr) {                                                                                    \
        arr->length = 0;                                                                                                                                \
        return arr;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE void name##_darray_destroy(name##_darray* arr) {                                                                                            \
        _kdarray_free(&arr->length, &arr->capacity, &arr->stride, (void**)&arr->data, &arr->allocator);                                                 \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE name##_darray_it name##_darray_iterator_begin(name##_darray* arr) {                                                                         \
        name##_darray_it it;                                                                                                                            \
        it.arr = arr;                                                                                                                                   \
        it.pos = 0;                                                                                                                                     \
        return it;                                                                                                                                      \
    }                                                                                                                                                   \
                                                                                                                                                        \
    KINLINE b8 name##_darray_iterator_end(const name##_darray_it* it) { return it->pos >= it->arr->length; }                                            \
    KINLINE type* name##_darray_iterator_value(const name##_darray_it* it) { return &it->arr->data[it->pos]; }                                          \
    KINLINE void name##_darray_iterator_next(name##_darray_it* it) { it->pos++; }                                                                       \
    KINLINE void name##_darray_iterator_prev(name##_darray_it* it) { it->pos--; }

// Create an array type of the given type. For advanced types or pointers, use ARRAY_TYPE_NAMED directly.
#define DARRAY_TYPE(type) DARRAY_TYPE_NAMED(type, type)

// Create array types for well-known types

DARRAY_TYPE(b8);

DARRAY_TYPE(u8);
DARRAY_TYPE(u16);
DARRAY_TYPE(u32);
DARRAY_TYPE(u64);

DARRAY_TYPE(i8);
DARRAY_TYPE(i16);
DARRAY_TYPE(i32);
DARRAY_TYPE(i64);

DARRAY_TYPE(f32);
DARRAY_TYPE(f64);

// Create array types for well-known "advanced" types, such as strings.
DARRAY_TYPE_NAMED(const char*, string);
