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

/**
 * @brief Duplicates the given array to a completely fresh copy, including
 * header data as well as actual data contained within.
 *
 * Performs a dynamic memory allocation.
 * @param type The type to be used to duplicate the darray. Used for size verification.
 * @param array The array to be duplicated.
 * @returns A pointer to the array's memory block.
 */
KAPI void* _darray_duplicate(u64 stride, void* array);

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
    (type*)_darray_create(DARRAY_DEFAULT_CAPACITY, sizeof(type), allocator)

/**
 * @brief Creates a new darray of the given type with the provided capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @returns A pointer to the array's memory block.
 */
#define darray_reserve(type, capacity) \
    (type*)_darray_create(capacity, sizeof(type), 0)

/**
 * @brief Creates a new darray of the given type with the provided capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @param allocator A pointer to a frame allocator.
 * @returns A pointer to the array's memory block.
 */
#define darray_reserve_with_allocator(type, capacity, allocator) \
    (type*)_darray_create(capacity, sizeof(type), allocator)

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
#define darray_push(array, value)                              \
    {                                                          \
        typeof(value) __k_temp_dingus_value__ = value;         \
        array = _darray_push(array, &__k_temp_dingus_value__); \
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
#define darray_insert_at(array, index, value)                              \
    {                                                                      \
        typeof(value) __k_temp_dingus_value__ = value;                     \
        array = _darray_insert_at(array, index, &__k_temp_dingus_value__); \
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
 * @brief Duplicates the given array to a completely fresh copy, including
 * header data as well as actual data contained within.
 *
 * Performs a dynamic memory allocation.
 * @param type The type to be used to duplicate the darray. Used for size verification.
 * @param array The array to be duplicated.
 * @returns A pointer to the array's memory block.
 */
#define darray_duplicate(type, array) (type*)_darray_duplicate(sizeof(type), array)

/**
 * NEW DARRAY
 */

KAPI void _kdarray_init(u32 length, u32 stride, u32 capacity, struct frame_allocator_int* allocator, u32* out_length, u32* out_stride, u32* out_capacity, void** block, struct frame_allocator_int** out_allocator);
KAPI void _kdarray_free(u32* length, u32* capacity, u32* stride, void** block, struct frame_allocator_int** out_allocator);
KAPI void _kdarray_ensure_size(u32 required_length, u32 stride, u32* out_capacity, struct frame_allocator_int* allocator, void** block, void** base_block);

typedef struct darray_base {
    u32 length;
    u32 stride;
    u32 capacity;
    struct frame_allocator_int* allocator;
    void* p_data;
} darray_base;

typedef struct darray_iterator {
    darray_base* arr;
    i32 pos;
    i32 dir;
    b8 (*end)(const struct darray_iterator* it);
    void* (*value)(const struct darray_iterator* it);
    void (*next)(struct darray_iterator* it);
    void (*prev)(struct darray_iterator* it);
} darray_iterator;

KAPI darray_iterator darray_iterator_begin(darray_base* arr);
KAPI darray_iterator darray_iterator_rbegin(darray_base* arr);
KAPI b8 darray_iterator_end(const darray_iterator* it);
KAPI void* darray_iterator_value(const darray_iterator* it);
KAPI void darray_iterator_next(darray_iterator* it);
KAPI void darray_iterator_prev(darray_iterator* it);

#define DARRAY_TYPE_NAMED(type, name)                                                                                                                                       \
    typedef struct darray_##name {                                                                                                                                          \
        darray_base base;                                                                                                                                                   \
        type* data;                                                                                                                                                         \
        darray_iterator (*begin)(darray_base * arr);                                                                                                                        \
        darray_iterator (*rbegin)(darray_base * arr);                                                                                                                       \
    } darray_##name;                                                                                                                                                        \
                                                                                                                                                                            \
    KINLINE darray_##name darray_##name##_reserve_with_allocator(u32 capacity, struct frame_allocator_int* allocator) {                                                     \
        darray_##name arr;                                                                                                                                                  \
        _kdarray_init(0, sizeof(type), capacity, allocator, &arr.base.length, &arr.base.stride, &arr.base.capacity, (void**)&arr.data, &arr.base.allocator);                \
        arr.base.p_data = arr.data;                                                                                                                                         \
        arr.begin = darray_iterator_begin;                                                                                                                                  \
        arr.rbegin = darray_iterator_rbegin;                                                                                                                                \
        return arr;                                                                                                                                                         \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE darray_##name darray_##name##_create_with_allocator(struct frame_allocator_int* allocator) {                                                                    \
        darray_##name arr;                                                                                                                                                  \
        _kdarray_init(0, sizeof(type), DARRAY_DEFAULT_CAPACITY, allocator, &arr.base.length, &arr.base.stride, &arr.base.capacity, (void**)&arr.data, &arr.base.allocator); \
        arr.base.p_data = arr.data;                                                                                                                                         \
        arr.begin = darray_iterator_begin;                                                                                                                                  \
        arr.rbegin = darray_iterator_rbegin;                                                                                                                                \
        return arr;                                                                                                                                                         \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE darray_##name darray_##name##_reserve(u32 capacity) {                                                                                                           \
        darray_##name arr;                                                                                                                                                  \
        _kdarray_init(0, sizeof(type), capacity, 0, &arr.base.length, &arr.base.stride, &arr.base.capacity, (void**)&arr.data, &arr.base.allocator);                        \
        arr.base.p_data = arr.data;                                                                                                                                         \
        arr.begin = darray_iterator_begin;                                                                                                                                  \
        arr.rbegin = darray_iterator_rbegin;                                                                                                                                \
        return arr;                                                                                                                                                         \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE darray_##name darray_##name##_create(void) {                                                                                                                    \
        darray_##name arr;                                                                                                                                                  \
        _kdarray_init(0, sizeof(type), DARRAY_DEFAULT_CAPACITY, 0, &arr.base.length, &arr.base.stride, &arr.base.capacity, (void**)&arr.data, &arr.base.allocator);         \
        arr.base.p_data = arr.data;                                                                                                                                         \
        arr.begin = darray_iterator_begin;                                                                                                                                  \
        arr.rbegin = darray_iterator_rbegin;                                                                                                                                \
        return arr;                                                                                                                                                         \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE darray_##name* darray_##name##_push(darray_##name* arr, type data) {                                                                                            \
        _kdarray_ensure_size(arr->base.length + 1, arr->base.stride, &arr->base.capacity, arr->base.allocator, (void**)&arr->data, (void**)&arr->base.p_data);              \
        arr->data[arr->base.length] = data;                                                                                                                                 \
        arr->base.length++;                                                                                                                                                 \
        return arr;                                                                                                                                                         \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE b8 darray_##name##_pop(darray_##name* arr, type* out_value) {                                                                                                   \
        if (arr->base.length < 1) {                                                                                                                                         \
            return false;                                                                                                                                                   \
        }                                                                                                                                                                   \
        *out_value = arr->data[arr->base.length - 1];                                                                                                                       \
        arr->base.length--;                                                                                                                                                 \
        return true;                                                                                                                                                        \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE b8 darray_##name##_pop_at(darray_##name* arr, u32 index, type* out_value) {                                                                                     \
        if (index >= arr->base.length) {                                                                                                                                    \
            return false;                                                                                                                                                   \
        }                                                                                                                                                                   \
        *out_value = arr->data[index];                                                                                                                                      \
        for (u32 i = index; i < arr->base.length; ++i) {                                                                                                                    \
            arr->data[i] = arr->data[i + 1];                                                                                                                                \
        }                                                                                                                                                                   \
        arr->base.length--;                                                                                                                                                 \
        return true;                                                                                                                                                        \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE b8 darray_##name##_insert_at(darray_##name* arr, u32 index, type data) {                                                                                        \
        if (index > arr->base.length) {                                                                                                                                     \
            return false;                                                                                                                                                   \
        }                                                                                                                                                                   \
        _kdarray_ensure_size(arr->base.length + 1, arr->base.stride, &arr->base.capacity, arr->base.allocator, (void**)&arr->data, (void**)&arr->base.p_data);              \
        arr->base.length++;                                                                                                                                                 \
        for (u32 i = arr->base.length; i > index; --i) {                                                                                                                    \
            arr->data[i] = arr->data[i - 1];                                                                                                                                \
        }                                                                                                                                                                   \
        arr->data[index] = data;                                                                                                                                            \
        return true;                                                                                                                                                        \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE darray_##name* darray_##name##_clear(darray_##name* arr) {                                                                                                      \
        arr->base.length = 0;                                                                                                                                               \
        return arr;                                                                                                                                                         \
    }                                                                                                                                                                       \
                                                                                                                                                                            \
    KINLINE void darray_##name##_destroy(darray_##name* arr) {                                                                                                              \
        _kdarray_free(&arr->base.length, &arr->base.capacity, &arr->base.stride, (void**)&arr->data, &arr->base.allocator);                                                 \
        arr->begin = 0;                                                                                                                                                     \
        arr->rbegin = 0;                                                                                                                                                    \
    }

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
