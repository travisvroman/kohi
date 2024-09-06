/**
 * @file array.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of a static-sized (but dynamically allocated) array.
 *
 * @details
 * Create an array type for a simple type using:
 *   `ARRAY_TYPE(f64)`.
 * Create an array type for "advanced" types, such as strings, like this:
 *   `ARRAY_TYPE_NAMED(const char*, string)`
 * Both of these types already exist (see the bottom of this file) but others may be easily created this way.
 *
 * @version 1.0
 * @date 2024-08-14
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"

KAPI void _karray_init(u32 length, u32 stride, u32* out_length, u32* out_stride, void** block);
KAPI void _karray_free(u32* length, u32* stride, void** block);

typedef struct array_base {
    u32 length;
    u32 stride;
    void* p_data;
} array_base;

typedef struct array_iterator {
    const array_base* arr;
    i32 pos;
    i32 dir;
    b8 (*end)(const struct array_iterator* it);
    void* (*value)(const struct array_iterator* it);
    void (*next)(struct array_iterator* it);
    void (*prev)(struct array_iterator* it);
} array_iterator;

KAPI array_iterator array_iterator_begin(const array_base* arr);
KAPI array_iterator array_iterator_rbegin(const array_base* arr);
KAPI b8 array_iterator_end(const array_iterator* it);
KAPI void* array_iterator_value(const array_iterator* it);
KAPI void array_iterator_next(array_iterator* it);
KAPI void array_iterator_prev(array_iterator* it);

#define ARRAY_TYPE_NAMED(type, name)                                                                                     \
    struct array_##name;                                                                                                 \
    typedef struct array_##name {                                                                                        \
        array_base base;                                                                                                 \
        type* data;                                                                                                      \
        array_iterator (*begin)(const array_base* arr);                                                                  \
        array_iterator (*rbegin)(const array_base* arr);                                                                 \
    } array_##name;                                                                                                      \
                                                                                                                         \
    KINLINE type* array_##name##_it_value(const array_iterator* it) { return &((array_##name*)it->arr)->data[it->pos]; } \
                                                                                                                         \
    KINLINE array_##name array_##name##_create(u32 length) {                                                             \
        array_##name arr;                                                                                                \
        _karray_init(length, sizeof(type), &arr.base.length, &arr.base.stride, (void**)&arr.data);                       \
        arr.base.p_data = (void*)arr.data;                                                                                      \
        arr.begin = array_iterator_begin;                                                                                \
        arr.rbegin = array_iterator_rbegin;                                                                              \
        return arr;                                                                                                      \
    }                                                                                                                    \
                                                                                                                         \
    KINLINE void array_##name##_destroy(array_##name* arr) {                                                             \
        _karray_free(&arr->base.length, &arr->base.stride, (void**)&arr->data);                                          \
        arr->begin = 0;                                                                                                  \
        arr->rbegin = 0;                                                                                                 \
    }

/**
 * @brief Create an array type of the given type. For advanced types or pointers,
 * use ARRAY_TYPE_NAMED directly.*/
#define ARRAY_TYPE(type) ARRAY_TYPE_NAMED(type, type)

// Create array types for well-known types

ARRAY_TYPE(b8);

ARRAY_TYPE(u8);
ARRAY_TYPE(u16);
ARRAY_TYPE(u32);
ARRAY_TYPE(u64);

ARRAY_TYPE(i8);
ARRAY_TYPE(i16);
ARRAY_TYPE(i32);
ARRAY_TYPE(i64);

ARRAY_TYPE(f32);
ARRAY_TYPE(f64);

// Create array types for well-known "advanced" types, such as strings.

ARRAY_TYPE_NAMED(const char*, string);
