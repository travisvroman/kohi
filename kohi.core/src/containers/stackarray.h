
/**
 * @file stackarray.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of a static-sized stack-allocated array.
 *
 * @details
 * Create an array type for a simple type using:
 *   `STACKARRAY_TYPE(f64, 4)`. This would create f64_stackarray_4.
 * Create an array type for "advanced" types, such as strings, like this:
 *   `STACKARRAY_TYPE_NAMED(const char*, string, 4)`. This would create string_stackarray_4.
 * See the bottom of this file for examples on how to create more stackarray types.
 *
 * @version 1.0
 * @date 2024-08-15
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"

#define STACKARRAY_TYPE_NAMED(type, name, len)                                                                                                        \
    typedef struct name##_stackarray_##len {                                                                                                          \
        type data[len];                                                                                                                               \
    } name##_stackarray_##len;                                                                                                                        \
    typedef struct name##_stackarray_##len##_it {                                                                                                     \
        name##_stackarray_##len* arr;                                                                                                                 \
        i32 pos;                                                                                                                                      \
        i32 dir;                                                                                                                                      \
    } name##_stackarray_##len##_it;                                                                                                                   \
                                                                                                                                                      \
    KINLINE name##_stackarray_##len name##_stackarray_##len##_create(void) {                                                                          \
        name##_stackarray_##len arr;                                                                                                                  \
        kzero_memory(&arr, sizeof(name##_stackarray_##len));                                                                                          \
        return arr;                                                                                                                                   \
    }                                                                                                                                                 \
                                                                                                                                                      \
    KINLINE void name##_stackarray_##len##_destroy(name##_stackarray_##len* arr) {                                                                    \
        kzero_memory(arr, sizeof(name##_stackarray_##len));                                                                                           \
    }                                                                                                                                                 \
                                                                                                                                                      \
    KINLINE name##_stackarray_##len##_it name##_stackarray_##len##_iterator_begin(name##_stackarray_##len* arr) {                                     \
        name##_stackarray_##len##_it it;                                                                                                              \
        it.arr = arr;                                                                                                                                 \
        it.pos = 0;                                                                                                                                   \
        it.dir = 1;                                                                                                                                   \
        return it;                                                                                                                                    \
    }                                                                                                                                                 \
                                                                                                                                                      \
    KINLINE name##_stackarray_##len##_it name##_stackarray_##len##_iterator_begin_reverse(name##_stackarray_##len* arr) {                             \
        name##_stackarray_##len##_it it;                                                                                                              \
        it.arr = arr;                                                                                                                                 \
        it.pos = len - 1;                                                                                                                             \
        it.dir = -1;                                                                                                                                  \
        return it;                                                                                                                                    \
    }                                                                                                                                                 \
                                                                                                                                                      \
    KINLINE b8 name##_stackarray_##len##_iterator_end(const name##_stackarray_##len##_it* it) { return it->dir == 1 ? it->pos >= len : it->pos < 0; } \
    KINLINE type* name##_stackarray_##len##_iterator_value(const name##_stackarray_##len##_it* it) { return &it->arr->data[it->pos]; }                \
    KINLINE void name##_stackarray_##len##_iterator_next(name##_stackarray_##len##_it* it) { it->pos += it->dir; }                                    \
    KINLINE void name##_stackarray_##len##_iterator_prev(name##_stackarray_##len##_it* it) { it->pos -= it->dir; }

// Create an array type of the given type. For advanced types or pointers, use ARRAY_TYPE_NAMED directly.
#define STACKARRAY_TYPE(type, length) STACKARRAY_TYPE_NAMED(type, type, length)

// EXAMPLES:
/*
// Creation of stackarray for standard/simple types:
STACKARRAY_TYPE(b8, 4);
STACKARRAY_TYPE(u8, 2);
STACKARRAY_TYPE(u16, 3);
STACKARRAY_TYPE(u32, 4);
STACKARRAY_TYPE(u64, 5);
STACKARRAY_TYPE(i8, 6);
STACKARRAY_TYPE(i16, 7);
STACKARRAY_TYPE(i32, 8);
STACKARRAY_TYPE(i64, 9);
STACKARRAY_TYPE(f32, 10);
STACKARRAY_TYPE(f64, 11);
*/

// Creation of stackarray "advanced" types, such as strings.
/* STACKARRAY_TYPE_NAMED(const char*, string, 4);  */
