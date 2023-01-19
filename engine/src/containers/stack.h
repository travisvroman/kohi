/**
 * @file stack.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A simple stack container. Elements may be pushed on or popped
 * off of the stack only.
 * @version 1.0
 * @date 2023-01-18
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */
#pragma once

#include "defines.h"

/**
 * @brief A simple stack container. Elements may be pushed on or popped
 * off of the stack only.
 */
typedef struct stack {
    /** @brief The element size in bytes.*/
    u32 element_size;
    /** @brief The current element count. */
    u32 element_count;
    /** @brief The total amount of currently-allocated memory.*/
    u32 allocated;
    /** @brief The allocated memory block. */
    void* memory;
} stack;

/**
 * @brief Creates a new stack.
 *
 * @param out_stack A pointer to hold the newly-created stack.
 * @param element_size The size of each element in the stack.
 * @return True on success; otherwise false.
 */
KAPI b8 stack_create(stack* out_stack, u32 element_size);
/**
 * @brief Destroys the given stack.
 *
 * @param s A pointer to the stack to be destroyed.
 */
KAPI void stack_destroy(stack* s);

/**
 * @brief Pushes an element (a copy of the element data) onto the stack.
 *
 * @param s A pointer to the stack to push to.
 * @param element_data The element data to be pushed. Required.
 * @return True on succcess; otherwise false.
 */
KAPI b8 stack_push(stack* s, void* element_data);

/**
 * @brief Attempts to pop an element (writing out a copy of the
 * element data on success) from the stack. If the stack is empty,
 * nothing is done and false is returned.
 *
 * @param s A pointer to the stack to pop from.
 * @param element_data A pointer to write the element data to. Required.
 * @return True on succcess; otherwise false.
 */
KAPI b8 stack_pop(stack* s, void* out_element_data);
